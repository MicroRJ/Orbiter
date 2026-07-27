#include "activity_tracker.h"
#include "nes/isa.h"
#include <stdio.h>

static ActivityTracker tracker;
static ActivityTracker observed_tracker;
static Program observed_program;

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
	if (!nes_instruction_is_control_flow(BNE_REL) ||
		!nes_instruction_is_control_flow(JMP_ABS) ||
		!nes_instruction_is_control_flow(JSR_ABS) ||
		!nes_instruction_is_control_flow(RTS_IMP) ||
		!nes_instruction_is_control_flow(RTI_IMP) ||
		!nes_instruction_is_control_flow(BRK_IMP) ||
		nes_instruction_is_control_flow(LDA_IMM)) return 1;

	ActivityEdge edges[16] = {};
	activity_tracker_reset(&tracker);

	activity_tracker_record(&tracker, 5, 19);
	activity_tracker_record(&tracker, 5, 19);
	activity_tracker_record(&tracker, 7, 23);
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

	activity_tracker_record(&tracker, 5, 19);
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

	activity_tracker_record(&tracker, 5, 19);
	activity_tracker_update(&tracker, 5.0);
	count = activity_tracker_sample(&tracker, 1, edges, ArrayCount(edges));
	exact = find_edge(edges, count, 5, 19);
	if (exact < 0 || edges[exact].activity < 0.999f) return 10;

	observed_program.prg_rom_byte_count = KiB(32);
	activity_tracker_reset(&observed_tracker);
	activity_tracker_observe_execution(&observed_tracker, &observed_program, (NES_SchedulerBoundary) { .cpu_address = 0x8000, .cpu_mapped = nes_map_addr(NES_DEVICE_PRG_ROM, 5), .cpu_byte = LDA_IMM });
	activity_tracker_observe_execution(&observed_tracker, &observed_program, (NES_SchedulerBoundary) { .cpu_address = 0x8002, .cpu_mapped = nes_map_addr(NES_DEVICE_PRG_ROM, 7), .cpu_byte = BNE_REL });
	if (observed_tracker.edge_count != 0) return 11;
	activity_tracker_observe_execution(&observed_tracker, &observed_program, (NES_SchedulerBoundary) { .cpu_address = 0x9000, .cpu_mapped = nes_map_addr(NES_DEVICE_PRG_ROM, 23), .cpu_byte = LDA_IMM });
	if (observed_tracker.edge_count != 1) return 12;
	activity_tracker_discard_sequence(&observed_tracker);
	activity_tracker_observe_execution(&observed_tracker, &observed_program, (NES_SchedulerBoundary) { .cpu_address = 0x8100, .cpu_mapped = nes_map_addr(NES_DEVICE_PRG_ROM, 29), .cpu_byte = LDA_IMM });
	if (observed_tracker.edge_count != 1) return 13;
	activity_tracker_observe_execution(&observed_tracker, &observed_program, (NES_SchedulerBoundary) { .cpu_address = 0x8102, .cpu_mapped = nes_map_addr(NES_DEVICE_PRG_ROM, 31), .cpu_byte = LDA_IMM });
	if (observed_tracker.edge_count != 1) return 14;
	activity_tracker_observe_execution(&observed_tracker, &observed_program, (NES_SchedulerBoundary) { .cpu_address = 0x9000, .cpu_mapped = nes_map_addr(NES_DEVICE_PRG_ROM, 40), .cpu_byte = LDA_IMM });
	if (observed_tracker.edge_count != 2) return 15;
	activity_tracker_discard_sequence(&observed_tracker);
	activity_tracker_observe_execution(&observed_tracker, &observed_program, (NES_SchedulerBoundary) { .cpu_address = 0xB000, .cpu_mapped = nes_map_addr(NES_DEVICE_PRG_ROM, 50), .cpu_byte = BNE_REL });
	activity_tracker_observe_execution(&observed_tracker, &observed_program, (NES_SchedulerBoundary) { .cpu_address = 0xB000, .cpu_mapped = nes_map_addr(NES_DEVICE_PRG_ROM, 50), .cpu_byte = LDA_IMM });
	if (observed_tracker.edge_count != 3) return 16;

	printf("Activity tracker tests passed\n");
	return 0;
}
