#include "nes_process.h"
#include "nes_target.h"
#include "nes_serialize.h"
#include "os.h"

#include <string.h>

static Str headless_read_file(Arena *arena, const char *path)
{
	Str result = {};
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
			result = str_from_data((char *)data, (u32)size);
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
	u32 value = 0;
	if (!str_to_u32(str_from_cstr(text), &value) || !value) return false;
	*frame_count = value;
	return true;
}

static ByteSpan capture_state(NES_Emulator *emulator, Arena *arena)
{
	return orb_nes_state_encode(arena, emulator);
}

static b32 check_determinism(NES_Process *debugger, NES_Emulator *emulator, NES_TargetPublication *publication, Arena *arena, u32 frame)
{
	SCRATCH_SCOPE(arena)
	{
		u64 sample_capacity = nes_required_sample_capacity();
		f32 *expected_samples = arena_push(arena, sizeof(*expected_samples) * sample_capacity);
		f32 *replayed_samples = arena_push(arena, sizeof(*replayed_samples) * sample_capacity);
		nes_process_capture_snapshot(debugger);
		u64 snapshot_clock = nes_emulator_scheduler_clock(emulator);
		NES_RunFrameResult expected_frame = nes_process_run_frame(debugger, expected_samples, sample_capacity);
		u64 expected_clock = nes_emulator_scheduler_clock(emulator);
		ByteSpan expected = capture_state(emulator, arena);
		if (!expected.data) return false;

		if (!nes_process_rewind(debugger) || nes_emulator_scheduler_clock(emulator) != snapshot_clock)
		{
			LOG_ERROR("snapshot undo mismatch at frame %u: expected clock %llu, got %llu",
				frame, snapshot_clock, nes_emulator_scheduler_clock(emulator));
			return false;
		}

		// Undo consumes the execution-origin snapshot. Capture the restored
		// state again before replaying, just as the application does before
		// every run.
		nes_process_capture_snapshot(debugger);
		NES_RunFrameResult replayed_frame = nes_process_run_frame(debugger, replayed_samples, sample_capacity);
		u64 replayed_clock = nes_emulator_scheduler_clock(emulator);
		ByteSpan replayed = capture_state(emulator, arena);
		if (!replayed.data) return false;

		if (expected_frame.samples != replayed_frame.samples ||
			expected_frame.steps != replayed_frame.steps ||
			expected_clock != replayed_clock ||
			expected.size != replayed.size ||
			!memory_match(expected_samples, replayed_samples,
				expected_frame.samples * sizeof(*expected_samples)) ||
			!memory_match(expected.data, replayed.data, expected.size))
		{
			nes_target_publish(publication, emulator);
			LOG_ERROR("determinism mismatch at frame %u: samples %llu/%llu, steps %llu/%llu, replay PC $%04X, PPU %u,%u",
				frame, expected_frame.samples, replayed_frame.samples, expected_frame.steps, replayed_frame.steps,
				publication->cpu.PC, publication->ppu.xtick, publication->ppu.ytick);
			headless_write_file(
				"determinism_expected.dump", expected.data, expected.size);
			headless_write_file(
				"determinism_replayed.dump", replayed.data, replayed.size);
			return false;
		}
		if (!nes_process_rewind(debugger) ||
			nes_emulator_scheduler_clock(emulator) != snapshot_clock)
		{
			LOG_ERROR("replayed snapshot undo mismatch at frame %u", frame);
			return false;
		}
		nes_process_capture_snapshot(debugger);
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
	NES_Process *debugger = nes_process_create(&arena);
	NES_Emulator *emulator = &debugger->emulator;
	Program *program = arena_push_zero(&arena, sizeof(*program));
	NES_TargetPublication *publication = arena_push_zero(&arena, sizeof(*publication));
	Str rom = headless_read_file(&arena, argv[1]);
	if (!rom.text || !rom.size)
	{
		LOG_ERROR("could not read ROM '%s'", argv[1]);
		goto done;
	}
	NES_CartridgeDesc cartridge = {};
	if (!nes_cartridge_parse_ines(byte_span((void *)rom.text, rom.size), &cartridge))
	{
		LOG_ERROR("could not parse ROM '%s'", argv[1]);
		goto done;
	}
	NES_SetupParams setup = {
		.mapper = cartridge.mapper,
		.vmirror = cartridge.vmirror,
		.four_screen = cartridge.four_screen,
		.has_trainer = cartridge.has_trainer,
		.prg_rom = cartridge.prg_rom,
		.chr_rom = cartridge.chr_rom,
	};
	if (!nes_setup_emulator(emulator, setup))
	{
		LOG_ERROR("could not set up ROM '%s'", argv[1]);
		goto done;
	}
	if (positional_argc >= 4)
	{
		Str state = headless_read_file(&arena, argv[3]);
		if (!state.text || !state.size || !orb_transfer_save_state_no_chunk(emulator, byte_span((void *)state.text, state.size)))
		{
			LOG_ERROR("could not restore state '%s'", argv[3]);
			goto done;
		}
	}
	nes_process_reset(debugger);
	program_reset(program, debugger->program_rom_size, debugger->program_ram_size);
	u64 sample_capacity = nes_required_sample_capacity();
	f32 *samples = arena_push(&arena, sizeof(*samples) * sample_capacity);
	if (check_replay)
	{
		nes_target_publish(publication, emulator);
		u16 breakpoint_pc = publication->cpu.PC;
		u64 breakpoint_clock = nes_emulator_scheduler_clock(emulator);
		NES_MapAddr breakpoint = nes_emulator_cpu_map(emulator, breakpoint_pc);
		nes_process_set_breakpoint(debugger, breakpoint, true);
		nes_process_capture_snapshot(debugger);
		NES_RunFrameResult breakpoint_frame = nes_process_run_frame(debugger, samples, sample_capacity);
		nes_target_publish(publication, emulator);
		if (breakpoint_frame.samples ||
			!nes_process_hit_breakpoint(debugger) || nes_emulator_scheduler_clock(emulator) != breakpoint_clock ||
			publication->cpu.PC != breakpoint_pc)
		{
			LOG_ERROR("snapshot breakpoint replay failed at CPU $%04X", breakpoint_pc);
			goto done;
		}
		nes_process_set_breakpoint(debugger, breakpoint, false);
		nes_process_capture_snapshot(debugger);
	}

	for (u32 frame = 0; frame < frame_count; ++frame)
	{
		if (check_replay)
		{
			if (!check_determinism(debugger, emulator, publication, &arena, frame)) goto done;
			nes_process_capture_snapshot(debugger);
			nes_process_run_frame(debugger, samples, sample_capacity);
			continue;
		}
		nes_process_capture_snapshot(debugger);
		nes_process_run_frame(debugger, samples, sample_capacity);
		program_invalidate(program);
		program_rebuild(program, emulator, debugger->program_evidence);

		nes_target_publish(publication, emulator);
		u16 pc = publication->cpu.PC;
		u32 instruction_index = 0;
		if (!program_index_from_cpu_address(program, pc, &instruction_index))
		{
			NES_MapAddr mapped = nes_emulator_cpu_map(emulator, pc);
			LOG_ERROR("frame %u: CPU PC $%04X could not be found (device %u offset $%05X, rows %u)", frame, pc, mapped.device, mapped.address, program->row_count);
			goto done;
		}
	}

	if (check_replay) {
		LOG_INFO("deterministic replay passed for '%s' across %u frames", argv[1], frame_count);
	} else {
		LOG_INFO("executed '%s' for %u frames; PC $%04X, %u listed rows", argv[1], frame_count, publication->cpu.PC, program->row_count);
	}
	exit_code = 0;

done:
	os_shutdown();
	return exit_code;
}
