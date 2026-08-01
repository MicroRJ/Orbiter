#include "debugger.h"
#include "os.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static String headless_read_file(Arena *arena, const char *path)
{
	String result = {};
	Platform_File file = platform_access_file(path, PLATFORM_FILE_OPEN_EXISTING, PLATFORM_FILE_READ | PLATFORM_FILE_SHARE_READ);
	if (!platform_file_is_valid(file)) return result;
	u64 size = 0;
	if (platform_get_file_size(file, &size) && size <= MAX_VALUE_U32)
	{
		u8 *data = arena_push(arena, size + 1);
		u64 bytes_read = 0;
		if (platform_read_file(file, data, size, &bytes_read) && bytes_read == size)
		{
			data[size] = 0;
			result = string_from_data((char *)data, (u32)size);
		}
	}
	platform_close_file(file);
	return result;
}

static b32 headless_write_file(const char *path, const void *data, u32 size)
{
	Platform_File file = platform_access_file(path, PLATFORM_FILE_CREATE_ALWAYS, PLATFORM_FILE_WRITE);
	if (!platform_file_is_valid(file)) return false;
	u64 bytes_written = 0;
	b32 result = platform_write_file(file, data, size, &bytes_written) && bytes_written == size;
	platform_close_file(file);
	return result;
}

static void print_usage(const char *executable)
{
	fprintf(stderr, "usage: %s <rom.nes> [frames] [state] [--determinism]\n", executable);
}

static b32 parse_frame_count(const char *text, u32 *frame_count)
{
	char *end = 0;
	errno = 0;
	unsigned long value = strtoul(text, &end, 10);
	if (errno || end == text || *end || !value || value > MAX_VALUE_U32) {
		return false;
	}
	*frame_count = (u32)value;
	return true;
}

static ByteSpan capture_state(Debugger *debugger, Arena *arena)
{
	u8 *start = arena_top(arena);
	if (!debugger_save_state(debugger, arena)) {
		return (ByteSpan) {};
	}
	return byte_span(start, (u64)((u8 *)arena_top(arena) - start));
}

static b32 check_determinism(Debugger *debugger, Arena *arena, u32 frame)
{
	ARENA_SCOPE(arena)
	{
		u64 sample_capacity = nes_required_sample_capacity();
		f32 *expected_samples = arena_push(arena, sizeof(*expected_samples) * sample_capacity);
		f32 *replayed_samples = arena_push(arena, sizeof(*replayed_samples) * sample_capacity);
		debugger_capture_snapshot(debugger);
		u64 snapshot_clock = debugger_scheduler_clock(debugger);
		NES_RunFrameResult expected_frame = debugger_run_frame(debugger, expected_samples, sample_capacity);
		u64 expected_clock = debugger_scheduler_clock(debugger);
		ByteSpan expected = capture_state(debugger, arena);
		if (!expected.data) return false;

		if (!debugger_undo_snapshot(debugger) || debugger_scheduler_clock(debugger) != snapshot_clock)
		{
			LOG_ERROR("snapshot undo mismatch at frame %u: expected clock %llu, got %llu",
				frame, snapshot_clock, debugger_scheduler_clock(debugger));
			return false;
		}

		// Undo consumes the execution-origin snapshot. Capture the restored
		// state again before replaying, just as the application does before
		// every run.
		debugger_capture_snapshot(debugger);
		NES_RunFrameResult replayed_frame = debugger_run_frame(debugger, replayed_samples, sample_capacity);
		u64 replayed_clock = debugger_scheduler_clock(debugger);
		ByteSpan replayed = capture_state(debugger, arena);
		if (!replayed.data) return false;

		if (expected_frame.samples != replayed_frame.samples ||
			expected_frame.steps != replayed_frame.steps ||
			expected_clock != replayed_clock ||
			expected.size != replayed.size ||
			!memory_match(expected_samples, replayed_samples,
				expected_frame.samples * sizeof(*expected_samples)) ||
			!memory_match(expected.data, replayed.data, expected.size))
		{
			DebuggerState state = debugger_capture_state(debugger);
			LOG_ERROR("determinism mismatch at frame %u: samples %llu/%llu, steps %llu/%llu, replay PC $%04X, PPU %u,%u",
				frame, expected_frame.samples, replayed_frame.samples, expected_frame.steps, replayed_frame.steps,
				state.cpu.PC, state.ppu.xtick, state.ppu.ytick);
			headless_write_file(
				"determinism_expected.dump", expected.data, expected.size);
			headless_write_file(
				"determinism_replayed.dump", replayed.data, replayed.size);
			return false;
		}
		if (!debugger_undo_snapshot(debugger) ||
			debugger_scheduler_clock(debugger) != snapshot_clock)
		{
			LOG_ERROR("replayed snapshot undo mismatch at frame %u", frame);
			return false;
		}
		debugger_capture_snapshot(debugger);
	}
	return true;
}

int main(int argc, char **argv)
{
	b32 check_replay =
		argc >= 3 && strcmp(argv[argc - 1], "--determinism") == 0;
	int positional_argc = argc - check_replay;
	if (positional_argc < 2 || positional_argc > 4)
	{
		print_usage(argv[0]);
		return 2;
	}
	u32 frame_count = 600;
	if (argc > positional_argc)
	{
		Assert(check_replay);
	}
	if (check_replay) logger_set_level(LOG_LEVEL_INFO);
	if (positional_argc >= 3 &&
		!parse_frame_count(argv[2], &frame_count))
	{
		fprintf(stderr, "invalid frame count: '%s'\n", argv[2]);
		return 2;
	}
	if (!os_init())
	{
		fprintf(stderr, "failed to initialize OS layer\n");
		return 1;
	}

	int exit_code = 1;
	Arena arena = arena_create(0, "headless debugger arena");
	Debugger *debugger = debugger_create(&arena);
	String rom = headless_read_file(&arena, argv[1]);
	if (!rom.text || !rom.size)
	{
		LOG_ERROR("could not read ROM '%s'", argv[1]);
		goto done;
	}
	if (!debugger_open_rom(debugger, byte_span((void *)rom.text, rom.size)))
	{
		LOG_ERROR("could not load ROM '%s'", argv[1]);
		goto done;
	}
	if (positional_argc >= 4)
	{
		String state = headless_read_file(&arena, argv[3]);
		if (!state.text || !state.size || !debugger_restore_state(debugger, byte_span((void *)state.text, state.size)))
		{
			LOG_ERROR("could not restore state '%s'", argv[3]);
			goto done;
		}
	}
	u64 sample_capacity = nes_required_sample_capacity();
	f32 *samples = arena_push(&arena, sizeof(*samples) * sample_capacity);
	if (check_replay)
	{
		u16 breakpoint_pc = debugger_capture_state(debugger).cpu.PC;
		u64 breakpoint_clock = debugger_scheduler_clock(debugger);
		NES_MapAddr breakpoint = debugger_cpu_map(debugger, breakpoint_pc);
		debugger_set_program_breakpoint(debugger, breakpoint, true);
		debugger_capture_snapshot(debugger);
		NES_RunFrameResult breakpoint_frame = debugger_run_frame(debugger, samples, sample_capacity);
		if (breakpoint_frame.samples ||
			!debugger_breakpoint_hit(debugger) || debugger_scheduler_clock(debugger) != breakpoint_clock ||
			debugger_capture_state(debugger).cpu.PC != breakpoint_pc)
		{
			LOG_ERROR("snapshot breakpoint replay failed at CPU $%04X", breakpoint_pc);
			goto done;
		}
		debugger_set_program_breakpoint(debugger, breakpoint, false);
		debugger_capture_snapshot(debugger);
	}

	for (u32 frame = 0; frame < frame_count; ++frame)
	{
		if (check_replay)
		{
			if (!check_determinism(debugger, &arena, frame)) goto done;
			debugger_capture_snapshot(debugger);
			debugger_run_frame(debugger, samples, sample_capacity);
			continue;
		}
		debugger_update_cpu_mapping(debugger);
		debugger_capture_snapshot(debugger);
		debugger_run_frame(debugger, samples, sample_capacity);
		const Program *program = debugger_program(debugger);
		u32 refinement_budget = program->refinement_pass_count < 2 ? 2048 : 128;
		debugger_run_program_crawler(debugger, refinement_budget);

		u16 pc = debugger_capture_state(debugger).cpu.PC;
		u32 instruction_index = 0;
		if (!program_index_from_cpu_address(debugger, pc, &instruction_index))
		{
			NES_MapAddr mapped = debugger_cpu_map(debugger, pc);
			u32 owner = program_mapped_instruction_offset(program, mapped);
			LOG_ERROR("frame %u: CPU PC $%04X could not be found (device %u offset $%05X, owner $%05X, refinement lap %llu cursor $%04X, program revision %llu)", frame, pc, mapped.device, mapped.address, owner, program->refinement_pass_count, program->refinement_cpu_cursor, program->revision);
			goto done;
		}
	}

	const Program *program = debugger_program(debugger);
	if (check_replay) {
		LOG_INFO("deterministic replay passed for '%s' across %u frames", argv[1], frame_count);
	} else {
		LOG_INFO("executed '%s' for %u frames; PC $%04X, %llu instructions observed, refinement lap %llu", argv[1], frame_count, debugger_capture_state(debugger).cpu.PC, program->executed_instruction_count, program->refinement_pass_count);
	}
	exit_code = 0;

done:
	os_shutdown();
	return exit_code;
}
