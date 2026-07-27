#include "activity_tracker.h"
#include <stdio.h>

static ActivityTracker tracker;

static i32 find_edge(ActivityEdge *edges, u32 count, u32 source_offset, u32 destination_offset)
{
	for (u32 index = 0; index < count; ++index) {
		if (edges[index].source_offset == source_offset && edges[index].destination_offset == destination_offset) {
			return (i32)index;
		}
	}
	return -1;
}

int main(void)
{
	ActivityEdge edges[16] = {};
	activity_tracker_reset(&tracker, 12);
	if (tracker.consumed_history_count != 12) return 1;

	activity_tracker_record(&tracker, 5, 19, 13);
	activity_tracker_record(&tracker, 5, 19, 14);
	activity_tracker_record(&tracker, 7, 23, 15);
	activity_tracker_update(&tracker, 1.0);
	if (tracker.edge_count != 2) return 2;

	u32 count = activity_tracker_sample(&tracker, 1, edges, ArrayCount(edges));
	i32 exact = find_edge(edges, count, 5, 19);
	if (exact < 0 || edges[exact].activity <= 0.f) return 3;

	count = activity_tracker_sample(&tracker, 16, edges, ArrayCount(edges));
	if (find_edge(edges, count, 0, 16) < 0) return 4;
	if (tracker.edge_count != 2) return 5;

	f32 intensity = edges[find_edge(edges, count, 0, 16)].activity;
	activity_tracker_update(&tracker, 2.0);
	count = activity_tracker_sample(&tracker, 16, edges, ArrayCount(edges));
	i32 decayed = find_edge(edges, count, 0, 16);
	if (decayed < 0 || edges[decayed].activity >= intensity) return 6;

	activity_tracker_record(&tracker, 5, 19, 16);
	activity_tracker_update(&tracker, 2.1);
	count = activity_tracker_sample(&tracker, 1, edges, ArrayCount(edges));
	exact = find_edge(edges, count, 5, 19);
	if (exact < 0 || edges[exact].activity > ACTIVITY_TRACKER_SUSTAIN_INTENSITY + 0.001f) return 7;

	activity_tracker_update(&tracker, 2.85);
	count = activity_tracker_sample(&tracker, 1, edges, ArrayCount(edges));
	exact = find_edge(edges, count, 5, 19);
	if (exact < 0 || edges[exact].activity >= ACTIVITY_TRACKER_SUSTAIN_INTENSITY) return 8;

	activity_tracker_update(&tracker, 4.0);
	count = activity_tracker_sample(&tracker, 1, edges, ArrayCount(edges));
	if (find_edge(edges, count, 5, 19) >= 0) return 9;

	activity_tracker_record(&tracker, 5, 19, 17);
	activity_tracker_update(&tracker, 5.0);
	count = activity_tracker_sample(&tracker, 1, edges, ArrayCount(edges));
	exact = find_edge(edges, count, 5, 19);
	if (exact < 0 || edges[exact].activity < 0.999f) return 10;

	printf("Activity tracker tests passed\n");
	return 0;
}
