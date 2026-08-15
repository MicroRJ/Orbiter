#ifndef DEBUGGER_DEBUGGER_H
#define DEBUGGER_DEBUGGER_H

#include "base.h"
#include "nes/emulator.h"
#include "program.h"
#include "execution_graph.h"

typedef struct NES_Process NES_Process;

enum
{
	CPU_MAPPING_CHUNK_SIZE = 0x2000,
	CPU_MAPPING_CHUNK_COUNT = NES_CPU_ADDRESS_SPACE / CPU_MAPPING_CHUNK_SIZE,
};

NES_Process *debugger_create(Arena *arena, NES_Emulator *emulator);
void debugger_reset(NES_Process *debugger);
void debugger_clear_program_breakpoints(NES_Process *debugger);
u32 debugger_step(NES_Process *debugger);


NES_RunFrameResult debugger_run_frame(NES_Process *debugger, f32 *samples, u64 sample_capacity);

void debugger_set_program_breakpoint(NES_Process *debugger, NES_MapAddr address, b32 enabled);
b32 debugger_has_program_breakpoint(const NES_Process *debugger, NES_MapAddr address);
b32 debugger_breakpoint_hit(const NES_Process *debugger);
void debugger_update_cpu_mapping(NES_Process *debugger);

void debugger_get_rewind_markers(NES_Process *debugger, u64 *rewind_marker, u64 *rewind_cursor, u64 *replay_marker);
void debugger_capture_snapshot(NES_Process *debugger);
b32 debugger_undo_snapshot(NES_Process *debugger);
b32 debugger_redo_snapshot(NES_Process *debugger);

const Program *debugger_program(const NES_Process *debugger);
const ExecutionGraph *debugger_execution_graph(const NES_Process *debugger);

NES_MapAddr debugger_cpu_mapping_chunk(const NES_Process *debugger, u32 chunk);

#endif
