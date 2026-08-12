#include "base.h"
#include "debugger/debugger.h"
#include "nes/emulator.h"
#include "orb.h"
#include "os.h"

static b32 benchmark_setup_emulator(NES_Emulator *emulator, const Orb *orb)
{
	const Orb_Game *game = &orb->game;
	NES_SetupParams params = {
		.mapper = game->metadata.mapper,
		.vmirror = game->metadata.vmirror,
		.four_screen = game->metadata.four_screen,
		.has_trainer = game->metadata.has_trainer,
		.prg_rom = byte_span(game->prg_rom_data, game->metadata.prg_rom_size),
		.chr_rom = byte_span(game->chr_rom_data, game->metadata.chr_rom_size),
	};
	return nes_setup_emulator(emulator, params);
}

int main(int argc, char **argv)
{
	if (argc != 3)
	{
		fprintf(stderr, "usage: %s <game.nes|game.orb> <frames>\n", argv[0]);
		return 2;
	}

	u32 frame_count = 0;
	if (!str_to_u32(str_from_cstr(argv[2]), &frame_count) || !frame_count)
	{
		fprintf(stderr, "invalid frame count: '%s'\n", argv[2]);
		return 2;
	}

	if (!os_init()) return 1;

	Arena arena = arena_create(0, "NES benchmark");
	Orb_Store store;
	orb_store_init(&store);
	Orb *orb = orb_from_file(&store, str_from_cstr(argv[1]));
	NES_Emulator *emulator = arena_push_zero(&arena, sizeof(*emulator));
	if (!orb || !benchmark_setup_emulator(emulator, orb))
	{
		fprintf(stderr, "failed to load game '%s'\n", argv[1]);
		orb_store_destroy(&store);
		arena_destroy(&arena);
		os_shutdown();
		return 1;
	}

	Debugger *debugger = debugger_create(&arena, emulator);
	debugger_reset(debugger);
	u64 sample_capacity = nes_required_sample_capacity();
	f32 *samples = arena_push(&arena, sizeof(*samples) * sample_capacity);
	Seconds begin = seconds_now();
	for (u32 frame = 0; frame < frame_count; ++frame) {
		debugger_run_frame(debugger, samples, sample_capacity);
	}
	f64 elapsed_seconds = seconds_now().seconds - begin.seconds;

	printf("%u frames\n", frame_count);
	printf("total       %.3f ms\n", elapsed_seconds * 1000.0);
	printf("per frame   %.3f ms\n", elapsed_seconds * 1000.0 / frame_count);
	printf("throughput  %.2f frames/s\n", frame_count / elapsed_seconds);

	orb_store_destroy(&store);
	arena_destroy(&arena);
	os_shutdown();
	return 0;
}
