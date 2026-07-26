#include "base.h"
#include "audio_stream.h"
#include "nes/emulator.h"
#include "emulator_internal.h"
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
		.sample_rate = 48000,
		.channels = 1,
		.frame_capacity = 4,
	});
	f32 output[4] = {};
	f32 initial[] = { 1, 2, 3, 4 };
	audio_stream_write(stream, initial, ArrayCount(initial));
	Assert(audio_stream_available_frames(stream) == 4);
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
	Assert(audio_stream_available_frames(stream) == 4);
	Assert(audio_stream_overrun_frames(stream) == 2);
	Assert(read_stream_frames(stream, output, ArrayCount(output)) == 4);
	Assert(output[0] == 9.0f && output[1] == 10.0f);
	Assert(output[2] == 11.0f && output[3] == 12.0f);

	f32 discarded = 13.0f;
	audio_stream_write(stream, &discarded, 1);
	audio_stream_discard(stream);
	Assert(audio_stream_available_frames(stream) == 0);
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

static void test_pull_audio_chunks(void)
{
	enum
	{
		SAMPLE_RATE = 48000,
		SAMPLE_COUNT = 8000,
		CHUNK_SAMPLES = 1000,
		SAMPLE_CAPACITY = 10000,
	};

	Arena arena = arena_create(0, "Orbiter audio test");
	NES_CartridgeDesc cartridge = make_looping_cartridge(&arena);
	NES_Emulator *whole = nes_emulator_create(&arena, (NES_EmulatorDesc) {
		.audio_sample_rate = SAMPLE_RATE,
	});
	NES_Emulator *chunked = nes_emulator_create(&arena, (NES_EmulatorDesc) {
		.audio_sample_rate = SAMPLE_RATE,
	});
	Assert(nes_emulator_load_cartridge(whole, cartridge));
	Assert(nes_emulator_load_cartridge(chunked, cartridge));

	f32 *whole_samples = arena_push_zero(&arena, sizeof(*whole_samples) * SAMPLE_CAPACITY);
	f32 *chunked_samples = arena_push_zero(&arena, sizeof(*chunked_samples) * SAMPLE_CAPACITY);
	u64 whole_count = nes_emulator_run_samples(whole, SAMPLE_COUNT, whole_samples, SAMPLE_CAPACITY);
	u64 chunked_count = 0;
	while (chunked_count < SAMPLE_COUNT)
	{
		chunked_count += nes_emulator_run_samples(chunked, CHUNK_SAMPLES,
			chunked_samples + chunked_count, SAMPLE_CAPACITY - chunked_count);
	}

	Assert(whole_count == SAMPLE_COUNT);
	Assert(chunked_count == whole_count);
	Assert(memory_match(whole_samples, chunked_samples, sizeof(*whole_samples) * whole_count));
	Assert(nes_emulator_scheduler_clock(whole) > 0);
	Assert(nes_emulator_scheduler_clock(chunked) == nes_emulator_scheduler_clock(whole));

	arena_destroy(&arena);
}

static void test_samples_follow_cpu_cycles(void)
{
	Arena arena = arena_create(0, "Orbiter audio cycle placement test");
	NES_CartridgeDesc cartridge = make_looping_cartridge(&arena);
	NES_Emulator *core = nes_emulator_create(&arena, (NES_EmulatorDesc) {
		.audio_sample_rate = NES_CPU_HZ,
	});
	Assert(nes_emulator_load_cartridge(core, cartridge));
	nes_emulator_reset(core);

	// Make the DAC audible across all three clocks of the JMP. Pulse muting is
	// based on the programmed period, not the changing countdown timer, so
	// sampling at each CPU-cycle boundary must preserve all three values.
	NES_APU_Pulse *pulse = &core->core.apu.pulse[0];
	pulse->enable = true;
	pulse->infinite_play = true;
	pulse->length_counter = 1;
	pulse->volume = 15;
	pulse->use_constant_volume = true;
	pulse->duty_mask = 0xFF;
	pulse->timer_period = 8;
	pulse->timer = 0;

	f32 samples[3] = {};
	Assert(nes_emulator_run_samples(core, ArrayCount(samples), samples, ArrayCount(samples)) == 3);
	Assert(samples[0] != 0.0f);
	Assert(samples[1] != 0.0f);
	Assert(samples[2] != 0.0f);
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

	NES_Emulator *core = nes_emulator_create(&arena, (NES_EmulatorDesc) {
		.audio_sample_rate = NES_CPU_HZ,
	});
	Assert(nes_emulator_load_cartridge(core, cartridge));
	nes_emulator_reset(core);

	nes_emulator_run(core, 6);

	// DMA is still instruction-atomic and included in the cycle count returned
	// by STA. However large that returned batch is, every one of those cycles
	// must pass through the exact same PPU/APU/audio scheduler boundary.
	f32 samples[1024] = {};
	u64 sample_count = nes_emulator_run_samples(core, 4, samples, ArrayCount(samples));
	Assert(sample_count > 4);
	arena_destroy(&arena);
}

static void test_pull_audio_instruction_overshoot(void)
{
	Arena arena = arena_create(0, "NES pull audio test");
	NES_CartridgeDesc cartridge = make_looping_cartridge(&arena);
	NES_Emulator *overshoot = nes_emulator_create(&arena, (NES_EmulatorDesc) {
		.audio_sample_rate = NES_CPU_HZ,
	});
	Assert(nes_emulator_load_cartridge(overshoot, cartridge));
	f32 overshoot_samples[6] = {};
	Assert(nes_emulator_run_samples(overshoot, 4, overshoot_samples, ArrayCount(overshoot_samples)) == 6);
	arena_destroy(&arena);
}

static void test_instruction_boundary_clocks(void)
{
	Arena arena = arena_create(0, "NES instruction boundary clock test");
	NES_Emulator *core = nes_emulator_create(&arena, (NES_EmulatorDesc) {
		.enable_instruction_boundaries = true,
	});
	Assert(nes_emulator_load_cartridge(core, make_looping_cartridge(&arena)));
	nes_emulator_run(core, 30);
	NES_InstructionBoundarySpan boundaries = nes_emulator_instruction_boundaries(core);
	Assert(boundaries.count > 0);
	Assert(boundaries.dropped == 0);
	for (u32 index = 1; index < boundaries.count; ++index) {
		Assert(boundaries.items[index].scheduler_clock > boundaries.items[index - 1].scheduler_clock);
	}
	arena_destroy(&arena);
}

int main(void)
{
	test_audio_stream();
	test_pull_audio_chunks();
	test_samples_follow_cpu_cycles();
	test_dma_cycles_cross_the_same_boundary();
	test_pull_audio_instruction_overshoot();
	test_instruction_boundary_clocks();
	return 0;
}
