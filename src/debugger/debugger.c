#include "debugger_internal.h"

static void debugger_capture_runtime_snapshot(const Debugger *debugger, DebuggerRuntimeSnapshot *snapshot)
{
	memory_zero(snapshot, sizeof(*snapshot));
	const NES_Emulator *emulator = debugger->emulator;
	const NES_State *state = &emulator->core;
	snapshot->mapper_number = state->mapper_number;
	snapshot->prg_rom_size = state->prg_rom_size;
	snapshot->chr_rom_size = state->chr_rom_size;
	memory_copy(snapshot->values, state->values, sizeof(snapshot->values));
	snapshot->input_state = state->input_state;
	snapshot->cpu_stall_cycles = state->cpu_stall_cycles;
	snapshot->audio_sample_phase = state->audio_sample_phase;
	snapshot->scheduler_clock = emulator->scheduler_clock;
	snapshot->cpu = state->cpu;
	snapshot->ppu = state->ppu;
	snapshot->apu = state->apu;
	memory_copy(snapshot->controllers, state->controllers, sizeof(snapshot->controllers));
	memory_copy(snapshot->wram, state->_wram, sizeof(snapshot->wram));
	memory_copy(snapshot->vram, state->_vram, sizeof(snapshot->vram));
	memory_copy(snapshot->chr_ram, state->chr_ram, sizeof(snapshot->chr_ram));
	memory_copy(snapshot->prg_ram, state->prg_ram, sizeof(snapshot->prg_ram));
	memory_copy(snapshot->video, emulator->video, sizeof(snapshot->video));
}

static b32 debugger_restore_runtime_snapshot(Debugger *debugger, const DebuggerRuntimeSnapshot *snapshot)
{
	NES_Emulator *emulator = debugger->emulator;
	NES_State *state = &emulator->core;
	if (snapshot->mapper_number.number != state->mapper_number.number ||
		snapshot->prg_rom_size != state->prg_rom_size ||
		snapshot->chr_rom_size != state->chr_rom_size) return false;
	memory_copy(state->values, snapshot->values, sizeof(snapshot->values));
	state->input_state = snapshot->input_state;
	state->cpu_stall_cycles = snapshot->cpu_stall_cycles;
	state->audio_sample_phase = snapshot->audio_sample_phase;
	emulator->scheduler_clock = snapshot->scheduler_clock;
	state->cpu = snapshot->cpu;
	state->ppu = snapshot->ppu;
	state->apu = snapshot->apu;
	memory_copy(state->controllers, snapshot->controllers, sizeof(snapshot->controllers));
	memory_copy(state->_wram, snapshot->wram, sizeof(snapshot->wram));
	memory_copy(state->_vram, snapshot->vram, sizeof(snapshot->vram));
	memory_copy(state->chr_ram, snapshot->chr_ram, sizeof(snapshot->chr_ram));
	memory_copy(state->prg_ram, snapshot->prg_ram, sizeof(snapshot->prg_ram));
	memory_copy(emulator->video, snapshot->video, sizeof(snapshot->video));
	return true;
}

static void debugger_clear_snapshots(Debugger *debugger)
{
	debugger->snapshot_count = 0;
	debugger->snapshot_cursor = 0;
}

static void debugger_prepare_execution(Debugger *debugger)
{
	if (debugger->snapshot_count && debugger->snapshot_cursor + 1 < debugger->snapshot_count) {
		debugger->snapshot_count = debugger->snapshot_cursor + 1;
	}
}

Debugger *debugger_create(Arena *arena, u32 audio_sample_rate)
{
	Debugger *debugger = arena_push_zero(arena, sizeof(*debugger));
	debugger->arena = arena;
	debugger->program_work_arena = arena_create(0, "debugger program work arena");
	debugger->emulator = nes_emulator_create(arena, (NES_EmulatorDesc) {
		.audio_sample_rate = audio_sample_rate,
		.enable_instruction_trace = true,
		.enable_instruction_boundaries = true,
	});
	return debugger;
}

static void debugger_discard_scheduler_trace(Debugger *debugger)
{
	NES_SchedulerTraceView trace = nes_emulator_scheduler_trace(debugger->emulator);
	debugger->personal_scheduler_trace_index = trace.index;
	debugger->personal_scheduler_trace_clock = trace.scheduler_clock;
	execution_path_discard(&debugger->execution_path);
}

static b32 debugger_map_address_equal(NES_MapAddr a, NES_MapAddr b)
{
	return a.device == b.device && a.address == b.address;
}

// Todo, this is to be removed and replaced with a map that also handles reads/writes to arbitrary memory
static void debugger_process_scheduler_trace_(Debugger *debugger)
{
	debugger->breakpoint_hit = false;

	NES_SchedulerTraceView trace = nes_emulator_scheduler_trace(debugger->emulator);
	NES_SchedulerTraceSpans trace_spans = nes_scheduler_trace_spans_since(trace, debugger->personal_scheduler_trace_index);
	if (trace_spans.dropped) LOG_WARN("instruction trace dropped %llu events", trace_spans.dropped);
	b32 clock_reconstructable = nes_scheduler_trace_clock_reconstructable_since(trace, debugger->personal_scheduler_trace_clock);
	if (!clock_reconstructable && debugger->program_breakpoint_count) LOG_WARN("instruction trace exceeded the 16-bit breakpoint clock window");
	debugger->personal_scheduler_trace_index = trace.index;
	debugger->personal_scheduler_trace_clock = trace.scheduler_clock;

	b32 skip_resume = false;
	if (debugger->program_breakpoint_count)
	{
		skip_resume = debugger->breakpoint_resume_pending;
		debugger->breakpoint_resume_pending = false;
	}

	b32 found_expired_breakpoint = false;
	for (u32 span_index = 0; span_index < ArrayCount(trace_spans.spans); ++span_index)
	{
		NES_SchedulerTraceSpan span = trace_spans.spans[span_index];
		const NES_SchedulerTraceEntry *end = span.entries + span.count;
		for (const NES_SchedulerTraceEntry *entry = span.entries; entry < end; ++entry)
		{
			NES_SchedulerBoundary boundary = nes_scheduler_trace_decode(trace, *entry);
			program_observe_execution(debugger, boundary);
			execution_graph_observe_execution(&debugger->execution_graph, &debugger->execution_path, &debugger->program, boundary);

			if (!clock_reconstructable) continue;
			for (u32 breakpoint_index = 0; breakpoint_index < debugger->program_breakpoint_count; breakpoint_index++)
			{
				if (!debugger_map_address_equal(boundary.cpu_mapped, debugger->program_breakpoints[breakpoint_index])) continue;
				if (skip_resume && debugger_map_address_equal(boundary.cpu_mapped, debugger->breakpoint_hit_address))
				{
					skip_resume = false;
					continue;
				}
				if (!debugger_restore_runtime_snapshot(debugger, &debugger->breakpoint_snapshot))
				{
					found_expired_breakpoint = true;
					continue;
				}
				while (debugger_scheduler_clock(debugger) < boundary.scheduler_clock) {
					nes_emulator_step(debugger->emulator);
				}
				debugger_discard_scheduler_trace(debugger);
				debugger->breakpoint_hit_address = boundary.cpu_mapped;
				debugger->breakpoint_hit = true;
				debugger->breakpoint_resume_pending = true;
				return;
			}
		}
	}
	if (found_expired_breakpoint) {
		LOG_WARN("could not restore the runtime snapshot for a program breakpoint");
	}
}

static void debugger_process_scheduler_trace(Debugger *debugger)
{
	PROF_BLOCK("process scheduler trace") debugger_process_scheduler_trace_(debugger);
}

void debugger_destroy(Debugger *debugger)
{
	if (!debugger) return;
	arena_destroy(&debugger->program_work_arena);
}

b32 debugger_has_cartridge(const Debugger *debugger)
{
	return debugger && nes_emulator_has_cartridge(debugger->emulator);
}

u32 debugger_cpu_peek(Debugger *debugger, u16 address, NES_MapAddr *mapped)
{
	NES_MapAddr result = nes_emulator_cpu_map(debugger->emulator, address);
	if (mapped) {
		*mapped = result;
	}
	return nes_emulator_cpu_peek(debugger->emulator, address);
}

u32 debugger_cpu_peek_word(Debugger *debugger, u16 address)
{
	return nes_emulator_cpu_peek_word(debugger->emulator, address);
}

NES_MapAddr debugger_cpu_map(Debugger *debugger, u16 address)
{
	return nes_emulator_cpu_map(debugger->emulator, address);
}

void debugger_cpu_write(Debugger *debugger, u16 address, u8 value)
{
	nes_emulator_cpu_write(debugger->emulator, address, value);
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
		NES_MapAddr mapped = debugger_cpu_map(debugger, cpu_address);
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

b32 debugger_reset(Debugger *debugger)
{
	if (!debugger_has_cartridge(debugger))
	{
		LOG_WARN("cannot reset emulator: no cartridge is loaded");
		return false;
	}
	nes_emulator_reset(debugger->emulator);
	debugger_discard_scheduler_trace(debugger);
	execution_graph_reset(&debugger->execution_graph);
	program_reset(debugger);
	debugger_clear_snapshots(debugger);
	debugger_capture_frame(debugger);
	return true;
}

b32 debugger_open_rom(Debugger *debugger, ByteSpan data)
{
	NES_CartridgeDesc cartridge = {};
	if (!nes_cartridge_parse_ines(data, &cartridge))
	{
		LOG_WARN("failed to load ROM: invalid or unsupported iNES image");
		return false;
	}
	b32 success = nes_emulator_load_cartridge(debugger->emulator, cartridge);
	if (success)
	{
		debugger->warned_missing_cartridge = false;
		debugger_discard_scheduler_trace(debugger);
		execution_graph_reset(&debugger->execution_graph);
		program_reset(debugger);
		debugger_clear_snapshots(debugger);
		debugger_capture_frame(debugger);
	}
	else LOG_WARN("failed to load ROM: mapper %u or its cartridge configuration is unsupported", cartridge.mapper);
	return success;
}

ByteSpan debugger_snapshot(Debugger *debugger, Arena *arena)
{
	if (!debugger_has_cartridge(debugger)) return (ByteSpan) { 0 };
	DebuggerRuntimeSnapshot *snapshot = arena_push(arena, sizeof(*snapshot));
	debugger_capture_runtime_snapshot(debugger, snapshot);
	return byte_span(snapshot, sizeof(*snapshot));
}

b32 debugger_restore_snapshot(Debugger *debugger, ByteSpan snapshot)
{
	if (!snapshot.data || snapshot.size != sizeof(DebuggerRuntimeSnapshot)) return false;
	b32 success = debugger_restore_runtime_snapshot(debugger, snapshot.data);
	if (success) debugger_discard_scheduler_trace(debugger);
	return success;
}

b32 debugger_save_state(Debugger *debugger, Arena *arena)
{
	if (!debugger_has_cartridge(debugger)) return false;
	ByteSpan state = nes_emulator_save_state(debugger->emulator, arena);
	return state.data && state.size;
}

b32 debugger_restore_state(Debugger *debugger, ByteSpan state)
{
	b32 success = nes_emulator_load_state(debugger->emulator, state);
	if (success)
	{
		debugger->warned_missing_cartridge = false;
		debugger_discard_scheduler_trace(debugger);
		execution_graph_reset(&debugger->execution_graph);
		program_reset(debugger);
		debugger_clear_snapshots(debugger);
		debugger_capture_frame(debugger);
	}
	// Todo, remove diagnistic printing from the debugger instead return an error code or something,
	// otherwise tests print stuff
	else LOG_WARN("failed to restore state: no supported cartridge was instantiated");
	return success;
}

void debugger_set_input(Debugger *debugger, NES_Input input, u32 player)
{
	nes_emulator_set_input(debugger->emulator, player, input);
}

u64 debugger_scheduler_clock(const Debugger *debugger)
{
	return nes_emulator_scheduler_clock(debugger->emulator);
}

u32 debugger_step(Debugger *debugger)
{
	if (!debugger_has_cartridge(debugger)) return 0;
	debugger_prepare_execution(debugger);
	if (debugger->program_breakpoint_count) debugger_capture_runtime_snapshot(debugger, &debugger->breakpoint_snapshot);
	u32 cycles = nes_emulator_step(debugger->emulator);
	debugger_process_scheduler_trace(debugger);
	return cycles;
}

void debugger_run(Debugger *debugger, u64 ppu_cycles)
{
	//  Todo, user messages are not part of debugger
	if (!debugger_has_cartridge(debugger))
	{
		if (ppu_cycles && !debugger->warned_missing_cartridge)
		{
			LOG_WARN("emulation is paused because no cartridge is loaded");
			debugger->warned_missing_cartridge = true;
		}
		return;
	}

	if (ppu_cycles)
	{
		debugger_prepare_execution(debugger);
		if (debugger->program_breakpoint_count) debugger_capture_runtime_snapshot(debugger, &debugger->breakpoint_snapshot);
	}
	nes_emulator_run(debugger->emulator, ppu_cycles);
	debugger_process_scheduler_trace(debugger);
}

u64 debugger_run_samples(Debugger *debugger, u64 minimum_samples, f32 *samples, u64 capacity)
{
	if (!debugger_has_cartridge(debugger)) return 0;
	debugger_prepare_execution(debugger);
	if (debugger->program_breakpoint_count) debugger_capture_runtime_snapshot(debugger, &debugger->breakpoint_snapshot);
	if (!debugger->program_breakpoint_count)
	{
		u64 count = nes_emulator_run_samples(debugger->emulator, minimum_samples, samples, capacity);
		debugger_process_scheduler_trace(debugger);
		return count;
	}

	// Note, we have to cap this before we might request so many samples that discard records ...
	u64 total = 0;
	while (total < minimum_samples)
	{
		u64 request = Min(minimum_samples - total, 128);
		u64 count = nes_emulator_run_samples(debugger->emulator, request, samples + total, capacity - total);
		debugger_process_scheduler_trace(debugger);
		if (debugger->breakpoint_hit) return 0;
		total += count;
	}
	return total;
}

static b32 debugger_restore_frame_snapshot(Debugger *debugger, u64 index)
{
	u64 oldest = debugger->snapshot_count > DEBUGGER_SNAPSHOT_CAPACITY ? debugger->snapshot_count - DEBUGGER_SNAPSHOT_CAPACITY : 0;
	if (index < oldest || index >= debugger->snapshot_count) return false;
	if (!debugger_restore_runtime_snapshot(debugger, &debugger->snapshots[index & DEBUGGER_SNAPSHOT_MASK])) return false;
	debugger_discard_scheduler_trace(debugger);
	debugger->snapshot_cursor = index;
	return true;
}

void debugger_capture_frame(Debugger *debugger)
{
	if (!debugger_has_cartridge(debugger)) return;
	if (debugger->snapshot_count)
	{
		DebuggerRuntimeSnapshot *current = &debugger->snapshots[debugger->snapshot_cursor & DEBUGGER_SNAPSHOT_MASK];
		if (current->scheduler_clock == debugger_scheduler_clock(debugger)) return;
	}
	if (!debugger->snapshots) {
		debugger->snapshots = arena_push(debugger->arena, sizeof(*debugger->snapshots) * DEBUGGER_SNAPSHOT_CAPACITY);
	}
	if (debugger->snapshot_count && debugger->snapshot_cursor + 1 < debugger->snapshot_count) {
		debugger->snapshot_count = debugger->snapshot_cursor + 1;
	}
	u64 index = debugger->snapshot_count++;
	debugger_capture_runtime_snapshot(debugger, &debugger->snapshots[index & DEBUGGER_SNAPSHOT_MASK]);
	debugger->snapshot_cursor = index;
}

b32 debugger_undo_frame(Debugger *debugger)
{
	if (!debugger->snapshot_count) return false;
	u64 oldest = debugger->snapshot_count > DEBUGGER_SNAPSHOT_CAPACITY ? debugger->snapshot_count - DEBUGGER_SNAPSHOT_CAPACITY : 0;
	if (debugger->snapshot_cursor <= oldest) return false;
	return debugger_restore_frame_snapshot(debugger, debugger->snapshot_cursor - 1);
}

b32 debugger_redo_frame(Debugger *debugger)
{
	if (!debugger->snapshot_count || debugger->snapshot_cursor + 1 >= debugger->snapshot_count) return false;
	return debugger_restore_frame_snapshot(debugger, debugger->snapshot_cursor + 1);
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

void debugger_refine(Debugger *debugger, u32 instruction_budget)
{
	program_refine(debugger, instruction_budget);
}

DebuggerState debugger_capture_state(const Debugger *debugger)
{
	return (DebuggerState) {
		.cpu = nes_emulator_cpu_state(debugger->emulator),
		.ppu = nes_emulator_ppu_state(debugger->emulator),
		.apu = nes_emulator_apu_state(debugger->emulator),
	};
}

void debugger_capture_video(const Debugger *debugger, u8 *pixels, u32 stride)
{
	Assert(pixels);
	Assert(stride >= NES_VIDEO_WIDTH);
	NES_VideoFrame video = nes_emulator_video_frame(debugger->emulator);
	for (u32 y = 0; y < video.height; ++y) {
		memory_copy(pixels + y * stride, video.pixels + y * video.stride, video.width);
	}
}

void debugger_capture_chr_map(const Debugger *debugger, NES_CHRMap *map)
{
	Assert(map);
	nes_emulator_capture_chr_map(debugger->emulator, map);
}

u32 debugger_prg_rom_size(const Debugger *debugger)
{
	return nes_emulator_prg_rom_size(debugger->emulator);
}

const Program *debugger_program(const Debugger *debugger)
{
	return &debugger->program;
}

const ExecutionGraph *debugger_execution_graph(const Debugger *debugger)
{
	return &debugger->execution_graph;
}
