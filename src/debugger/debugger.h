#ifndef DEBUGGER_DEBUGGER_H
#define DEBUGGER_DEBUGGER_H

#include "base.h"
#include "nes/emulator.h"
#include "program.h"

typedef struct Debugger Debugger;


// TODO, BROTHER ...
typedef struct
{
	u16         cpu_address;
	NES_MapAddr destination;
}
NES_ExecutionMapping;

// TODO, BROTHER ...
typedef struct
{
	const NES_ExecutionMapping *entries;
	u32 capacity;
	u32 count;
	u32 write_index;
	u64 total_count;
}
NES_ExecutionHistory;

typedef struct
{
	NES_CPUState cpu;
	NES_PPUState ppu;
	NES_APUState apu;
}
DebuggerState;

enum
{
	CPU_MAPPING_CHUNK_SIZE = 0x2000,
	CPU_MAPPING_CHUNK_COUNT = NES_CPU_ADDRESS_SPACE / CPU_MAPPING_CHUNK_SIZE,
};

Debugger *debugger_create(Arena *arena, u32 audio_sample_rate);
void debugger_destroy(Debugger *debugger);
b32 debugger_has_cartridge(const Debugger *debugger);
b32 debugger_reset(Debugger *debugger);
b32 debugger_open_rom(Debugger *debugger, ByteSpan data);
ByteSpan debugger_snapshot(Debugger *debugger, Arena *arena);
b32 debugger_restore_snapshot(Debugger *debugger, ByteSpan snapshot);
b32 debugger_save_state(Debugger *debugger, Arena *arena);
b32 debugger_restore_state(Debugger *debugger, ByteSpan state);
void debugger_set_input(Debugger *debugger, NES_Input input, u32 player);
u64 debugger_scheduler_clock(const Debugger *debugger);
u32 debugger_step(Debugger *debugger);
void debugger_run(Debugger *debugger, u64 ppu_cycles);
u64 debugger_run_samples(Debugger *debugger, u64 minimum_samples, f32 *samples, u64 capacity);
void debugger_capture_frame(Debugger *debugger);
b32 debugger_undo_frame(Debugger *debugger);
b32 debugger_redo_frame(Debugger *debugger);
void debugger_set_program_breakpoint(Debugger *debugger, NES_MapAddr address, b32 enabled);
b32 debugger_has_program_breakpoint(const Debugger *debugger, NES_MapAddr address);
b32 debugger_breakpoint_hit(const Debugger *debugger);
void debugger_refine(Debugger *debugger, u32 instruction_budget);
void debugger_update_cpu_mapping(Debugger *debugger);

DebuggerState debugger_capture_state(const Debugger *debugger);
void debugger_capture_video(const Debugger *debugger, u8 *pixels, u32 stride);
void debugger_capture_chr_map(const Debugger *debugger, NES_CHRMap *map);
void debugger_capture_execution_history(const Debugger *debugger, NES_ExecutionMapping *entries, u32 capacity, NES_ExecutionHistory *history);
u32 debugger_prg_rom_size(const Debugger *debugger);
const Program *debugger_program(const Debugger *debugger);

u32 debugger_cpu_peek(Debugger *debugger, u16 address, NES_MapAddr *mapped);
u32 debugger_cpu_peek_word(Debugger *debugger, u16 address);
NES_MapAddr debugger_cpu_map(Debugger *debugger, u16 address);
void debugger_cpu_write(Debugger *debugger, u16 address, u8 value);
NES_MapAddr debugger_cpu_mapping_chunk(const Debugger *debugger, u32 chunk);

#endif
