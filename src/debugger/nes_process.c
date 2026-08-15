#include "nes_process.h"

// TODO(RJ): remove this!
void debugger_get_rewind_markers(NES_Process *debugger, u64 *rewind_marker, u64 *rewind_cursor, u64 *replay_marker)
{
	if(rewind_marker) *rewind_marker = debugger->snapshots_rewind_marker;
	if(replay_marker) *replay_marker = debugger->snapshots_replay_marker;
	if(rewind_cursor) *rewind_cursor = debugger->snapshots_cursor;
}

void nes_process_capture_snapshot(NES_Process *debugger)
{
	if (debugger->snapshots_cursor > 0) {
		NES_ProcessState *previous_snapshot = & debugger->snapshots[debugger->snapshots_cursor - 1 & DEBUGGER_SNAPSHOT_MASK];
		if (previous_snapshot->state.scheduler_clock == nes_emulator_scheduler_clock(&debugger->emulator)) {
			return;
		}
	}
	NES_ProcessState *snapshot = (NES_ProcessState *) & debugger->snapshots[debugger->snapshots_cursor & DEBUGGER_SNAPSHOT_MASK];
	debugger->snapshots_replay_marker = ++ debugger->snapshots_cursor;
	debugger->snapshots_rewind_marker = debugger->snapshots_cursor - Min(debugger->snapshots_cursor, DEBUGGER_SNAPSHOT_CAPACITY);
	Assert(debugger->snapshots_replay_marker >= debugger->snapshots_rewind_marker);
	Assert(debugger->snapshots_replay_marker - debugger->snapshots_rewind_marker <= DEBUGGER_SNAPSHOT_CAPACITY);

	memory_zero(snapshot, sizeof(*snapshot));
	snapshot->state = debugger->emulator.state;
}

static void restore_process(NES_Process *debugger, const NES_ProcessState *snapshot)
{
	debugger->emulator.state = snapshot->state;
}

static void update_cpu_mapping(NES_Process *debugger)
{
	u8 changed_chunks = 0;
	for (u32 chunk = 0; chunk < CPU_MAPPING_CHUNK_COUNT; ++chunk)
	{
		u16 cpu_address = (u16)(chunk * CPU_MAPPING_CHUNK_SIZE);
		NES_MapAddr mapped = nes_emulator_cpu_map(&debugger->emulator, cpu_address);
		NES_MapAddr previous = debugger->cpu_mapping.chunks[chunk];
		if (!debugger->cpu_mapping.initialized || mapped.device != previous.device || mapped.address != previous.address)
		{
			debugger->cpu_mapping.chunks[chunk] = mapped;
			changed_chunks |= (u8)(1u << chunk);
		}
	}
	debugger->cpu_mapping.changed_chunks = changed_chunks;
	if (changed_chunks) ++debugger->cpu_mapping.revision;
	debugger->cpu_mapping.initialized = true;
}

b32 nes_process_rewind(NES_Process *debugger)
{
	// Todo, we need to detect wrap around!
	if (debugger->snapshots_cursor <= debugger->snapshots_rewind_marker) return 0;
	restore_process(debugger, &debugger->snapshots[-- debugger->snapshots_cursor & DEBUGGER_SNAPSHOT_MASK]);
	execution_path_discard(&debugger->execution_path);
	update_cpu_mapping(debugger);
	return 1;
}

b32 nes_process_replay(NES_Process *debugger)
{
	if (debugger->snapshots_cursor >= debugger->snapshots_replay_marker) return 0;
	restore_process(debugger, &debugger->snapshots[debugger->snapshots_cursor ++ & DEBUGGER_SNAPSHOT_MASK]);
	execution_path_discard(&debugger->execution_path);
	update_cpu_mapping(debugger);
	return 1;
}

static void clear_timeline(NES_Process *debugger)
{
	debugger->snapshots_replay_marker = 0;
	debugger->snapshots_rewind_marker = 0;
	debugger->snapshots_cursor        = 0;
}

NES_Process *nes_process_create(Arena *arena)
{
	Assert(arena);
	NES_Process *debugger = arena_push_zero(arena, sizeof(*debugger));
	debugger->trace = arena_push(arena, sizeof(*debugger->trace) * DEBUGGER_TRACE_CAPACITY);
	return debugger;
}

static b32 debugger_map_address_equal(NES_MapAddr a, NES_MapAddr b)
{
	return a.device == b.device && a.address == b.address;
}

static b32 mapped_address_to_absolute_address(const NES_Process *debugger, NES_MapAddr mapped, u32 *offset)
{
	if (mapped.device == NES_DEVICE_PRG_ROM && mapped.offset < debugger->program_rom_size)
	{
		*offset = mapped.offset;
		return true;
	}
	if (mapped.device == NES_DEVICE_PRG_RAM && mapped.offset < debugger->program_ram_size)
	{
		*offset = debugger->program_rom_size + mapped.offset;
		return true;
	}
	return false;
}

static void update_program_evidence_from_trace(NES_Process *process, NES_TraceEntry trace)
{
	u32 storage = 0;
	if (!mapped_address_to_absolute_address(process, trace.cpu_mapped, &storage)) return;
	process->program_evidence[storage] |= PROGRAM_INSTRUCTION_EXECUTED;
}

static void seed_program_vector(NES_Process *debugger, u16 vector_address)
{
	u16 cpu_address = nes_emulator_cpu_peek_word(&debugger->emulator, vector_address);
	NES_MapAddr mapped = nes_emulator_cpu_map(&debugger->emulator, cpu_address);
	u32 storage_offset = 0;
	if (!mapped_address_to_absolute_address(debugger, mapped, &storage_offset)) return;
	debugger->program_evidence[storage_offset] |= PROGRAM_INSTRUCTION_STATIC;
}

// TODO(RJ) We have to test whether the execution trace is faster than doing the checks read/write/exec
// checks on the emulator using a flat map.
// Even if we do the trace style, we'd still have a flat map to remove the double loop which scales
// bad for multiple breakpoints ...
// We have to do read/write breakpoints eventually regardless, unless we want to go back to generating
// hundreds of thousands of records and scanning each of them, so this may be partly temporary.
static void process_execution_trace_(NES_Process *debugger, const NES_TraceEntry *trace, u64 trace_count)
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
		update_program_evidence_from_trace(debugger, boundary);
		execution_graph_observe_execution(&debugger->execution_graph, &debugger->execution_path, debugger->program_rom_size, debugger->program_ram_size, boundary);

		for (u32 breakpoint_index = 0; breakpoint_index < debugger->program_breakpoint_count; breakpoint_index++)
		{
			if (!debugger_map_address_equal(boundary.cpu_mapped, debugger->program_breakpoints[breakpoint_index])) continue;

			if (skip_resume && debugger_map_address_equal(boundary.cpu_mapped, debugger->breakpoint_hit_address))
			{
				skip_resume = false;
				continue;
			}

			b32 success = nes_process_rewind(debugger);
			Assert(success);

			for (u64 replay_index = 0; replay_index < trace_index; ++replay_index) {
				nes_emulator_step(&debugger->emulator, 0);
			}
			debugger->breakpoint_hit_address = boundary.cpu_mapped;
			debugger->breakpoint_hit = true;
			debugger->breakpoint_resume_pending = true;
			return;
		}
	}
}

static void process_execution_trace(NES_Process *debugger, const NES_TraceEntry *trace, u64 trace_count)
{
	PROF_BLOCK("process trace") process_execution_trace_(debugger, trace, trace_count);
}

NES_MapAddr nes_process_cpu_mapped_chunk(const NES_Process *debugger, u32 chunk)
{
	Assert(chunk < CPU_MAPPING_CHUNK_COUNT);
	return debugger->cpu_mapping.chunks[chunk];
}

void nes_process_reset(NES_Process *debugger)
{
	execution_path_discard(&debugger->execution_path);
	execution_graph_reset(&debugger->execution_graph);
	debugger->program_rom_size = debugger->emulator.prg_rom_size;
	debugger->program_ram_size = NES_MAX_PRG_RAM_SIZE;
	Assert(debugger->program_rom_size <= NES_MAX_PRG_ROM_SIZE);
	Assert(debugger->program_rom_size + debugger->program_ram_size <= PROGRAM_MAX_SIZE);
	memory_zero(debugger->program_evidence, sizeof(debugger->program_evidence));
	if (nes_emulator_ready_to_run(&debugger->emulator))
	{
		seed_program_vector(debugger, 0xFFFC);
		seed_program_vector(debugger, 0xFFFA);
		seed_program_vector(debugger, 0xFFFE);
	}
	debugger->breakpoint_hit_address = (NES_MapAddr) {};
	debugger->breakpoint_hit = false;
	debugger->breakpoint_resume_pending = false;
	clear_timeline(debugger);
	nes_process_capture_snapshot(debugger);
	update_cpu_mapping(debugger);
}

void nes_process_clear_ram_evidence(NES_Process *debugger)
{
	Assert(debugger->program_rom_size + debugger->program_ram_size <= PROGRAM_MAX_SIZE);
	memory_zero(debugger->program_evidence + debugger->program_rom_size, debugger->program_ram_size);
}

void nes_process_clear_breakpoints(NES_Process *debugger)
{
	Assert(debugger);
	debugger->program_breakpoint_count = 0;
	debugger->breakpoint_hit_address = (NES_MapAddr) {};
	debugger->breakpoint_hit = false;
	debugger->breakpoint_resume_pending = false;
}

u32 nes_process_step(NES_Process *debugger)
{
	Assert(nes_emulator_ready_to_run(&debugger->emulator));
	PROF_BLOCK("snapshot") nes_process_capture_snapshot(debugger);
	u32 cycles = nes_emulator_step(&debugger->emulator, debugger->trace);
	process_execution_trace(debugger, debugger->trace, 1);
	update_cpu_mapping(debugger);
	return cycles;
}

NES_RunFrameResult nes_process_run_frame(NES_Process *debugger, f32 *sample_buffer, u64 sample_capacity)
{
	Assert(nes_emulator_ready_to_run(&debugger->emulator));
	PROF_BLOCK("snapshot") nes_process_capture_snapshot(debugger);
	NES_RunFrameResult result = nes_emulator_run_frame(&debugger->emulator, (NES_RunParams) {
		.samples = sample_buffer,
		.sample_capacity = sample_capacity,
		.trace = debugger->trace,
		.trace_capacity = DEBUGGER_TRACE_CAPACITY,
	});
	process_execution_trace(debugger, debugger->trace, result.steps);
	update_cpu_mapping(debugger);
	if (debugger->breakpoint_hit) return (NES_RunFrameResult) {};
	return result;
}

void nes_process_set_breakpoint(NES_Process *debugger, NES_MapAddr address, b32 enabled)
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

b32 nes_process_has_breakpoint(const NES_Process *debugger, NES_MapAddr address)
{
	for (u32 index = 0; index < debugger->program_breakpoint_count; index++) {
		if (debugger_map_address_equal(debugger->program_breakpoints[index], address)) return true;
	}
	return false;
}

b32 nes_process_hit_breakpoint(const NES_Process *debugger)
{
	return debugger->breakpoint_hit;
}

const ExecutionGraph *debugger_execution_graph(const NES_Process *debugger)
{
	return &debugger->execution_graph;
}
