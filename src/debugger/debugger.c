#include "debugger_internal.h"

void debugger_get_rewind_markers(Debugger *debugger, u64 *rewind_marker, u64 *rewind_cursor, u64 *replay_marker)
{
	if(rewind_marker) *rewind_marker = debugger->snapshots_rewind_marker;
	if(replay_marker) *replay_marker = debugger->snapshots_replay_marker;
	if(rewind_cursor) *rewind_cursor = debugger->snapshots_cursor;
}

void debugger_capture_snapshot(Debugger *debugger)
{
	if (debugger->snapshots_cursor > 0) {
		DBG_LiveSnapshot *previous_snapshot = & debugger->snapshots[debugger->snapshots_cursor - 1 & DEBUGGER_SNAPSHOT_MASK];
		if (previous_snapshot->scheduler_clock == nes_emulator_scheduler_clock(debugger->emulator)) {
			return;
		}
	}
	DBG_LiveSnapshot *snapshot = (DBG_LiveSnapshot *) & debugger->snapshots[debugger->snapshots_cursor & DEBUGGER_SNAPSHOT_MASK];
	debugger->snapshots_replay_marker = ++ debugger->snapshots_cursor;
	debugger->snapshots_rewind_marker = debugger->snapshots_cursor - Min(debugger->snapshots_cursor, DEBUGGER_SNAPSHOT_CAPACITY);
	Assert(debugger->snapshots_replay_marker >= debugger->snapshots_rewind_marker);
	Assert(debugger->snapshots_replay_marker - debugger->snapshots_rewind_marker <= DEBUGGER_SNAPSHOT_CAPACITY);

	NES_Emulator *emulator = debugger->emulator;

	memory_zero(snapshot, sizeof(*snapshot));
	snapshot->sample_phase = emulator->sample_phase;
	memory_copy(snapshot->values, emulator->values, sizeof(snapshot->values));
	snapshot->input_state = emulator->input_state;
	snapshot->cpu_stall_cycles = emulator->cpu_stall_cycles;
	snapshot->scheduler_clock = emulator->scheduler_clock;
	snapshot->cpu = emulator->cpu;
	snapshot->ppu = emulator->ppu;
	snapshot->apu = emulator->apu;
	memory_copy(snapshot->controllers, emulator->controllers, sizeof(snapshot->controllers));
	memory_copy(snapshot->wram, emulator->_wram, sizeof(snapshot->wram));
	memory_copy(snapshot->vram, emulator->_vram, sizeof(snapshot->vram));
	memory_copy(snapshot->chr_ram, emulator->chr_ram, sizeof(snapshot->chr_ram));
	memory_copy(snapshot->prg_ram, emulator->prg_ram, sizeof(snapshot->prg_ram));
	memory_copy(snapshot->video, emulator->video, sizeof(snapshot->video));
}

static void debugger_restore(Debugger *debugger, const DBG_LiveSnapshot *snapshot)
{
	NES_Emulator *emulator = debugger->emulator;
	memory_copy(emulator->values, snapshot->values, sizeof(snapshot->values));
	emulator->sample_phase = snapshot->sample_phase;
	emulator->input_state = snapshot->input_state;
	emulator->cpu_stall_cycles = snapshot->cpu_stall_cycles;
	emulator->scheduler_clock = snapshot->scheduler_clock;
	emulator->cpu = snapshot->cpu;
	emulator->ppu = snapshot->ppu;
	emulator->apu = snapshot->apu;
	memory_copy(emulator->controllers, snapshot->controllers, sizeof(snapshot->controllers));
	memory_copy(emulator->_wram, snapshot->wram, sizeof(snapshot->wram));
	memory_copy(emulator->_vram, snapshot->vram, sizeof(snapshot->vram));
	memory_copy(emulator->chr_ram, snapshot->chr_ram, sizeof(snapshot->chr_ram));
	memory_copy(emulator->prg_ram, snapshot->prg_ram, sizeof(snapshot->prg_ram));
	memory_copy(emulator->video, snapshot->video, sizeof(snapshot->video));
}

b32 debugger_undo_snapshot(Debugger *debugger)
{
	// Todo, we need to detect wrap around!
	if (debugger->snapshots_cursor <= debugger->snapshots_rewind_marker) return 0;
	debugger_restore(debugger, &debugger->snapshots[-- debugger->snapshots_cursor & DEBUGGER_SNAPSHOT_MASK]);
	execution_path_discard(&debugger->execution_path);
	return 1;
}

b32 debugger_redo_snapshot(Debugger *debugger)
{
	if (debugger->snapshots_cursor >= debugger->snapshots_replay_marker) return 0;
	debugger_restore(debugger, &debugger->snapshots[debugger->snapshots_cursor ++ & DEBUGGER_SNAPSHOT_MASK]);
	execution_path_discard(&debugger->execution_path);
	return 1;
}

static void debugger_clear_snapshots(Debugger *debugger)
{
	debugger->snapshots_replay_marker = 0;
	debugger->snapshots_rewind_marker = 0;
	debugger->snapshots_cursor = 0;
}

Debugger *debugger_create(Arena *arena, NES_Emulator *emulator)
{
	Assert(arena && emulator);
	Debugger *debugger = arena_push_zero(arena, sizeof(*debugger));
	debugger->arena = arena;
	debugger->emulator = emulator;
	debugger->trace = arena_push(arena, sizeof(*debugger->trace) * DEBUGGER_TRACE_CAPACITY);
	debugger->program_work_arena = arena_create(0, "debugger program work arena");
	return debugger;
}

static b32 debugger_map_address_equal(NES_MapAddr a, NES_MapAddr b)
{
	return a.device == b.device && a.address == b.address;
}

// TODO(RJ) we have to test whether the execution trace is faster than doing the checks read/write/exec
// checks on the emulator using a flat map.
// Even if we do the trace style, we'd still have a flat map to remove the double loop which scales
// bad for multiple breakpoints ...
// We have to do read/write breakpoints eventually regardless, unless we want to go back to generating
// hundreds of thousands of records and scanning each of them, so this may be partly temporary.
static void debugger_process_trace_(Debugger *debugger, const NES_TraceEntry *trace, u64 trace_count)
{
	debugger->breakpoint_hit = false;

	// Todo, remove this, just make it so that we call debugger step which skips over breakpoints once ...
	b32 skip_resume = false;
	if (debugger->program_breakpoint_count)
	{
		skip_resume = debugger->breakpoint_resume_pending;
		debugger->breakpoint_resume_pending = false;
	}

	for (u64 trace_index = 0; trace_index < trace_count; ++trace_index)
	{
		NES_TraceEntry boundary = trace[trace_index];
		program_observe_execution(debugger, boundary);
		execution_graph_observe_execution(&debugger->execution_graph, &debugger->execution_path, &debugger->program, boundary);

		for (u32 breakpoint_index = 0; breakpoint_index < debugger->program_breakpoint_count; breakpoint_index++)
		{
			if (!debugger_map_address_equal(boundary.cpu_mapped, debugger->program_breakpoints[breakpoint_index])) continue;

			if (skip_resume && debugger_map_address_equal(boundary.cpu_mapped, debugger->breakpoint_hit_address))
			{
				skip_resume = false;
				continue;
			}

			b32 success = debugger_undo_snapshot(debugger);
			Assert(success);

			for (u64 replay_index = 0; replay_index < trace_index; ++replay_index) {
				nes_emulator_step(debugger->emulator, 0);
			}
			debugger->breakpoint_hit_address = boundary.cpu_mapped;
			debugger->breakpoint_hit = true;
			debugger->breakpoint_resume_pending = true;
			return;
		}
	}
}

static void debugger_process_trace(Debugger *debugger, const NES_TraceEntry *trace, u64 trace_count)
{
	PROF_BLOCK("process trace") debugger_process_trace_(debugger, trace, trace_count);
}

void debugger_destroy(Debugger *debugger)
{
	if (!debugger) return;
	arena_destroy(&debugger->program_work_arena);
}

NES_MapAddr debugger_cpu_mapping_chunk(const Debugger *debugger, u32 chunk)
{
	Assert(chunk < CPU_MAPPING_CHUNK_COUNT);
	return debugger->cpu_mapping.chunks[chunk];
}

void debugger_update_cpu_mapping(Debugger *debugger)
{
	u8 changed_chunks = 0;
	for (u32 chunk = 0; chunk < CPU_MAPPING_CHUNK_COUNT; ++chunk)
	{
		u16 cpu_address = (u16)(chunk * CPU_MAPPING_CHUNK_SIZE);
		NES_MapAddr mapped = nes_emulator_cpu_map(debugger->emulator, cpu_address);
		NES_MapAddr previous = debugger->cpu_mapping.chunks[chunk];
		if (!debugger->cpu_mapping.initialized || mapped.device != previous.device || mapped.address != previous.address)
		{
			debugger->cpu_mapping.chunks[chunk] = mapped;
			changed_chunks |= (u8)(1u << chunk);
		}
	}
	debugger->cpu_mapping.changed_chunks = changed_chunks;
	if (changed_chunks) {
		++debugger->cpu_mapping.revision;
	}
	debugger->cpu_mapping.initialized = true;
}

void debugger_reset(Debugger *debugger)
{
	execution_path_discard(&debugger->execution_path);
	execution_graph_reset(&debugger->execution_graph);
	program_reset(debugger);
	debugger_clear_snapshots(debugger);
	debugger_capture_snapshot(debugger);
}

static void debugger_ensure_has_restore_point_in_case_of_breakpoint(Debugger *debugger)
{
	Assert(debugger->snapshots_cursor >= 1);
	DBG_LiveSnapshot *previous_snapshot = & debugger->snapshots[debugger->snapshots_cursor - 1 & DEBUGGER_SNAPSHOT_MASK];
	Assert(nes_emulator_scheduler_clock(debugger->emulator) == previous_snapshot->scheduler_clock);
}

u32 debugger_step(Debugger *debugger)
{
	Assert(nes_emulator_ready_to_run(debugger->emulator));
	PROF_BLOCK("snapshot") debugger_capture_snapshot(debugger);
	debugger_ensure_has_restore_point_in_case_of_breakpoint(debugger);
	u32 cycles = nes_emulator_step(debugger->emulator, debugger->trace);
	debugger_process_trace(debugger, debugger->trace, 1);
	return cycles;
}

NES_RunFrameResult debugger_run_frame(Debugger *debugger, f32 *sample_buffer, u64 sample_capacity)
{
	Assert(nes_emulator_ready_to_run(debugger->emulator));
	PROF_BLOCK("snapshot") debugger_capture_snapshot(debugger);
	debugger_ensure_has_restore_point_in_case_of_breakpoint(debugger);
	NES_RunFrameResult result = nes_emulator_run_frame(debugger->emulator, (NES_RunParams) {
		.samples = sample_buffer,
		.sample_capacity = sample_capacity,
		.trace = debugger->trace,
		.trace_capacity = DEBUGGER_TRACE_CAPACITY,
	});
	debugger_process_trace(debugger, debugger->trace, result.steps);
	if (debugger->breakpoint_hit) return (NES_RunFrameResult) {};
	return result;
}

void debugger_set_program_breakpoint(Debugger *debugger, NES_MapAddr address, b32 enabled)
{
	for (u32 index = 0; index < debugger->program_breakpoint_count; index++)
	{
		if (!debugger_map_address_equal(debugger->program_breakpoints[index], address)) continue;
		if (!enabled)
		{
			debugger->program_breakpoints[index] = debugger->program_breakpoints[--debugger->program_breakpoint_count];
		}
		return;
	}
	if (enabled)
	{
		Assert(debugger->program_breakpoint_count < DEBUGGER_PROGRAM_BREAKPOINT_CAPACITY);
		debugger->program_breakpoints[debugger->program_breakpoint_count++] = address;
	}
}

b32 debugger_has_program_breakpoint(const Debugger *debugger, NES_MapAddr address)
{
	for (u32 index = 0; index < debugger->program_breakpoint_count; index++) {
		if (debugger_map_address_equal(debugger->program_breakpoints[index], address)) return true;
	}
	return false;
}

b32 debugger_breakpoint_hit(const Debugger *debugger)
{
	return debugger->breakpoint_hit;
}

void debugger_run_program_crawler(Debugger *debugger, u32 instruction_budget)
{
	program_refine(debugger, instruction_budget);
}

const Program *debugger_program(const Debugger *debugger)
{
	return &debugger->program;
}

const ExecutionGraph *debugger_execution_graph(const Debugger *debugger)
{
	return &debugger->execution_graph;
}
