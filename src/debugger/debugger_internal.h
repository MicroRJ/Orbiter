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
	DEBUGGER_SNAPSHOT_CAPACITY = 1024,
	DEBUGGER_SNAPSHOT_MASK = DEBUGGER_SNAPSHOT_CAPACITY - 1,
	DEBUGGER_RUNTIME_CHR_RAM_SIZE = KiB(8),
	DEBUGGER_RUNTIME_PRG_RAM_SIZE = KiB(8),
};

typedef struct
{
	NES_MapperId mapper_number;
	u32 prg_rom_size;
	u32 chr_rom_size;
	u8 values[32];
	NES_InputState input_state;
	u32 cpu_stall_cycles;
	u64 audio_sample_phase;
	u64 scheduler_clock;
	NES_CPUState cpu;
	NES_PPUState ppu;
	NES_APUState apu;
	u8 controllers[2];
	u8 wram[NES_WRAM_SIZE];
	u8 vram[NES_VRAM_SIZE];
	u8 chr_ram[DEBUGGER_RUNTIME_CHR_RAM_SIZE];
	u8 prg_ram[DEBUGGER_RUNTIME_PRG_RAM_SIZE];
	u8 video[NES_VIDEO_HEIGHT][NES_VIDEO_WIDTH];
}
DebuggerRuntimeSnapshot;

struct Debugger
{
	Arena *arena;
	Arena program_work_arena;
	NES_Emulator *emulator;
	DebuggerRuntimeSnapshot *snapshots;
	DebuggerRuntimeSnapshot breakpoint_snapshot;
	u64 snapshot_count;
	u64 snapshot_cursor;
	Program program;
	CPU_MappingSnapshot cpu_mapping;
	NES_ExecutionMapping execution_history[NES_EXECUTION_HISTORY_CAPACITY];
	u32 execution_history_count;
	u32 execution_history_write_index;
	u64 execution_history_total_count;
	NES_MapAddr program_breakpoints[DEBUGGER_PROGRAM_BREAKPOINT_CAPACITY];
	u32 program_breakpoint_count;
	NES_MapAddr breakpoint_hit_address;
	b32 breakpoint_hit;
	b32 breakpoint_resume_pending;
	b32 warned_missing_cartridge;
};

#endif
