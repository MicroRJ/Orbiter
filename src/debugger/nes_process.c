#include "nes_process.h"

NES_ProcessTimeline nes_process_timeline(const NES_Process *process)
{
	Assert(process);
	return (NES_ProcessTimeline) {
		.rewind_marker = process->snapshots_rewind_marker,
		.cursor = process->snapshots_cursor,
		.replay_marker = process->snapshots_replay_marker,
	};
}

void nes_process_capture_snapshot(NES_Process *process)
{
	if (process->snapshots_cursor > 0) {
		NES_ProcessState *previous_snapshot = & process->snapshots[process->snapshots_cursor - 1 & NES_PROCESS_SNAPSHOT_MASK];
		if (previous_snapshot->state.scheduler_clock == nes_emulator_scheduler_clock(&process->emulator)) {
			return;
		}
	}
	NES_ProcessState *snapshot = (NES_ProcessState *) & process->snapshots[process->snapshots_cursor & NES_PROCESS_SNAPSHOT_MASK];
	process->snapshots_replay_marker = ++ process->snapshots_cursor;
	process->snapshots_rewind_marker = process->snapshots_cursor - Min(process->snapshots_cursor, NES_PROCESS_SNAPSHOT_CAPACITY);
	Assert(process->snapshots_replay_marker >= process->snapshots_rewind_marker);
	Assert(process->snapshots_replay_marker - process->snapshots_rewind_marker <= NES_PROCESS_SNAPSHOT_CAPACITY);

	memory_zero(snapshot, sizeof(*snapshot));
	snapshot->state = process->emulator.state;
}

static void restore_process(NES_Process *process, const NES_ProcessState *snapshot)
{
	process->emulator.state = snapshot->state;
}

static void update_cpu_mapping(NES_Process *process)
{
	u8 changed_chunks = 0;
	for (u32 chunk = 0; chunk < CPU_MAPPING_CHUNK_COUNT; ++chunk)
	{
		u16 cpu_address = (u16)(chunk * CPU_MAPPING_CHUNK_SIZE);
		NES_MapAddr mapped = nes_emulator_cpu_map(&process->emulator, cpu_address);
		NES_MapAddr previous = process->cpu_mapping.chunks[chunk];
		if (!process->cpu_mapping.initialized || mapped.device != previous.device || mapped.address != previous.address)
		{
			process->cpu_mapping.chunks[chunk] = mapped;
			changed_chunks |= (u8)(1u << chunk);
		}
	}
	process->cpu_mapping.changed_chunks = changed_chunks;
	if (changed_chunks) ++process->cpu_mapping.revision;
	process->cpu_mapping.initialized = true;
}

b32 nes_process_rewind(NES_Process *process)
{
	// Todo, we need to detect wrap around!
	if (process->snapshots_cursor <= process->snapshots_rewind_marker) return 0;
	restore_process(process, &process->snapshots[-- process->snapshots_cursor & NES_PROCESS_SNAPSHOT_MASK]);
	execution_path_discard(&process->execution_path);
	update_cpu_mapping(process);
	return 1;
}

b32 nes_process_replay(NES_Process *process)
{
	if (process->snapshots_cursor >= process->snapshots_replay_marker) return 0;
	restore_process(process, &process->snapshots[process->snapshots_cursor ++ & NES_PROCESS_SNAPSHOT_MASK]);
	execution_path_discard(&process->execution_path);
	update_cpu_mapping(process);
	return 1;
}

static void clear_timeline(NES_Process *process)
{
	process->snapshots_replay_marker = 0;
	process->snapshots_rewind_marker = 0;
	process->snapshots_cursor        = 0;
}

NES_Process *nes_process_create(Arena *arena)
{
	Assert(arena);
	NES_Process *process = arena_push_zero(arena, sizeof(*process));
	process->trace = arena_push(arena, sizeof(*process->trace) * NES_PROCESS_TRACE_CAPACITY);
	return process;
}

static b32 map_address_equal(NES_MapAddr a, NES_MapAddr b)
{
	return a.device == b.device && a.address == b.address;
}

static b32 mapped_address_to_absolute_address(const NES_Process *process, NES_MapAddr mapped, u32 *offset)
{
	if (mapped.device == NES_DEVICE_PRG_ROM && mapped.offset < process->program_rom_size)
	{
		*offset = mapped.offset;
		return true;
	}
	if (mapped.device == NES_DEVICE_PRG_RAM && mapped.offset < process->program_ram_size)
	{
		*offset = process->program_rom_size + mapped.offset;
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

static void seed_program_vector(NES_Process *process, u16 vector_address)
{
	u16 cpu_address = nes_emulator_cpu_peek_word(&process->emulator, vector_address);
	NES_MapAddr mapped = nes_emulator_cpu_map(&process->emulator, cpu_address);
	u32 storage_offset = 0;
	if (!mapped_address_to_absolute_address(process, mapped, &storage_offset)) return;
	process->program_evidence[storage_offset] |= PROGRAM_INSTRUCTION_STATIC;
}

// TODO(RJ) We have to test whether the execution trace is faster than doing the checks read/write/exec
// checks on the emulator using a flat map.
// Even if we do the trace style, we'd still have a flat map to remove the double loop which scales
// bad for multiple breakpoints ...
// We have to do read/write breakpoints eventually regardless, unless we want to go back to generating
// hundreds of thousands of records and scanning each of them, so this may be partly temporary.
static void process_execution_trace_(NES_Process *process, const NES_TraceEntry *trace, u64 trace_count)
{
	process->breakpoint_hit = false;

	// Todo, remove this, just make it so that we call nes_process_step which skips over breakpoints once ...
	b32 skip_resume = false;
	if (process->program_breakpoint_count)
	{
		skip_resume = process->breakpoint_resume_pending;
		process->breakpoint_resume_pending = false;
	}

	for (u64 trace_index = 0; trace_index < trace_count; ++trace_index)
	{
		NES_TraceEntry boundary = trace[trace_index];
		update_program_evidence_from_trace(process, boundary);
		execution_graph_observe_execution(&process->execution_graph, &process->execution_path, process->program_rom_size, process->program_ram_size, boundary);

		for (u32 breakpoint_index = 0; breakpoint_index < process->program_breakpoint_count; breakpoint_index++)
		{
			if (!map_address_equal(boundary.cpu_mapped, process->program_breakpoints[breakpoint_index])) continue;

			if (skip_resume && map_address_equal(boundary.cpu_mapped, process->breakpoint_hit_address))
			{
				skip_resume = false;
				continue;
			}

			b32 success = nes_process_rewind(process);
			Assert(success);

			for (u64 replay_index = 0; replay_index < trace_index; ++replay_index) {
				nes_emulator_step(&process->emulator, 0);
			}
			process->breakpoint_hit_address = boundary.cpu_mapped;
			process->breakpoint_hit = true;
			process->breakpoint_resume_pending = true;
			return;
		}
	}
}

static void process_execution_trace(NES_Process *process, const NES_TraceEntry *trace, u64 trace_count)
{
	PROF_BLOCK("process trace") process_execution_trace_(process, trace, trace_count);
}

NES_MapAddr nes_process_cpu_mapped_chunk(const NES_Process *process, u32 chunk)
{
	Assert(chunk < CPU_MAPPING_CHUNK_COUNT);
	return process->cpu_mapping.chunks[chunk];
}

void nes_process_reset(NES_Process *process)
{
	execution_path_discard(&process->execution_path);
	execution_graph_reset(&process->execution_graph);
	process->program_rom_size = process->emulator.prg_rom_size;
	process->program_ram_size = NES_MAX_PRG_RAM_SIZE;
	Assert(process->program_rom_size <= NES_MAX_PRG_ROM_SIZE);
	Assert(process->program_rom_size + process->program_ram_size <= PROGRAM_MAX_SIZE);
	memory_zero(process->program_evidence, sizeof(process->program_evidence));
	if (nes_emulator_ready_to_run(&process->emulator))
	{
		seed_program_vector(process, 0xFFFC);
		seed_program_vector(process, 0xFFFA);
		seed_program_vector(process, 0xFFFE);
	}
	process->breakpoint_hit_address = (NES_MapAddr) {};
	process->breakpoint_hit = false;
	process->breakpoint_resume_pending = false;
	clear_timeline(process);
	nes_process_capture_snapshot(process);
	update_cpu_mapping(process);
}

void nes_process_clear_ram_evidence(NES_Process *process)
{
	Assert(process->program_rom_size + process->program_ram_size <= PROGRAM_MAX_SIZE);
	memory_zero(process->program_evidence + process->program_rom_size, process->program_ram_size);
}

void nes_process_clear_breakpoints(NES_Process *process)
{
	Assert(process);
	process->program_breakpoint_count = 0;
	process->breakpoint_hit_address = (NES_MapAddr) {};
	process->breakpoint_hit = false;
	process->breakpoint_resume_pending = false;
}

u32 nes_process_step(NES_Process *process)
{
	Assert(nes_emulator_ready_to_run(&process->emulator));
	PROF_BLOCK("snapshot") nes_process_capture_snapshot(process);
	u32 cycles = nes_emulator_step(&process->emulator, process->trace);
	process_execution_trace(process, process->trace, 1);
	update_cpu_mapping(process);
	return cycles;
}

NES_RunFrameResult nes_process_run_frame(NES_Process *process, f32 *sample_buffer, u64 sample_capacity)
{
	Assert(nes_emulator_ready_to_run(&process->emulator));
	PROF_BLOCK("snapshot") nes_process_capture_snapshot(process);
	NES_RunFrameResult result = nes_emulator_run_frame(&process->emulator, (NES_RunParams) {
		.samples = sample_buffer,
		.sample_capacity = sample_capacity,
		.trace = process->trace,
		.trace_capacity = NES_PROCESS_TRACE_CAPACITY,
	});
	process_execution_trace(process, process->trace, result.steps);
	update_cpu_mapping(process);
	if (process->breakpoint_hit) return (NES_RunFrameResult) {};
	return result;
}

void nes_process_set_breakpoint(NES_Process *process, NES_MapAddr address, b32 enabled)
{
	for (u32 index = 0; index < process->program_breakpoint_count; index++)
	{
		if (!map_address_equal(process->program_breakpoints[index], address)) continue;
		if (!enabled)
		{
			process->program_breakpoints[index] = process->program_breakpoints[--process->program_breakpoint_count];
		}
		return;
	}
	if (enabled)
	{
		Assert(process->program_breakpoint_count < NES_PROCESS_BREAKPOINT_CAPACITY);
		process->program_breakpoints[process->program_breakpoint_count++] = address;
	}
}

b32 nes_process_has_breakpoint(const NES_Process *process, NES_MapAddr address)
{
	for (u32 index = 0; index < process->program_breakpoint_count; index++) {
		if (map_address_equal(process->program_breakpoints[index], address)) return true;
	}
	return false;
}

b32 nes_process_hit_breakpoint(const NES_Process *process)
{
	return process->breakpoint_hit;
}

const ExecutionGraph *nes_process_execution_graph(const NES_Process *process)
{
	return &process->execution_graph;
}
