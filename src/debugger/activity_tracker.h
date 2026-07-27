#ifndef DEBUGGER_ACTIVITY_TRACKER_H
#define DEBUGGER_ACTIVITY_TRACKER_H

#include "base.h"
#include "program.h"

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
	NES_SchedulerBoundary previous_boundary;
	b32 has_previous_boundary;
	f64 last_update_seconds;
}
ActivityTracker;

void activity_tracker_reset(ActivityTracker *tracker);
void activity_tracker_discard_sequence(ActivityTracker *tracker);
void activity_tracker_record(ActivityTracker *tracker, u32 source_offset, u32 destination_offset);
void activity_tracker_observe_execution(ActivityTracker *tracker, const Program *program, NES_SchedulerBoundary boundary);
void activity_tracker_update(ActivityTracker *tracker, f64 now_seconds);
u32 activity_tracker_sample(const ActivityTracker *tracker, u32 cell_size, ActivityEdge *edges, u32 capacity);

#endif
