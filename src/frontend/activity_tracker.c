#include "activity_tracker.h"

typedef struct
{
	u64 key;
	f32 intensity;
}
ActivitySampleEntry;

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

static f32 activity_edge_intensity(f32 pulse, f32 frequency)
{
	return CLAMP(pulse + frequency * 0.15f, 0.f, 1.f);
}

void activity_tracker_reset(ActivityTracker *tracker, u64 consumed_history_count)
{
	memory_zero(tracker, sizeof(*tracker));
	tracker->consumed_history_count = consumed_history_count;
}

void activity_tracker_record(ActivityTracker *tracker, u32 source_offset, u32 destination_offset, u64 sequence)
{
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
			entry->last_seen_sequence = sequence;
			entry->pending_sequence_gap = ~(u64)0;
			entry->pending_occurrences = 1;
			tracker->used_slots[tracker->edge_count++] = (u16)slot;
			return;
		}
		if (entry->key == key)
		{
			if (!entry->pending_occurrences) {
				entry->pending_sequence_gap = sequence - entry->last_seen_sequence;
			}
			entry->last_seen_sequence = sequence;
			++entry->pending_occurrences;
			return;
		}
		slot = (slot + 1) & (ACTIVITY_TRACKER_EDGE_CAPACITY - 1);
	}
}

void activity_tracker_update(ActivityTracker *tracker, f64 now_seconds)
{
	f64 elapsed = tracker->last_update_seconds > 0.0 ? now_seconds - tracker->last_update_seconds : 0.0;
	f32 pulse_decay = expf((f32)(-12.0 * Max(elapsed, 0.0)));
	f32 frequency_decay = expf((f32)(-0.18 * Max(elapsed, 0.0)));
	for (u32 index = 0; index < tracker->edge_count; ++index)
	{
		ActivityTrackerEntry *entry = &tracker->entries[tracker->used_slots[index]];
		entry->pulse *= pulse_decay;
		entry->frequency *= frequency_decay;
		if (!entry->pending_occurrences) {
			continue;
		}

		f32 novelty = entry->pending_sequence_gap == ~(u64)0
			? 1.f : Min((f32)entry->pending_sequence_gap / 4096.f, 1.f);
		entry->pulse = Max(entry->pulse, 0.12f + novelty * (1.f - entry->frequency) * 0.88f);
		f32 reinforcement = 1.f - powf(0.92f, (f32)entry->pending_occurrences);
		entry->frequency += (1.f - entry->frequency) * reinforcement;
		entry->pending_occurrences = 0;
		entry->pending_sequence_gap = 0;
	}
	tracker->last_update_seconds = now_seconds;
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
				sample->intensity = Max(sample->intensity, activity_edge_intensity(entry->pulse, entry->frequency));
				break;
			}
			slot = (slot + 1) & (ACTIVITY_TRACKER_SAMPLE_CAPACITY - 1);
		}
	}

	u32 edge_count = 0;
	for (u32 slot = 0; slot < ACTIVITY_TRACKER_SAMPLE_CAPACITY && edge_count < capacity; ++slot)
	{
		const ActivitySampleEntry *sample = &samples[slot];
		if (!sample->key || sample->intensity < 0.01f) {
			continue;
		}
		u64 raw_key = sample->key - 1;
		edges[edge_count++] = (ActivityEdge) {
			.source_offset = (u32)(raw_key >> 32),
			.destination_offset = (u32)raw_key,
			.pulse = sample->intensity,
		};
	}
	return edge_count;
}
