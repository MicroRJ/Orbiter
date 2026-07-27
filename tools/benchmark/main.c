#include "base.h"
#include "nes/cartridge.h"
#include "nes/emulator.h"
#include "nes/isa.h"
#include "nes/src/emulator_internal.h"
#include "nes/src/ppu/ppu.h"
#include "debugger/debugger.h"
#include "execution_activity.h"
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
	NES_SchedulerTraceView trace;
	u64 first;
}
BenchmarkTraceRange;

typedef struct
{
	u64 lookups;
	u64 key_misses;
	u64 collision_misses;
	u64 probes;
	u32 maximum_probe_count;
}
BenchmarkHashStats;

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

static void benchmark_print_delta(const char *name, BenchmarkStats total, BenchmarkStats baseline)
{
	printf("%-24s median %+8.3f ms\n", name, (total.median - baseline.median) * 1000.0);
}

static void benchmark_print_bus(const char *name, NES_BusMetrics metrics, u32 iterations, f64 elapsed)
{
	f64 accesses = (f64)(metrics.reads + metrics.writes);
	printf("%-24s %10.0f reads/frame  %8.2f writes/frame  %8.2f M access/s\n",
		name, (f64)metrics.reads / iterations, (f64)metrics.writes / iterations,
		accesses / elapsed / 1000000.0);
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

static void benchmark_scan_trace(BenchmarkTraceRange range)
{
	u64 sum = 0;
	NES_SchedulerTraceSpans spans = nes_scheduler_trace_spans_since(range.trace, range.first);
	Assert(!spans.dropped);
	for (u32 span_index = 0; span_index < ArrayCount(spans.spans); ++span_index)
	{
		NES_SchedulerTraceSpan span = spans.spans[span_index];
		const NES_SchedulerTraceEntry *end = span.entries + span.count;
		for (const NES_SchedulerTraceEntry *entry = span.entries; entry < end; ++entry) {
			sum += nes_scheduler_trace_decode(range.trace, *entry).cpu_address;
		}
	}
	benchmark_boundary_sink += sum;
}

static b32 benchmark_execution_storage_offset(const Program *program, NES_MapAddr mapped, u32 *offset)
{
	if (mapped.device == NES_DEVICE_PRG_ROM && mapped.offset < program->prg_rom_byte_count)
	{
		*offset = mapped.offset;
		return true;
	}
	if (mapped.device == NES_DEVICE_PRG_RAM && mapped.offset < program->prg_ram_byte_count)
	{
		*offset = program->prg_rom_byte_count + mapped.offset;
		return true;
	}
	return false;
}

static u64 benchmark_execution_edge_key(u32 source_offset, u32 destination_offset)
{
	return ((u64)source_offset << 32) | destination_offset;
}

static u32 benchmark_execution_hash(u64 key)
{
	key ^= key >> 30;
	key *= 0xBF58476D1CE4E5B9ull;
	key ^= key >> 27;
	key *= 0x94D049BB133111EBull;
	key ^= key >> 31;
	return (u32)key & (EXECUTION_GRAPH_HASH_CAPACITY - 1);
}

static BenchmarkHashStats benchmark_measure_execution_hash(BenchmarkTraceRange range, const Program *program, u64 *keys)
{
	BenchmarkHashStats result = {};
	for (u64 index = range.first + 1; index < range.trace.index; ++index)
	{
		NES_SchedulerBoundary source = nes_scheduler_trace_at(range.trace, index - 1);
		NES_SchedulerBoundary destination = nes_scheduler_trace_at(range.trace, index);
		if (!nes_instruction_links_to_next(source.cpu_address, source.cpu_byte, destination.cpu_address)) continue;
		u32 source_offset = 0;
		u32 destination_offset = 0;
		if (!benchmark_execution_storage_offset(program, source.cpu_mapped, &source_offset) ||
			!benchmark_execution_storage_offset(program, destination.cpu_mapped, &destination_offset)) {
			continue;
		}

		++result.lookups;
		u64 key = benchmark_execution_edge_key(source_offset, destination_offset);
		u64 stored_key = key + 1;
		u32 slot = benchmark_execution_hash(key);
		u32 probe_count = 0;
		for (; probe_count < EXECUTION_GRAPH_HASH_CAPACITY; ++probe_count)
		{
			++result.probes;
			if (!keys[slot])
			{
				keys[slot] = stored_key;
				++result.key_misses;
				break;
			}
			if (keys[slot] == stored_key) break;
			++result.collision_misses;
			slot = (slot + 1) & (EXECUTION_GRAPH_HASH_CAPACITY - 1);
		}
		result.maximum_probe_count = Max(result.maximum_probe_count, probe_count + 1);
	}
	return result;
}

static void benchmark_print_hash_stats(const char *name, BenchmarkHashStats stats)
{
	printf("%-24s %llu lookups, %llu new keys, %llu collision misses, %.3f probes/lookup, max %u\n",
		name, stats.lookups, stats.key_misses, stats.collision_misses,
		stats.lookups ? (f64)stats.probes / stats.lookups : 0.0, stats.maximum_probe_count);
}

static void benchmark_print_control_flow(BenchmarkTraceRange range)
{
	u32 counts[256] = {};
	u32 total = 0;
	NES_SchedulerTraceSpans spans = nes_scheduler_trace_spans_since(range.trace, range.first);
	Assert(!spans.dropped);
	u32 remaining = range.trace.index > range.first ? (u32)(range.trace.index - range.first - 1) : 0;
	for (u32 span_index = 0; span_index < ArrayCount(spans.spans); ++span_index)
	{
		NES_SchedulerTraceSpan span = spans.spans[span_index];
		u32 count = Min(span.count, remaining);
		const NES_SchedulerTraceEntry *end = span.entries + count;
		for (const NES_SchedulerTraceEntry *entry = span.entries; entry < end; ++entry)
		{
			u8 opcode = nes_scheduler_trace_decode(range.trace, *entry).cpu_byte;
			if (nes_instruction_is_control_flow(opcode))
			{
				++counts[opcode];
				++total;
			}
		}
		remaining -= count;
	}
	printf("%-24s %u instructions\n", "control flow", total);
	for (u32 opcode = 0; opcode < ArrayCount(counts); ++opcode) {
		if (counts[opcode]) printf("%-24s $%02X %-4s %u\n", "", opcode, nes_instruction_desc(opcode).name, counts[opcode]);
	}
}

static void benchmark_scan_program(Debugger *debugger, BenchmarkTraceRange range)
{
	NES_SchedulerTraceSpans spans = nes_scheduler_trace_spans_since(range.trace, range.first);
	Assert(!spans.dropped);
	for (u32 span_index = 0; span_index < ArrayCount(spans.spans); ++span_index)
	{
		NES_SchedulerTraceSpan span = spans.spans[span_index];
		const NES_SchedulerTraceEntry *end = span.entries + span.count;
		for (const NES_SchedulerTraceEntry *entry = span.entries; entry < end; ++entry) {
			program_observe_execution(debugger, nes_scheduler_trace_decode(range.trace, *entry));
		}
	}
}

static void benchmark_scan_program_graph(Debugger *debugger, ExecutionGraph *graph, ExecutionPathState *path, BenchmarkTraceRange range, const NES_MapAddr *breakpoints, u32 breakpoint_count)
{
	u64 matches = 0;
	const Program *program = debugger_program(debugger);
	NES_SchedulerTraceSpans spans = nes_scheduler_trace_spans_since(range.trace, range.first);
	Assert(!spans.dropped);
	for (u32 span_index = 0; span_index < ArrayCount(spans.spans); ++span_index)
	{
		NES_SchedulerTraceSpan span = spans.spans[span_index];
		const NES_SchedulerTraceEntry *end = span.entries + span.count;
		for (const NES_SchedulerTraceEntry *entry = span.entries; entry < end; ++entry)
		{
			NES_SchedulerBoundary boundary = nes_scheduler_trace_decode(range.trace, *entry);
			program_observe_execution(debugger, boundary);
			execution_graph_observe_execution(graph, path, program, boundary);
			for (u32 breakpoint_index = 0; breakpoint_index < breakpoint_count; ++breakpoint_index) {
				matches += boundary.cpu_mapped.device == breakpoints[breakpoint_index].device && boundary.cpu_mapped.address == breakpoints[breakpoint_index].address;
			}
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
	if (!nes_emulator_load_cartridge(core, cartridge))
	{
		fprintf(stderr, "unsupported ROM '%s'\n", argv[1]);
		return 1;
	}

	for (u32 frame = 0; frame < BENCHMARK_WARMUP_FRAMES; ++frame) {
		nes_emulator_run(core, NES_PPU_FRAME_CYCLES);
	}

	f64 *samples = arena_push(&arena, sizeof(*samples) * iterations);
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

	NES_BusMetrics cpu_bus_begin = core->cpu_bus_metrics;
	NES_BusMetrics ppu_bus_begin = core->ppu_bus_metrics;
	f64 frame_elapsed = 0;
	for (u32 iteration = 0; iteration < iterations; ++iteration)
	{
		Seconds begin = seconds_now();
		nes_emulator_run(core, NES_PPU_FRAME_CYCLES);
		samples[iteration] = seconds_now().seconds - begin.seconds;
		frame_elapsed += samples[iteration];
	}
	BenchmarkStats frame_stats = benchmark_stats(samples, iterations);
	benchmark_print("emulator frame", frame_stats);
	NES_BusMetrics cpu_bus = {
		.reads = core->cpu_bus_metrics.reads - cpu_bus_begin.reads,
		.writes = core->cpu_bus_metrics.writes - cpu_bus_begin.writes,
	};
	NES_BusMetrics ppu_bus = {
		.reads = core->ppu_bus_metrics.reads - ppu_bus_begin.reads,
		.writes = core->ppu_bus_metrics.writes - ppu_bus_begin.writes,
	};
	benchmark_print_bus("CPU bus", cpu_bus, iterations, frame_elapsed);
	benchmark_print_bus("PPU bus", ppu_bus, iterations, frame_elapsed);

	u64 trace_first = nes_emulator_scheduler_trace(core).index;
	nes_emulator_run(core, NES_PPU_FRAME_CYCLES);
	NES_SchedulerTraceView trace = nes_emulator_scheduler_trace(core);
	Assert(nes_scheduler_trace_first_since(trace, trace_first) == trace_first);
	BenchmarkTraceRange range = { .trace = trace, .first = trace_first };
	u32 trace_count = (u32)(trace.index - trace_first);
	printf("%-24s %u events from one frame\n", "scheduler trace", trace_count);
	benchmark_print_control_flow(range);

	Debugger *analysis_debugger = debugger_create(&arena, 48000);
	Assert(debugger_open_rom(analysis_debugger, byte_span((void *)file.text, file.size)));
	u64 *execution_hash_keys = arena_push_zero(&arena, sizeof(*execution_hash_keys) * EXECUTION_GRAPH_HASH_CAPACITY);
	BenchmarkHashStats cold_hash_stats = benchmark_measure_execution_hash(range, debugger_program(analysis_debugger), execution_hash_keys);
	BenchmarkHashStats warm_hash_stats = benchmark_measure_execution_hash(range, debugger_program(analysis_debugger), execution_hash_keys);
	benchmark_print_hash_stats("execution hash, cold", cold_hash_stats);
	benchmark_print_hash_stats("execution hash, warm", warm_hash_stats);
	ExecutionGraph *graph = arena_push_zero(&arena, sizeof(*graph));
	ExecutionActivity *execution_activity = arena_push_zero(&arena, sizeof(*execution_activity));
	ExecutionPathState path = {};
	NES_MapAddr breakpoints[16] = {};
	for (u32 index = 0; index < ArrayCount(breakpoints); ++index) {
		breakpoints[index] = (NES_MapAddr) { NES_DEVICE_PRG_ROM, MAX_VALUE_U32 - index };
	}

	benchmark_scan_trace(range);
	for (u32 iteration = 0; iteration < iterations; ++iteration)
	{
		Seconds begin = seconds_now();
		benchmark_scan_trace(range);
		samples[iteration] = seconds_now().seconds - begin.seconds;
	}
	BenchmarkStats traversal_stats = benchmark_stats(samples, iterations);
	benchmark_print("trace traversal", traversal_stats);

	benchmark_scan_program(analysis_debugger, range);
	for (u32 iteration = 0; iteration < iterations; ++iteration)
	{
		Seconds begin = seconds_now();
		benchmark_scan_program(analysis_debugger, range);
		samples[iteration] = seconds_now().seconds - begin.seconds;
	}
	BenchmarkStats program_stats = benchmark_stats(samples, iterations);
	benchmark_print("program observation", program_stats);
	benchmark_print_delta("program cost", program_stats, traversal_stats);

	execution_graph_reset(graph);
	benchmark_scan_program_graph(analysis_debugger, graph, &path, range, breakpoints, 0);
	for (u32 iteration = 0; iteration < iterations; ++iteration)
	{
		execution_path_discard(&path);
		Seconds begin = seconds_now();
		benchmark_scan_program_graph(analysis_debugger, graph, &path, range, breakpoints, 0);
		samples[iteration] = seconds_now().seconds - begin.seconds;
	}
	BenchmarkStats graph_stats = benchmark_stats(samples, iterations);
	benchmark_print("program + graph", graph_stats);
	benchmark_print_delta("graph cost", graph_stats, program_stats);
	printf("%-24s %u tracked edges\n", "", graph->edge_count);

	execution_graph_reset(graph);
	execution_path_discard(&path);
	benchmark_scan_program_graph(analysis_debugger, graph, &path, range, breakpoints, 0);
	execution_activity_update(execution_activity, graph, 1.0);
	for (u32 iteration = 0; iteration < iterations; ++iteration)
	{
		execution_path_discard(&path);
		Seconds begin = seconds_now();
		benchmark_scan_program_graph(analysis_debugger, graph, &path, range, breakpoints, 0);
		execution_activity_update(execution_activity, graph, 1.0 + (f64)(iteration + 1) / 60.0);
		samples[iteration] = seconds_now().seconds - begin.seconds;
	}
	BenchmarkStats graph_activity_stats = benchmark_stats(samples, iterations);
	benchmark_print("program + graph + app", graph_activity_stats);
	benchmark_print_delta("app activity cost", graph_activity_stats, graph_stats);

	for (u32 iteration = 0; iteration < iterations; ++iteration)
	{
		for (u32 edge_index = 0; edge_index < graph->edge_count; ++edge_index) {
			++graph->entries[graph->used_slots[edge_index]].hit_count;
		}
		Seconds begin = seconds_now();
		execution_activity_update(execution_activity, graph, 2.0 + (f64)(iteration + 1) / 60.0);
		samples[iteration] = seconds_now().seconds - begin.seconds;
	}
	benchmark_print("app activity update", benchmark_stats(samples, iterations));

	execution_graph_reset(graph);
	execution_path_discard(&path);
	benchmark_scan_program_graph(analysis_debugger, graph, &path, range, breakpoints, 1);
	for (u32 iteration = 0; iteration < iterations; ++iteration)
	{
		execution_path_discard(&path);
		Seconds begin = seconds_now();
		benchmark_scan_program_graph(analysis_debugger, graph, &path, range, breakpoints, 1);
		samples[iteration] = seconds_now().seconds - begin.seconds;
	}
	BenchmarkStats breakpoint_one_stats = benchmark_stats(samples, iterations);
	benchmark_print("+ breakpoint, 1", breakpoint_one_stats);
	benchmark_print_delta("breakpoint 1 cost", breakpoint_one_stats, graph_stats);

	execution_graph_reset(graph);
	execution_path_discard(&path);
	benchmark_scan_program_graph(analysis_debugger, graph, &path, range, breakpoints, ArrayCount(breakpoints));
	for (u32 iteration = 0; iteration < iterations; ++iteration)
	{
		execution_path_discard(&path);
		Seconds begin = seconds_now();
		benchmark_scan_program_graph(analysis_debugger, graph, &path, range, breakpoints, ArrayCount(breakpoints));
		samples[iteration] = seconds_now().seconds - begin.seconds;
	}
	BenchmarkStats breakpoint_sixteen_stats = benchmark_stats(samples, iterations);
	benchmark_print("+ breakpoint, 16", breakpoint_sixteen_stats);
	benchmark_print_delta("breakpoint 16 cost", breakpoint_sixteen_stats, graph_stats);

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
