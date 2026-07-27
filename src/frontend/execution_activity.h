#ifndef FRONTEND_EXECUTION_ACTIVITY_H
#define FRONTEND_EXECUTION_ACTIVITY_H

#include "execution_graph.h"

enum
{
	EXECUTION_ACTIVITY_SAMPLE_CAPACITY = KiB(1),
};

typedef struct
{
	u32 observed_hit_count;
	f64 stimulus_time;
	f32 intensity;
}
EdgeActivityState;

typedef struct
{
	EdgeActivityState edges[EXECUTION_GRAPH_EDGE_CAPACITY];
	u32 graph_generation;
}
ExecutionActivity;

typedef struct
{
	u32 source_offset;
	u32 destination_offset;
	f32 intensity;
}
ExecutionActivitySample;

void execution_activity_update(ExecutionActivity *activity, const ExecutionGraph *graph, f64 now_seconds);
u32 execution_activity_sample(const ExecutionActivity *activity, const ExecutionGraph *graph, u32 cell_size, ExecutionActivitySample *samples, u32 capacity);

#endif
