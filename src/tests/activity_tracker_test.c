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
	if (exact < 0 || edges[exact].pulse <= 0.f) return 3;

	count = activity_tracker_sample(&tracker, 16, edges, ArrayCount(edges));
	if (find_edge(edges, count, 0, 16) < 0) return 4;
	if (tracker.edge_count != 2) return 5;

	f32 intensity = edges[find_edge(edges, count, 0, 16)].pulse;
	activity_tracker_update(&tracker, 2.0);
	count = activity_tracker_sample(&tracker, 16, edges, ArrayCount(edges));
	i32 decayed = find_edge(edges, count, 0, 16);
	if (decayed < 0 || edges[decayed].pulse >= intensity) return 6;

	printf("Activity tracker tests passed\n");
	return 0;
}
