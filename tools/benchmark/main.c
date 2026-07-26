#include "base.h"
#include "nes/cartridge.h"
#include "nes/emulator.h"
#include "nes/src/emulator_internal.h"
#include "nes/src/ppu/ppu.h"
#include "os.h"

#include <errno.h>
#include <stdlib.h>

static String benchmark_read_file(Arena *arena, const char *path)
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

enum
{
	BENCHMARK_DEFAULT_ITERATIONS = 300,
	BENCHMARK_MAX_ITERATIONS = 10000,
	BENCHMARK_WARMUP_FRAMES = 120,
};

typedef struct
{
	f64 minimum;
	f64 median;
	f64 mean;
	f64 p95;
}
BenchmarkStats;

typedef struct
{
	u8 values[32];
	NES_InputState input_state;
	u32 cpu_stall_cycles;
	u64 audio_sample_phase;
	NES_CPUState cpu;
	NES_PPUState ppu;
	NES_APUState apu;
	u8 controllers[2];
	u8 wram[NES_WRAM_SIZE];
	u8 vram[NES_VRAM_SIZE];
	u8 chr_ram[KiB(8)];
	u8 prg_ram[KiB(8)];
	u8 video[NES_VIDEO_HEIGHT][NES_VIDEO_WIDTH];
}
BenchmarkRuntimeSnapshot;

static volatile u64 benchmark_snapshot_sink;
static volatile u64 benchmark_boundary_sink;

static void benchmark_capture_runtime_snapshot(BenchmarkRuntimeSnapshot *snapshot, const NES_Emulator *core)
{
	const NES_State *state = &core->core;
	memory_copy(snapshot->values, state->values, sizeof(snapshot->values));
	snapshot->input_state = state->input_state;
	snapshot->cpu_stall_cycles = state->cpu_stall_cycles;
	snapshot->audio_sample_phase = state->audio_sample_phase;
	snapshot->cpu = state->cpu;
	snapshot->ppu = state->ppu;
	snapshot->apu = state->apu;
	memory_copy(snapshot->controllers, state->controllers, sizeof(snapshot->controllers));
	memory_copy(snapshot->wram, state->_wram, sizeof(snapshot->wram));
	memory_copy(snapshot->vram, state->_vram, sizeof(snapshot->vram));
	memory_copy(snapshot->chr_ram, state->chr_ram, sizeof(snapshot->chr_ram));
	memory_copy(snapshot->prg_ram, state->prg_ram, sizeof(snapshot->prg_ram));
	memory_copy(snapshot->video, core->video, sizeof(snapshot->video));
}

static int benchmark_compare_f64(const void *left, const void *right)
{
	f64 a = *(const f64 *)left;
	f64 b = *(const f64 *)right;
	return (a > b) - (a < b);
}

static BenchmarkStats benchmark_stats(f64 *samples, u32 count)
{
	Assert(count);
	qsort(samples, count, sizeof(*samples), benchmark_compare_f64);
	f64 total = 0;
	for (u32 index = 0; index < count; ++index) {
		total += samples[index];
	}
	return (BenchmarkStats) {
		.minimum = samples[0],
		.median = samples[count / 2],
		.mean = total / count,
		.p95 = samples[(count - 1) * 95 / 100],
	};
}

static void benchmark_print(const char *name, BenchmarkStats stats)
{
	printf("%-24s min %8.3f ms  median %8.3f ms  mean %8.3f ms  p95 %8.3f ms\n",
		name, stats.minimum * 1000.0, stats.median * 1000.0,
		stats.mean * 1000.0, stats.p95 * 1000.0);
}

static b32 benchmark_parse_iterations(const char *text, u32 *result)
{
	char *end = 0;
	errno = 0;
	unsigned long value = strtoul(text, &end, 10);
	if (errno || end == text || *end || !value || value > BENCHMARK_MAX_ITERATIONS) {
		return false;
	}
	*result = (u32)value;
	return true;
}

static void benchmark_scan_execution_history(NES_InstructionBoundarySpan boundaries, NES_ExecutionMapping *history)
{
	for (u32 index = 0; index < boundaries.count; ++index)
	{
		NES_InstructionBoundary boundary = boundaries.items[index];
		history[index] = (NES_ExecutionMapping) {
			.cpu_address = boundary.cpu_address,
			.destination = boundary.program_address,
		};
	}
	benchmark_boundary_sink += history[boundaries.count - 1].cpu_address;
}

static void benchmark_scan_breakpoints(NES_InstructionBoundarySpan boundaries, const NES_MapAddr *breakpoints, u32 breakpoint_count)
{
	u64 matches = 0;
	for (u32 boundary_index = 0; boundary_index < boundaries.count; ++boundary_index)
	{
		NES_MapAddr address = boundaries.items[boundary_index].program_address;
		for (u32 breakpoint_index = 0; breakpoint_index < breakpoint_count; ++breakpoint_index) {
			matches += address.device == breakpoints[breakpoint_index].device && address.address == breakpoints[breakpoint_index].address;
		}
	}
	benchmark_boundary_sink += matches;
}

int main(int argc, char **argv)
{
	if (argc < 2 || argc > 3)
	{
		fprintf(stderr, "usage: %s <rom.nes> [iterations]\n", argv[0]);
		return 2;
	}

	u32 iterations = BENCHMARK_DEFAULT_ITERATIONS;
	if (argc == 3 && !benchmark_parse_iterations(argv[2], &iterations))
	{
		fprintf(stderr, "invalid iteration count: '%s'\n", argv[2]);
		return 2;
	}

	Assert(os_init());
	Arena arena = arena_create(0, "NES benchmark");
	String file = benchmark_read_file(&arena, argv[1]);
	NES_CartridgeDesc cartridge;
	if (!file.size || !nes_cartridge_parse_ines(byte_span((void *)file.text, file.size), &cartridge))
	{
		fprintf(stderr, "failed to load ROM '%s'\n", argv[1]);
		return 1;
	}

	NES_Emulator *core = nes_emulator_create(&arena, (NES_EmulatorDesc) { .audio_sample_rate = 48000 });
	NES_Emulator *boundary_core = nes_emulator_create(&arena, (NES_EmulatorDesc) {
		.audio_sample_rate = 48000,
		.enable_instruction_boundaries = true,
	});
	if (!nes_emulator_load_cartridge(core, cartridge))
	{
		fprintf(stderr, "unsupported ROM '%s'\n", argv[1]);
		return 1;
	}
	Assert(nes_emulator_load_cartridge(boundary_core, cartridge));

	for (u32 frame = 0; frame < BENCHMARK_WARMUP_FRAMES; ++frame)
	{
		nes_emulator_run(core, NES_PPU_FRAME_CYCLES);
		nes_emulator_run(boundary_core, NES_PPU_FRAME_CYCLES);
	}

	f64 *samples = arena_push(&arena, sizeof(*samples) * iterations);
	f64 *boundary_samples = arena_push(&arena, sizeof(*boundary_samples) * iterations);
	u64 full_snapshot_size = sizeof(core->core) + sizeof(core->video);
	u8 *full_snapshot = arena_push(&arena, full_snapshot_size);
	for (u32 iteration = 0; iteration < iterations; ++iteration)
	{
		Seconds begin = seconds_now();
		memory_copy(full_snapshot, &core->core, sizeof(core->core));
		memory_copy(full_snapshot + sizeof(core->core), core->video, sizeof(core->video));
		samples[iteration] = seconds_now().seconds - begin.seconds;
		benchmark_snapshot_sink += full_snapshot[iteration % full_snapshot_size];
	}
	benchmark_print("full snapshot copy", benchmark_stats(samples, iterations));
	printf("%-24s %.2f MiB copied\n", "", (f64)full_snapshot_size / MB(1));

	BenchmarkRuntimeSnapshot *runtime_snapshot = arena_push(&arena, sizeof(*runtime_snapshot));
	for (u32 iteration = 0; iteration < iterations; ++iteration)
	{
		Seconds begin = seconds_now();
		benchmark_capture_runtime_snapshot(runtime_snapshot, core);
		samples[iteration] = seconds_now().seconds - begin.seconds;
		benchmark_snapshot_sink += ((u8 *)runtime_snapshot)[iteration % sizeof(*runtime_snapshot)];
	}
	benchmark_print("runtime snapshot copy", benchmark_stats(samples, iterations));
	printf("%-24s %.2f KiB copied\n", "", (f64)sizeof(*runtime_snapshot) / KiB(1));

	for (u32 iteration = 0; iteration < iterations; ++iteration)
	{
		Seconds begin = seconds_now();
		nes_emulator_run(core, NES_PPU_FRAME_CYCLES);
		samples[iteration] = seconds_now().seconds - begin.seconds;

		begin = seconds_now();
		nes_emulator_run(boundary_core, NES_PPU_FRAME_CYCLES);
		boundary_samples[iteration] = seconds_now().seconds - begin.seconds;
	}
	BenchmarkStats frame_stats = benchmark_stats(samples, iterations);
	BenchmarkStats boundary_stats = benchmark_stats(boundary_samples, iterations);
	benchmark_print("frame, boundaries off", frame_stats);
	benchmark_print("frame, boundaries on", boundary_stats);
	printf("%-24s median %+8.3f ms  %+6.2f%%\n", "boundary recording cost",
		(boundary_stats.median - frame_stats.median) * 1000.0,
		frame_stats.median ? (boundary_stats.median / frame_stats.median - 1.0) * 100.0 : 0.0);
	printf("%-24s %u events in final frame\n", "", nes_emulator_instruction_boundaries(boundary_core).count);

	NES_InstructionBoundarySpan boundaries = nes_emulator_instruction_boundaries(boundary_core);
	NES_ExecutionMapping *history = arena_push(&arena, sizeof(*history) * boundaries.count);
	NES_MapAddr breakpoints[16] = {};
	for (u32 index = 0; index < ArrayCount(breakpoints); ++index) {
		breakpoints[index] = (NES_MapAddr) { NES_DEVICE_PRG_ROM, MAX_VALUE_U32 - index };
	}
	for (u32 iteration = 0; iteration < iterations; ++iteration)
	{
		Seconds begin = seconds_now();
		benchmark_scan_execution_history(boundaries, history);
		samples[iteration] = seconds_now().seconds - begin.seconds;
	}
	benchmark_print("boundary history scan", benchmark_stats(samples, iterations));
	for (u32 iteration = 0; iteration < iterations; ++iteration)
	{
		Seconds begin = seconds_now();
		benchmark_scan_breakpoints(boundaries, breakpoints, 1);
		samples[iteration] = seconds_now().seconds - begin.seconds;
	}
	benchmark_print("breakpoint scan, 1", benchmark_stats(samples, iterations));
	for (u32 iteration = 0; iteration < iterations; ++iteration)
	{
		Seconds begin = seconds_now();
		benchmark_scan_breakpoints(boundaries, breakpoints, ArrayCount(breakpoints));
		samples[iteration] = seconds_now().seconds - begin.seconds;
	}
	benchmark_print("breakpoint scan, 16", benchmark_stats(samples, iterations));

	for (u32 iteration = 0; iteration < iterations; ++iteration)
	{
		Seconds begin = seconds_now();
		for (u32 cycle = 0; cycle < NES_PPU_FRAME_CYCLES; ++cycle) {
			nes_ppu_step(core);
		}
		samples[iteration] = seconds_now().seconds - begin.seconds;
	}
	benchmark_print("PPU frame", benchmark_stats(samples, iterations));

	os_shutdown();
	return 0;
}
