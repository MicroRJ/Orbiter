#include "execution_activity.h"

void execution_activity_update(ExecutionActivity *activity, const ExecutionGraph *graph, f64 now_seconds)
{
	if (activity->graph_generation != graph->generation)
	{
		memory_zero(activity, sizeof(*activity));
		activity->graph_generation = graph->generation;
	}
	for (u32 index = 0; index < graph->edge_count; ++index)
	{
		const ExecutionEdge *edge = execution_graph_edge(graph, index);
		EdgeActivityState *state = &activity->edges[index];

		// detect stimulus
		b32 stimulated = state->observed_hit_count != edge->hit_count;
		state->observed_hit_count = edge->hit_count;

		// decay smoothly
		state->intensity *= 0.85f;
		if (stimulated)
		{
			f64 time_since = now_seconds - state->stimulus_time;
			state->stimulus_time = now_seconds;

			// detect transition, reset intensity
			if (time_since >= 0.035f) state->intensity = 1.f;

			// we're stimulated, do not fade away
			state->intensity = Max(state->intensity, 0.15f);
		}

	}
}

typedef struct
{
	u64 key;
	f32 intensity;
}
ExecutionActivitySampleEntry;

static u64 execution_activity_key(u32 source_offset, u32 destination_offset)
{
	return (((u64)source_offset << 32) | destination_offset) + 1;
}

static u32 execution_activity_hash(u64 key)
{
	key ^= key >> 30;
	key *= 0xBF58476D1CE4E5B9ull;
	key ^= key >> 27;
	key *= 0x94D049BB133111EBull;
	key ^= key >> 31;
	return (u32)key & (EXECUTION_ACTIVITY_SAMPLE_CAPACITY - 1);
}

static f32 execution_activity_decay(f32 value, f32 half_life, f32 elapsed)
{
	return value * exp2f(-elapsed / half_life);
}

u32 execution_activity_sample(const ExecutionActivity *activity, const ExecutionGraph *graph, u32 cell_size, ExecutionActivitySample *samples, u32 capacity)
{
	Assert(cell_size);
	ExecutionActivitySampleEntry table[EXECUTION_ACTIVITY_SAMPLE_CAPACITY] = {};
	for (u32 index = 0; index < graph->edge_count; ++index)
	{
		const ExecutionEdge *edge = execution_graph_edge(graph, index);
		f32 intensity = activity->edges[index].intensity;
		if (intensity <= 0.f) {
			continue;
		}

		u32 source_offset = execution_edge_source_offset(edge) / cell_size * cell_size;
		u32 destination_offset = execution_edge_destination_offset(edge) / cell_size * cell_size;
		if (source_offset == destination_offset) {
			continue;
		}

		u64 key = execution_activity_key(source_offset, destination_offset);
		u32 slot = execution_activity_hash(key);
		for (u32 probe = 0; probe < EXECUTION_ACTIVITY_SAMPLE_CAPACITY; ++probe)
		{
			ExecutionActivitySampleEntry *entry = &table[slot];
			if (!entry->key || entry->key == key)
			{
				entry->key = key;
				entry->intensity = Max(entry->intensity, intensity);
				break;
			}
			slot = (slot + 1) & (EXECUTION_ACTIVITY_SAMPLE_CAPACITY - 1);
		}
	}

	u32 sample_count = 0;
	for (u32 slot = 0; slot < EXECUTION_ACTIVITY_SAMPLE_CAPACITY && sample_count < capacity; ++slot)
	{
		const ExecutionActivitySampleEntry *entry = &table[slot];
		if (!entry->key) {
			continue;
		}

		u64 raw_key = entry->key - 1;
		samples[sample_count++] = (ExecutionActivitySample) {
			.source_offset = (u32)(raw_key >> 32),
			.destination_offset = (u32)raw_key,
			.intensity = entry->intensity,
		};
	}
	return sample_count;
}
