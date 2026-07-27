#ifndef FRONTEND_ACTIVITY_TRACKER_H
#define FRONTEND_ACTIVITY_TRACKER_H

#include "base.h"
#include "debugger.h"

enum
{
	ACTIVITY_TRACKER_EDGE_CAPACITY = KiB(16),
	ACTIVITY_TRACKER_SAMPLE_CAPACITY = KiB(1),
};

#define ACTIVITY_TRACKER_SUSTAIN_INTENSITY 0.05f
#define ACTIVITY_TRACKER_DEAD_INTENSITY 0.01f

typedef struct
{
	u32 source_offset;
	u32 destination_offset;
	f32 activity;
}
ActivityEdge;

typedef struct
{
	u64 key;
	u32 pending_occurrences;
	f32 activity;
}
ActivityTrackerEntry;

typedef struct
{
	ActivityTrackerEntry entries[ACTIVITY_TRACKER_EDGE_CAPACITY];
	u16 used_slots[ACTIVITY_TRACKER_EDGE_CAPACITY];
	u32 edge_count;
	u64 consumed_history_count;
	f64 last_update_seconds;
}
ActivityTracker;

void activity_tracker_reset(ActivityTracker *tracker, u64 consumed_history_count);
void activity_tracker_record(ActivityTracker *tracker, u32 source_offset, u32 destination_offset, u64 sequence);
void activity_tracker_update(ActivityTracker *tracker, f64 now_seconds);
void activity_tracker_observe_execution(ActivityTracker *tracker, const Debugger *debugger, NES_ExecutionHistory history);
u32 activity_tracker_sample(const ActivityTracker *tracker, u32 cell_size, ActivityEdge *edges, u32 capacity);

#endif
