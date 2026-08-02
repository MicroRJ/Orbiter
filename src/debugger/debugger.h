#ifndef DEBUGGER_DEBUGGER_H
#define DEBUGGER_DEBUGGER_H

#include "base.h"
#include "nes/emulator.h"
#include "program.h"
#include "execution_graph.h"

typedef struct Debugger Debugger;
struct NES_TargetPublication;

enum
{
	CPU_MAPPING_CHUNK_SIZE = 0x2000,
	CPU_MAPPING_CHUNK_COUNT = NES_CPU_ADDRESS_SPACE / CPU_MAPPING_CHUNK_SIZE,
};

Debugger *debugger_create(Arena *arena, NES_Emulator *emulator);
void debugger_destroy(Debugger *debugger);
b32 debugger_armed(const Debugger *debugger);
b32 debugger_open_rom(Debugger *debugger, ByteSpan data);
b32 debugger_save_state(Debugger *debugger, Arena *arena);
b32 debugger_restore_state(Debugger *debugger, ByteSpan state);
void debugger_set_input(Debugger *debugger, NES_Input input, u32 player);
u64 debugger_scheduler_clock(const Debugger *debugger);
u32 debugger_step(Debugger *debugger);


NES_RunFrameResult debugger_run_frame(Debugger *debugger, f32 *samples, u64 sample_capacity);

void debugger_set_program_breakpoint(Debugger *debugger, NES_MapAddr address, b32 enabled);
b32 debugger_has_program_breakpoint(const Debugger *debugger, NES_MapAddr address);
b32 debugger_breakpoint_hit(const Debugger *debugger);
void debugger_run_program_crawler(Debugger *debugger, u32 instruction_budget);
void debugger_update_cpu_mapping(Debugger *debugger);

void debugger_capture_snapshot(Debugger *debugger);
b32 debugger_undo_snapshot(Debugger *debugger);
b32 debugger_redo_snapshot(Debugger *debugger);

const Program *debugger_program(const Debugger *debugger);
const ExecutionGraph *debugger_execution_graph(const Debugger *debugger);

u32 debugger_cpu_peek(Debugger *debugger, u16 address, NES_MapAddr *mapped);
u32 debugger_cpu_peek_word(Debugger *debugger, u16 address);
NES_MapAddr debugger_cpu_map(Debugger *debugger, u16 address);
NES_MapAddr debugger_cpu_mapping_chunk(const Debugger *debugger, u32 chunk);

void debugger_publish_target(Debugger *debugger, struct NES_TargetPublication *publication);

#endif
