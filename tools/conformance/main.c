#include "base.h"
#include "nes/emulator.h"
#include "orb.h"
#include "os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	BLARGG_STATUS_ADDRESS    = 0x6000,
	BLARGG_SIGNATURE_ADDRESS = 0x6001,
	BLARGG_OUTPUT_ADDRESS    = 0x6004,
	BLARGG_OUTPUT_END        = 0x8000,
	BLARGG_STATUS_RUNNING    = 0x80,
	BLARGG_STATUS_RESET      = 0x81,
	BLARGG_DEFAULT_TIMEOUT_SECONDS = 60,
	BLARGG_RESET_DELAY_MILLISECONDS = 100,
	CONFORMANCE_PATH_CAPACITY = 4096,
};

typedef enum
{
	CONFORMANCE_RESULT_PASS,
	CONFORMANCE_RESULT_FAIL,
	CONFORMANCE_RESULT_TIMEOUT,
	CONFORMANCE_RESULT_LOAD_ERROR,
	CONFORMANCE_RESULT_NO_PROTOCOL,
}
Conformance_ResultKind;

typedef struct
{
	Conformance_ResultKind kind;
	u64 cpu_cycles;
	u32 reset_count;
	u8 status;
}
Conformance_Result;

typedef struct
{
	u32 total;
	u32 failures;
	u32 errors;
}
Conformance_Summary;

static b32 conformance_setup_emulator(NES_Emulator *emulator, const NES_Game *game)
{
	return nes_setup_emulator(emulator, *game);
}

static b32 conformance_has_blargg_signature(NES_Emulator *emulator)
{
	static const u8 signature[] = { 0xDE, 0xB0, 0x61 };
	for (u32 index = 0; index < ArrayCount(signature); index ++) {
		if (nes_emulator_cpu_peek(emulator, BLARGG_SIGNATURE_ADDRESS + index) != signature[index]) return false;
	}
	return true;
}

static void conformance_print_blargg_output(NES_Emulator *emulator)
{
	b32 wrote_anything = false;
	for (u32 address = BLARGG_OUTPUT_ADDRESS; address < BLARGG_OUTPUT_END; address ++)
	{
		u8 value = nes_emulator_cpu_peek(emulator, (u16)address);
		if (!value) break;
		if (value == '\r' || value == '\n' || value == '\t' || (value >= 32 && value < 127)) {
			fputc(value, stdout);
		}
		else {
			fprintf(stdout, "\\x%02X", value);
		}
		wrote_anything = true;
	}
	if (wrote_anything) {
		fputc('\n', stdout);
		fflush(stdout);
	}
}

static Conformance_Result conformance_run_blargg(NES_Emulator *emulator, u64 timeout_cycles)
{
	Conformance_Result result = { .kind = CONFORMANCE_RESULT_TIMEOUT };
	u64 reset_delay_cycles = ((u64)NES_CPU_HZ * BLARGG_RESET_DELAY_MILLISECONDS + 999) / 1000;
	u64 reset_at_cycle = 0;
	b32 protocol_seen = false;
	b32 running_seen = false;
	b32 reset_pending = false;
	b32 wait_for_reset_acknowledgement = false;

	while (result.cpu_cycles < timeout_cycles)
	{
		result.cpu_cycles += nes_emulator_step(emulator, 0);
		b32 has_signature = conformance_has_blargg_signature(emulator);
		if (has_signature) protocol_seen = true;
		if (!has_signature) continue;

		u8 status = nes_emulator_cpu_peek(emulator, BLARGG_STATUS_ADDRESS);
		result.status = status;
		if (status == BLARGG_STATUS_RUNNING) running_seen = true;
		if (wait_for_reset_acknowledgement && status != BLARGG_STATUS_RESET) wait_for_reset_acknowledgement = false;

		if (reset_pending && result.cpu_cycles >= reset_at_cycle)
		{
			nes_reset_emulator(emulator);
			result.reset_count ++;
			reset_pending = false;
			wait_for_reset_acknowledgement = true;
			continue;
		}

		if (status == BLARGG_STATUS_RESET)
		{
			if (!reset_pending && !wait_for_reset_acknowledgement)
			{
				reset_at_cycle = result.cpu_cycles + reset_delay_cycles;
				reset_pending = true;
			}
			continue;
		}

		if (status == BLARGG_STATUS_RUNNING || !running_seen) continue;
		result.kind = status == 0 ? CONFORMANCE_RESULT_PASS : CONFORMANCE_RESULT_FAIL;
		return result;
	}

	if (!protocol_seen) result.kind = CONFORMANCE_RESULT_NO_PROTOCOL;
	return result;
}

static const char *conformance_result_name(Conformance_ResultKind kind)
{
	switch (kind)
	{
		case CONFORMANCE_RESULT_PASS:        return "PASS";
		case CONFORMANCE_RESULT_FAIL:        return "FAIL";
		case CONFORMANCE_RESULT_TIMEOUT:     return "TIMEOUT";
		case CONFORMANCE_RESULT_LOAD_ERROR:  return "LOAD";
		case CONFORMANCE_RESULT_NO_PROTOCOL: return "PROTOCOL";
	}
	return "UNKNOWN";
}

static Conformance_Result conformance_run_path(Arena *game_arena, NES_Emulator *emulator, const char *path, u64 timeout_cycles)
{
	arena_reset(game_arena);
	NES_Game *game = orb_game_from_ines_file(game_arena, str_from_cstr(path), 0);
	if (!game || !conformance_setup_emulator(emulator, game)) return (Conformance_Result) { .kind = CONFORMANCE_RESULT_LOAD_ERROR };
	return conformance_run_blargg(emulator, timeout_cycles);
}

static void conformance_print_usage(const char *executable)
{
	fprintf(stderr, "usage: %s [--timeout-seconds N] [--rom-root <directory>] <test.nes> [test.nes ...]\n", executable);
}

static void conformance_run_and_report(Arena *game_arena, NES_Emulator *emulator, const char *path, u64 timeout_cycles, Conformance_Summary *summary)
{
	fprintf(stdout, "[ RUN      ] %s\n", path);
	Conformance_Result result = conformance_run_path(game_arena, emulator, path, timeout_cycles);
	fprintf(stdout, "[ %-8s ] %s (status $%02X, %.3f emulated seconds, %u resets)\n", conformance_result_name(result.kind), path, result.status, (f64)result.cpu_cycles / NES_CPU_HZ, result.reset_count);
	if (result.kind != CONFORMANCE_RESULT_LOAD_ERROR) conformance_print_blargg_output(emulator);
	summary->total ++;
	if (result.kind != CONFORMANCE_RESULT_PASS) summary->failures ++;
	if (result.kind == CONFORMANCE_RESULT_LOAD_ERROR) summary->errors ++;
}

static b32 conformance_path_is_absolute(const char *path)
{
	return path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':');
}

static b32 conformance_resolve_path(char *path, u32 capacity, const char *rom_root, const char *input_path)
{
	if (!rom_root || !rom_root[0] || conformance_path_is_absolute(input_path))
	{
		i32 written = snprintf(path, capacity, "%s", input_path);
		return written >= 0 && (u32)written < capacity;
	}
	u64 root_size = strlen(rom_root);
	b32 needs_separator = rom_root[root_size - 1] != '/' && rom_root[root_size - 1] != '\\';
	i32 written = needs_separator ? snprintf(path, capacity, "%s\\%s", rom_root, input_path) : snprintf(path, capacity, "%s%s", rom_root, input_path);
	return written >= 0 && (u32)written < capacity;
}

int main(int argc, char **argv)
{
	u32 timeout_seconds = BLARGG_DEFAULT_TIMEOUT_SECONDS;
	const char *rom_root = getenv("ORBITER_NES_TEST_ROM_ROOT");
	i32 first_path = 1;
	while (first_path < argc && argv[first_path][0] == '-')
	{
		if (strcmp(argv[first_path], "--timeout-seconds") == 0)
		{
			if (++ first_path >= argc || !str_to_u32(str_from_cstr(argv[first_path]), &timeout_seconds) || !timeout_seconds)
			{
				fprintf(stderr, "invalid or missing timeout\n");
				return 2;
			}
		}
		else if (strcmp(argv[first_path], "--rom-root") == 0)
		{
			if (++ first_path >= argc)
			{
				fprintf(stderr, "missing ROM root\n");
				return 2;
			}
			rom_root = argv[first_path];
		}
		else
		{
			fprintf(stderr, "unknown option: '%s'\n", argv[first_path]);
			conformance_print_usage(argv[0]);
			return 2;
		}
		first_path ++;
	}
	if (first_path >= argc)
	{
		conformance_print_usage(argv[0]);
		return 2;
	}
	if (!os_init())
	{
		fprintf(stderr, "failed to initialize OS layer\n");
		return 2;
	}

	Arena arena = arena_create(0, "NES conformance runner");
	Arena game_arena = arena_create(0, "NES conformance game");
	NES_Emulator *emulator = arena_push_zero(&arena, sizeof(*emulator));
	u64 timeout_cycles = (u64)timeout_seconds * NES_CPU_HZ;
	Conformance_Summary summary = {};
	b32 invocation_succeeded = true;
	for (i32 index = first_path; index < argc; index ++)
	{
		char path[CONFORMANCE_PATH_CAPACITY];
		if (!conformance_resolve_path(path, sizeof(path), rom_root, argv[index]))
		{
			fprintf(stderr, "ROM path is too long: '%s'\n", argv[index]);
			invocation_succeeded = false;
			break;
		}
		conformance_run_and_report(&game_arena, emulator, path, timeout_cycles, &summary);
	}
	if (invocation_succeeded && !summary.total)
	{
		fprintf(stderr, "conformance suite is empty\n");
		invocation_succeeded = false;
	}

	fprintf(stdout, "%u passed, %u failed\n", summary.total - summary.failures, summary.failures);
	arena_destroy(&game_arena);
	arena_destroy(&arena);
	os_shutdown();
	if (!invocation_succeeded || summary.errors) return 2;
	return summary.failures ? 1 : 0;
}
