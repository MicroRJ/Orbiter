#include "base.h"
#include "debugger/nes_process.h"
#include "nes/emulator.h"
#include "ines_importer.h"
#include "os.h"

int main(int argc, char **argv)
{
	if (argc != 3)
	{
		fprintf(stderr, "usage: %s <game.nes> <frames>\n", argv[0]);
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
	ByteSpan source = push_file(&arena, str_from_cstr(argv[1]));
	NES_Game game = {};
	NES_Process *debugger = nes_process_create(&arena);
	NES_Emulator *emulator = &debugger->emulator;
	if (!source.data || !ines_import(source, &game) || !nes_setup_emulator(emulator, game))
	{
		fprintf(stderr, "failed to load game '%s'\n", argv[1]);
		arena_destroy(&arena);
		os_shutdown();
		return 1;
	}

	nes_process_reset(debugger);
	u64 sample_capacity = nes_required_sample_capacity();
	f32 *samples = arena_push(&arena, sizeof(*samples) * sample_capacity);
	Seconds begin = seconds_now();
	for (u32 frame = 0; frame < frame_count; ++frame) {
		nes_process_run_frame(debugger, samples, sample_capacity);
	}
	f64 elapsed_seconds = seconds_now().seconds - begin.seconds;

	printf("%u frames\n", frame_count);
	printf("total       %.3f ms\n", elapsed_seconds * 1000.0);
	printf("per frame   %.3f ms\n", elapsed_seconds * 1000.0 / frame_count);
	printf("throughput  %.2f frames/s\n", frame_count / elapsed_seconds);

	arena_destroy(&arena);
	os_shutdown();
	return 0;
}
