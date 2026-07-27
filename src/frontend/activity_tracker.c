#include "activity_tracker.h"

typedef struct
{
	u64 key;
	f32 activity;
}
ActivitySampleEntry;

static const f32 activity_spike_half_life_seconds = 0.035f;
static const f32 activity_release_half_life_seconds = 0.750f;

static u64 activity_edge_key(u32 source_offset, u32 destination_offset)
{
	return (((u64)source_offset << 32) | destination_offset) + 1;
}

static u32 activity_hash(u64 key, u32 capacity)
{
	key ^= key >> 30;
	key *= 0xBF58476D1CE4E5B9ull;
	key ^= key >> 27;
	key *= 0x94D049BB133111EBull;
	key ^= key >> 31;
	return (u32)key & (capacity - 1);
}

static f32 activity_decay(f32 value, f32 half_life, f32 elapsed)
{
	return value * exp2f(-elapsed / half_life);
}

static b32 activity_storage_offset(const Program *program, NES_MapAddr mapped, b32 include_prg_ram, u32 *offset)
{
	if (mapped.device == NES_DEVICE_PRG_ROM && mapped.offset < program->prg_rom_byte_count)
	{
		*offset = mapped.offset;
		return true;
	}
	if (include_prg_ram && mapped.device == NES_DEVICE_PRG_RAM && mapped.offset < program->prg_ram_byte_count)
	{
		*offset = program->prg_rom_byte_count + mapped.offset;
		return true;
	}
	return false;
}

static b32 activity_has_mapped_ram(const Debugger *debugger)
{
	for (u32 chunk = 0; chunk < CPU_MAPPING_CHUNK_COUNT; ++chunk) {
		if (debugger_cpu_mapping_chunk(debugger, chunk).device == NES_DEVICE_PRG_RAM) {
			return true;
		}
	}
	return false;
}

void activity_tracker_reset(ActivityTracker *tracker, u64 consumed_history_count)
{
	memory_zero(tracker, sizeof(*tracker));
	tracker->consumed_history_count = consumed_history_count;
}

void activity_tracker_record(ActivityTracker *tracker, u32 source_offset, u32 destination_offset, u64 sequence)
{
	(void)sequence;
	u64 key = activity_edge_key(source_offset, destination_offset);
	u32 slot = activity_hash(key, ACTIVITY_TRACKER_EDGE_CAPACITY);
	for (u32 probe = 0; probe < ACTIVITY_TRACKER_EDGE_CAPACITY; ++probe)
	{
		ActivityTrackerEntry *entry = &tracker->entries[slot];
		if (!entry->key)
		{
			if (tracker->edge_count == ACTIVITY_TRACKER_EDGE_CAPACITY) {
				return;
			}
			entry->key = key;
			entry->pending_occurrences = 1;
			tracker->used_slots[tracker->edge_count++] = (u16)slot;
			return;
		}
		if (entry->key == key)
		{
			++entry->pending_occurrences;
			return;
		}
		slot = (slot + 1) & (ACTIVITY_TRACKER_EDGE_CAPACITY - 1);
	}
}

void activity_tracker_update(ActivityTracker *tracker, f64 now_seconds)
{
	f32 elapsed = tracker->last_update_seconds > 0.0 ? (f32)Max(now_seconds - tracker->last_update_seconds, 0.0) : 0.f;
	for (u32 index = 0; index < tracker->edge_count; ++index)
	{
		ActivityTrackerEntry *entry = &tracker->entries[tracker->used_slots[index]];
		b32 stimulated = entry->pending_occurrences != 0;
		if (entry->activity > ACTIVITY_TRACKER_SUSTAIN_INTENSITY)
		{
			f32 time_to_sustain = log2f(entry->activity / ACTIVITY_TRACKER_SUSTAIN_INTENSITY) * activity_spike_half_life_seconds;
			if (elapsed < time_to_sustain) {
				entry->activity = activity_decay(entry->activity, activity_spike_half_life_seconds, elapsed);
			}
			else
			{
				entry->activity = ACTIVITY_TRACKER_SUSTAIN_INTENSITY;
				if (!stimulated) entry->activity = activity_decay(entry->activity, activity_release_half_life_seconds, elapsed - time_to_sustain);
			}
		}
		else if (stimulated) {
			entry->activity = entry->activity < ACTIVITY_TRACKER_DEAD_INTENSITY ? 1.f : ACTIVITY_TRACKER_SUSTAIN_INTENSITY;
		}
		else {
			entry->activity = activity_decay(entry->activity, activity_release_half_life_seconds, elapsed);
		}
		entry->pending_occurrences = 0;
	}
	tracker->last_update_seconds = now_seconds;
}

void activity_tracker_observe_execution(ActivityTracker *tracker, const Debugger *debugger, NES_ExecutionHistory history)
{
	Assert(tracker);
	Assert(debugger);
	const Program *program = debugger_program(debugger);
	b32 include_prg_ram = activity_has_mapped_ram(debugger);
	if (history.total_count < tracker->consumed_history_count) {
		activity_tracker_reset(tracker, 0);
	}
	u64 oldest_sequence = history.total_count - history.count;
	u64 first_destination = Max(tracker->consumed_history_count, oldest_sequence + 1);
	for (u64 sequence = first_destination; sequence < history.total_count; ++sequence)
	{
		const NES_ExecutionMapping *source = &history.entries[(sequence - 1) % history.capacity];
		const NES_ExecutionMapping *destination = &history.entries[sequence % history.capacity];
		u32 source_offset = 0;
		u32 destination_offset = 0;
		if (activity_storage_offset(program, source->destination, include_prg_ram, &source_offset) &&
			activity_storage_offset(program, destination->destination, include_prg_ram, &destination_offset) &&
			source_offset != destination_offset) {
			activity_tracker_record(tracker, source_offset, destination_offset, sequence);
		}
	}
	tracker->consumed_history_count = history.total_count;
	activity_tracker_update(tracker, seconds_now().seconds);
}

u32 activity_tracker_sample(const ActivityTracker *tracker, u32 cell_size, ActivityEdge *edges, u32 capacity)
{
	ActivitySampleEntry samples[ACTIVITY_TRACKER_SAMPLE_CAPACITY] = {};
	for (u32 index = 0; index < tracker->edge_count; ++index)
	{
		const ActivityTrackerEntry *entry = &tracker->entries[tracker->used_slots[index]];
		u64 raw_key = entry->key - 1;
		u32 source_offset = (u32)(raw_key >> 32) / cell_size * cell_size;
		u32 destination_offset = (u32)raw_key / cell_size * cell_size;
		if (source_offset == destination_offset) {
			continue;
		}
		u64 key = activity_edge_key(source_offset, destination_offset);
		u32 slot = activity_hash(key, ACTIVITY_TRACKER_SAMPLE_CAPACITY);
		for (u32 probe = 0; probe < ACTIVITY_TRACKER_SAMPLE_CAPACITY; ++probe)
		{
			ActivitySampleEntry *sample = &samples[slot];
			if (!sample->key || sample->key == key)
			{
				sample->key = key;
				sample->activity = Max(sample->activity, entry->activity);
				break;
			}
			slot = (slot + 1) & (ACTIVITY_TRACKER_SAMPLE_CAPACITY - 1);
		}
	}

	u32 edge_count = 0;
	for (u32 slot = 0; slot < ACTIVITY_TRACKER_SAMPLE_CAPACITY && edge_count < capacity; ++slot)
	{
		const ActivitySampleEntry *sample = &samples[slot];
		if (!sample->key || sample->activity < ACTIVITY_TRACKER_DEAD_INTENSITY) {
			continue;
		}
		u64 raw_key = sample->key - 1;
		edges[edge_count++] = (ActivityEdge) {
			.source_offset = (u32)(raw_key >> 32),
			.destination_offset = (u32)raw_key,
			.activity = sample->activity,
		};
	}
	return edge_count;
}
