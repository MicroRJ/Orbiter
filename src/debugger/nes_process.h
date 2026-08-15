#ifndef DEBUGGER_DEBUGGER_H
#define DEBUGGER_DEBUGGER_H

#include "base.h"
#include "nes/emulator.h"
#include "program.h"
#include "execution_graph.h"
#include "emulator_internal.h"


enum
{
	CPU_MAPPING_CHUNK_SIZE                = KiB(8),
	CPU_MAPPING_CHUNK_COUNT               = NES_CPU_ADDRESS_SPACE / CPU_MAPPING_CHUNK_SIZE,
	DEBUGGER_PROGRAM_BREAKPOINT_CAPACITY  = 256,
	DEBUGGER_TRACE_CAPACITY               = 16 * 1024,
	DEBUGGER_SNAPSHOT_CAPACITY            = 1024,
	DEBUGGER_SNAPSHOT_MASK                = DEBUGGER_SNAPSHOT_CAPACITY - 1,
};

typedef struct
{
	NES_MapAddr chunks[CPU_MAPPING_CHUNK_COUNT];
	u64         revision;
	u8          changed_chunks;
	b32         initialized;
}
CPU_MappingSnapshot;

typedef struct
{
	NES_State state;
}
NES_ProcessSnapshot;

// TODO(RJ): the timeline size will become configurable and compress-able!
struct NES_Process
{
	NES_Emulator emulator;
	NES_TraceEntry *trace;
	u32                program_rom_size;
	u32                program_ram_size;
	u8                 program_evidence[PROGRAM_MAX_SIZE];
	ExecutionGraph     execution_graph;
	ExecutionPathState execution_path;
	CPU_MappingSnapshot cpu_mapping;
	u32 program_breakpoint_count;
	NES_MapAddr breakpoint_hit_address;
	b32 breakpoint_hit;
	b32 breakpoint_resume_pending;
	b32 warned_missing_cartridge;

	NES_MapAddr       program_breakpoints[DEBUGGER_PROGRAM_BREAKPOINT_CAPACITY];
	u64               snapshots_rewind_marker;
	u64               snapshots_replay_marker;
	u64               snapshots_cursor;

	NES_ProcessSnapshot  snapshots[DEBUGGER_SNAPSHOT_CAPACITY];
};

NES_Process *nes_process_create(Arena *arena);
void nes_process_reset(NES_Process *debugger);
void nes_process_clear_ram_evidence(NES_Process *debugger);
void nes_process_clear_breakpoints(NES_Process *debugger);
u32 nes_process_step(NES_Process *debugger);
NES_RunFrameResult nes_process_run_frame(NES_Process *debugger, f32 *samples, u64 sample_capacity);

void nes_process_set_breakpoint(NES_Process *debugger, NES_MapAddr address, b32 enabled);
b32 nes_process_has_breakpoint(const NES_Process *debugger, NES_MapAddr address);
b32 nes_process_hit_breakpoint(const NES_Process *debugger);
void nes_process_capture_snapshot(NES_Process *debugger);
b32 nes_process_rewind(NES_Process *debugger);
b32 nes_process_replay(NES_Process *debugger);


NES_MapAddr nes_process_cpu_mapped_chunk(const NES_Process *debugger, u32 chunk);


const ExecutionGraph *debugger_execution_graph(const NES_Process *debugger);
void debugger_get_rewind_markers(NES_Process *debugger, u64 *rewind_marker, u64 *rewind_cursor, u64 *replay_marker);
#endif
