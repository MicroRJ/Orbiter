#ifndef NES_PROCESS_H
#define NES_PROCESS_H

#include "base.h"
#include "nes/emulator.h"
#include "program.h"
#include "execution_graph.h"
#include "emulator_internal.h"


enum
{
	CPU_MAPPING_CHUNK_SIZE                = KiB(8),
	CPU_MAPPING_CHUNK_COUNT               = NES_CPU_ADDRESS_SPACE / CPU_MAPPING_CHUNK_SIZE,
	NES_PROCESS_BREAKPOINT_CAPACITY = 256,
	NES_PROCESS_TRACE_CAPACITY      = 16 * 1024,
	NES_PROCESS_SNAPSHOT_CAPACITY   = 1024,
	NES_PROCESS_SNAPSHOT_MASK       = NES_PROCESS_SNAPSHOT_CAPACITY - 1,
};

// TODO(RJ): we don't really need this ...
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
NES_ProcessState;

typedef struct
{
	u64 rewind_marker;
	u64 cursor;
	u64 replay_marker;
}
NES_ProcessTimeline;

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

	NES_MapAddr       program_breakpoints[NES_PROCESS_BREAKPOINT_CAPACITY];
	u64               snapshots_rewind_marker;
	u64               snapshots_replay_marker;
	u64               snapshots_cursor;

	NES_ProcessState  snapshots[NES_PROCESS_SNAPSHOT_CAPACITY];
};

NES_Process *nes_process_create(Arena *arena);
void nes_process_reset(NES_Process *process);
void nes_process_clear_ram_evidence(NES_Process *process);
void nes_process_clear_breakpoints(NES_Process *process);
u32 nes_process_step(NES_Process *process);
NES_RunFrameResult nes_process_run_frame(NES_Process *process, f32 *samples, u64 sample_capacity);

void nes_process_set_breakpoint(NES_Process *process, NES_MapAddr address, b32 enabled);
b32 nes_process_has_breakpoint(const NES_Process *process, NES_MapAddr address);
b32 nes_process_hit_breakpoint(const NES_Process *process);
void nes_process_capture_snapshot(NES_Process *process);
b32 nes_process_rewind(NES_Process *process);
b32 nes_process_replay(NES_Process *process);


NES_MapAddr nes_process_cpu_mapped_chunk(const NES_Process *process, u32 chunk);


const ExecutionGraph *nes_process_execution_graph(const NES_Process *process);
NES_ProcessTimeline nes_process_timeline(const NES_Process *process);
#endif
