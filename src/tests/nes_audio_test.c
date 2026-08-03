#include "base.h"
#include "audio_stream.h"
#include "nes/emulator.h"
#include <string.h>

static u32 read_stream_frames(Audio_Stream *stream, f32 *frames, u32 capacity)
{
	u32 cursor = 0;
	while (cursor < capacity)
	{
		Audio_ReadSpan span = audio_stream_acquire(stream);
		u32 count = Min(span.frame_count, capacity - cursor);
		if (count == 0) break;
		memory_copy(frames + cursor, span.samples, sizeof(*frames) * count);
		audio_stream_consume(stream, count);
		cursor += count;
	}
	return cursor;
}

static void test_audio_stream(void)
{
	Arena arena = arena_create(0, "audio stream test");
	Audio_Stream *stream = audio_stream_create(&arena, (Audio_StreamDesc) {
		.frame_capacity = 4,
	});
	f32 output[4] = {};
	f32 initial[] = { 1, 2, 3, 4 };
	audio_stream_write(stream, initial, ArrayCount(initial));
	Assert(audio_stream_queued_frames(stream) == 4);
	Assert(audio_stream_capacity_frames(stream) == 4);
	Audio_ReadSpan held = audio_stream_acquire(stream);
	Assert(held.frame_count == 4);
	audio_stream_consume(stream, 0);
	Assert(audio_stream_acquire(stream).frame_count == 4);
	audio_stream_consume(stream, 0);
	Assert(read_stream_frames(stream, output, 2) == 2);
	Assert(output[0] == 1.0f && output[1] == 2.0f);

	// Exercise wraparound without overflowing.
	f32 wrapped[] = { 5, 6 };
	audio_stream_write(stream, wrapped, ArrayCount(wrapped));
	Assert(read_stream_frames(stream, output, ArrayCount(output)) == 4);
	Assert(output[0] == 3.0f && output[1] == 4.0f);
	Assert(output[2] == 5.0f && output[3] == 6.0f);
	Assert(audio_stream_overrun_frames(stream) == 0);

	// A full real-time queue retains the newest frames and reports each old
	// frame it had to replace.
	f32 overflow[] = { 7, 8, 9, 10, 11, 12 };
	audio_stream_write(stream, overflow, ArrayCount(overflow));
	Assert(audio_stream_queued_frames(stream) == 4);
	Assert(audio_stream_overrun_frames(stream) == 2);
	Assert(read_stream_frames(stream, output, ArrayCount(output)) == 4);
	Assert(output[0] == 9.0f && output[1] == 10.0f);
	Assert(output[2] == 11.0f && output[3] == 12.0f);

	f32 discarded = 13.0f;
	audio_stream_write(stream, &discarded, 1);
	audio_stream_discard(stream);
	Assert(audio_stream_queued_frames(stream) == 0);
	Assert(audio_stream_overrun_frames(stream) == 2);
	arena_destroy(&arena);
}

static NES_CartridgeDesc make_looping_cartridge(Arena *arena)
{
	u8 *prg = arena_push_zero(arena, KiB(16));
	u8 *chr = arena_push_zero(arena, KiB(8));
	memset(prg, 0xEA, KiB(16));
	prg[0] = 0x4C; // JMP $8000: a stable three-cycle execution loop.
	prg[1] = 0x00;
	prg[2] = 0x80;
	prg[0x3FFC] = 0x00;
	prg[0x3FFD] = 0x80;
	return (NES_CartridgeDesc) {
		.prg_rom = byte_span(prg, KiB(16)),
		.chr_rom = byte_span(chr, KiB(8)),
	};
}

static void test_step_advances_audio_phase(void)
{
	Arena arena = arena_create(0, "NES step audio phase test");
	NES_Emulator *core = arena_push_zero(&arena, sizeof(NES_Emulator));
	Assert(nes_emulator_load_cartridge(core, make_looping_cartridge(&arena)));
	u64 expected_phase = 0;
	for (u32 index = 0; index < 20; index ++)
	{
		u32 cycles = nes_emulator_step(core);
		expected_phase = (expected_phase + cycles * nes_sample_rate(core)) % NES_CPU_HZ;
		Assert(core->sample_phase == expected_phase);
	}
	arena_destroy(&arena);
}

static void test_run_frame_audio_contract(void)
{
	enum { FRAME_COUNT = 120 };
	Arena arena = arena_create(0, "NES frame audio contract test");
	NES_Emulator *core = arena_push_zero(&arena, sizeof(NES_Emulator));
	Assert(nes_emulator_load_cartridge(core, make_looping_cartridge(&arena)));

	u64 sample_capacity = nes_required_sample_capacity();
	f32 *guarded_samples = arena_push_zero(&arena, sizeof(*guarded_samples) * (sample_capacity + 2));
	guarded_samples[0] = -1234.5f;
	guarded_samples[sample_capacity + 1] = 9876.5f;
	u64 total_samples = 0;
	u64 total_cpu_cycles = 0;

	for (u32 frame_index = 0; frame_index < FRAME_COUNT; frame_index ++)
	{
		NES_RunFrameResult frame = nes_emulator_run_frame(core, guarded_samples + 1, sample_capacity);
		Assert(frame.steps > 0);
		Assert(frame.samples > 0 && frame.samples <= sample_capacity);
		Assert(guarded_samples[0] == -1234.5f);
		Assert(guarded_samples[sample_capacity + 1] == 9876.5f);
		Assert(core->ppu.ytick == 241);
		Assert(core->ppu.xtick < 16);

		total_samples += frame.samples;
		total_cpu_cycles += frame.steps * 3;
	}

	u64 generated_phase = total_cpu_cycles * nes_sample_rate(core);
	Assert(total_samples == generated_phase / NES_CPU_HZ);
	Assert(core->sample_phase == generated_phase % NES_CPU_HZ);

	NES_Emulator *discarding = arena_push_zero(&arena, sizeof(NES_Emulator));
	Assert(nes_emulator_load_cartridge(discarding, make_looping_cartridge(&arena)));
	Assert(nes_emulator_run_frame(discarding, 0, 0).samples > 0);
	arena_destroy(&arena);
}

static void test_dma_cycles_cross_the_same_boundary(void)
{
	Arena arena = arena_create(0, "Orbiter DMA scheduler test");
	NES_CartridgeDesc cartridge = make_looping_cartridge(&arena);
	u8 *prg = cartridge.prg_rom.data;
	prg[0] = 0xA9; // LDA #$02
	prg[1] = 0x02;
	prg[2] = 0x8D; // STA $4014: OAM DMA
	prg[3] = 0x14;
	prg[4] = 0x40;

	NES_Emulator *core = arena_push_zero(&arena, sizeof(NES_Emulator));
	Assert(nes_emulator_load_cartridge(core, cartridge));

	u32 first_cycles = nes_emulator_step(core);
	Assert(first_cycles == 2);

	// DMA is still instruction-atomic and included in the cycle count returned
	// by STA. However large that returned batch is, every one of those cycles
	// must pass through the exact same PPU/APU/audio scheduler boundary.
	u64 phase_before = core->sample_phase;
	u32 dma_cycles = nes_emulator_step(core);
	Assert(dma_cycles > 4);
	Assert(core->sample_phase == (phase_before + dma_cycles * nes_sample_rate(core)) % NES_CPU_HZ);
	arena_destroy(&arena);
}

static void test_instruction_boundary_clocks(void)
{
	Arena arena = arena_create(0, "NES instruction boundary clock test");
	NES_Emulator *core = arena_push_zero(&arena, sizeof(NES_Emulator));
	Assert(nes_emulator_load_cartridge(core, make_looping_cartridge(&arena)));
	for (u32 index = 0; index < 4; ++index) nes_emulator_step(core);
	NES_SchedulerTraceView boundaries = nes_emulator_scheduler_trace(core);
	Assert(!nes_scheduler_trace_dropped_since(boundaries, 0));
	Assert(boundaries.index > 0);
	for (u64 index = 1; index < boundaries.index; index ++) {
		NES_TraceEntry current = nes_scheduler_trace_at(boundaries, index);
		NES_TraceEntry previous = nes_scheduler_trace_at(boundaries, index - 1);
		Assert(current.scheduler_clock > previous.scheduler_clock);
	}
	arena_destroy(&arena);
}

int main(void)
{
	test_audio_stream();
	test_step_advances_audio_phase();
	test_run_frame_audio_contract();
	test_dma_cycles_cross_the_same_boundary();
	test_instruction_boundary_clocks();
	return 0;
}
