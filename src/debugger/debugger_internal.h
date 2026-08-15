#ifndef DEBUGGER_DEBUGGER_INTERNAL_H
#define DEBUGGER_DEBUGGER_INTERNAL_H

#include "debugger.h"
#include "emulator_internal.h"

typedef struct
{
	NES_MapAddr chunks[CPU_MAPPING_CHUNK_COUNT];
	u64 revision;
	u8 changed_chunks;
	b32 initialized;
}
CPU_MappingSnapshot;

enum
{
	DEBUGGER_PROGRAM_BREAKPOINT_CAPACITY = 256,
	DEBUGGER_TRACE_CAPACITY              = 16 * 1024,
	DEBUGGER_SNAPSHOT_CAPACITY     = 1024,
	DEBUGGER_SNAPSHOT_MASK         = DEBUGGER_SNAPSHOT_CAPACITY - 1,
	DEBUGGER_RUNTIME_CHR_RAM_SIZE  = KiB(8),
	DEBUGGER_RUNTIME_PRG_RAM_SIZE  = KiB(8),
};

typedef struct
{
	// ---
	u64              sample_phase;
	// ---
	u8                 values[32];
	NES_InputState    input_state;
	u32          cpu_stall_cycles;
	u64           scheduler_clock;
	NES_CPUState              cpu;
	NES_PPUState              ppu;
	NES_APUState              apu;
	u8             controllers[2];
	u8        wram[NES_WRAM_SIZE];
	u8        vram[NES_VRAM_SIZE];
	u8 chr_ram[DEBUGGER_RUNTIME_CHR_RAM_SIZE];
	u8 prg_ram[DEBUGGER_RUNTIME_PRG_RAM_SIZE];
	u8 video[NES_VIDEO_HEIGHT][NES_VIDEO_WIDTH];
}
DBG_LiveSnapshot;

struct NES_Process
{
	NES_Emulator *emulator;
	NES_TraceEntry *trace;
	Program program;
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

	// TODO(RJ) this will become configurable!
	DBG_LiveSnapshot  snapshots[DEBUGGER_SNAPSHOT_CAPACITY];
};

#endif
