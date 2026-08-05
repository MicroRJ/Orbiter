#include "orb.h"

static b32 orb_test_write_file(const char *path, ByteSpan data)
{
	Platform_File file = platform_access_file(path, PLATFORM_FILE_CREATE_ALWAYS, PLATFORM_FILE_WRITE);
	if (!platform_file_is_valid(file)) return false;
	u64 written = 0;
	b32 success = platform_write_file(file, data.data, data.size, &written) && written == data.size;
	platform_close_file(file);
	return success;
}

static Orb_Game orb_test_game(Arena *arena)
{
	u8 *prg_rom = arena_push_zero(arena, KiB(16));
	u8 *chr_rom = arena_push_zero(arena, KiB(8));
	prg_rom[0] = 0xEA;
	prg_rom[0x3FFC] = 0x00;
	prg_rom[0x3FFD] = 0x80;
	chr_rom[0] = 0x80;
	return (Orb_Game) {
		.metadata = {
			.mapper = 0,
			.vmirror = true,
			.prg_rom_size = KiB(16),
			.chr_rom_size = KiB(8),
		},
		.prg_rom_data = prg_rom,
		.chr_rom_data = chr_rom,
	};
}

int main(void)
{
	Arena source_arena = arena_create(0, "ORB source test");
	Arena encoded_arena = arena_create(0, "ORB encoded test");
	Arena decoded_arena = arena_create(0, "ORB decoded test");
	Arena roundtrip_arena = arena_create(0, "ORB roundtrip test");

	Orb_Game game = orb_test_game(&source_arena);
	u8 thumbnail_pixels[] = {
		255, 0, 0, 255, 0, 255, 0, 255,
		0, 0, 255, 255, 255, 255, 255, 255,
	};

	Orb *source = arena_push_zero(&source_arena, sizeof(*source));
	Orb_SaveNode *first_save = arena_push_zero(&source_arena, sizeof(*first_save));
	Orb_SaveNode *second_save = arena_push_zero(&source_arena, sizeof(*second_save));
	source->game = game;
	source->game_hash = orb_game_hash(game);
	source->title = LIT("Roundtrip ORB");
	source->first_played_unix_ms = 100;
	source->last_played_unix_ms = 500;
	source->play_time_ms = 400;
	source->first_save = first_save;
	source->last_save = second_save;
	first_save->next = second_save;
	first_save->orb = source;
	second_save->orb = source;
	first_save->metadata = (Orb_SaveMetadata) {
		.kind = ORB_SAVE_RESUME,
		.id = { .bytes = { 1 } },
		.created_unix_ms = 100,
		.updated_unix_ms = 300,
		.play_time_ms = 200,
	};
	second_save->metadata = (Orb_SaveMetadata) {
		.kind = ORB_SAVE_MANUAL,
		.id = { .bytes = { 2 } },
		.created_unix_ms = 400,
		.updated_unix_ms = 500,
		.play_time_ms = 400,
	};
	first_save->state.cpu.PC = 0x8123;
	first_save->state.ppu.xtick = 123;
	first_save->state.video[7][11] = 0x2A;
	second_save->state.cpu.PC = 0x9234;
	second_save->state.scheduler_clock = 123456;
	first_save->thumbnail = (Orb_Thumbnail) {
		.width = 2,
		.height = 2,
		.stride = 8,
		.format = ORB_PIXEL_FORMAT_RGBA8,
		.pixels = byte_span(thumbnail_pixels, sizeof(thumbnail_pixels)),
	};

	ByteSpan encoded = orb_write(&encoded_arena, source);
	Assert(encoded.data && encoded.size);
	Assert(source->save_count == 2);

	Orb *decoded = orb_read(&decoded_arena, encoded);
	Assert(decoded);
	Assert(str_match(decoded->title, source->title));
	Assert(decoded->first_played_unix_ms == 100);
	Assert(decoded->last_played_unix_ms == 500);
	Assert(decoded->play_time_ms == 400);
	Assert(hash256_match(decoded->game_hash, source->game_hash));
	Assert(decoded->game.metadata.mapper == game.metadata.mapper);
	Assert(decoded->game.metadata.vmirror == game.metadata.vmirror);
	Assert(decoded->game.metadata.prg_rom_size == KiB(16));
	Assert(decoded->game.metadata.chr_rom_size == KiB(8));
	Assert(memory_match(decoded->game.prg_rom_data, game.prg_rom_data, KiB(16)));
	Assert(memory_match(decoded->game.chr_rom_data, game.chr_rom_data, KiB(8)));
	Assert(decoded->save_count == 2);
	Assert(decoded->first_save && decoded->last_save && decoded->first_save != decoded->last_save);
	Assert(decoded->first_save->orb == decoded);
	Assert(decoded->last_save->orb == decoded);
	Assert(decoded->first_save->state.cpu.PC == 0x8123);
	Assert(decoded->first_save->state.ppu.xtick == 123);
	Assert(decoded->first_save->state.video[7][11] == 0x2A);
	Assert(decoded->last_save->state.cpu.PC == 0x9234);
	Assert(decoded->last_save->state.scheduler_clock == 123456);
	Assert(decoded->first_save->thumbnail.width == 2);
	Assert(decoded->first_save->thumbnail.height == 2);
	Assert(decoded->first_save->thumbnail.pixels.size == sizeof(thumbnail_pixels));
	Assert(memory_match(decoded->first_save->thumbnail.pixels.data, thumbnail_pixels, sizeof(thumbnail_pixels)));

	ByteSpan roundtrip = orb_write(&roundtrip_arena, decoded);
	Assert(roundtrip.size == encoded.size);
	Assert(memory_match(roundtrip.data, encoded.data, encoded.size));

	for (u64 size = 0; size < 8; size ++) Assert(!orb_read(&decoded_arena, byte_span(encoded.data, size)));
	u8 *invalid_magic = arena_push_copy(&source_arena, encoded.size, encoded.data);
	invalid_magic[0] ^= 0x80;
	Assert(!orb_read(&decoded_arena, byte_span(invalid_magic, encoded.size)));

	const char orb_path[] = "orb_store_test.orb";
	Assert(orb_test_write_file(orb_path, encoded));
	Orb_Store store = {};
	orb_store_init(&store);
	Orb *loaded = orb_from_file(&store, str_from_cstr(orb_path));
	Assert(loaded && loaded == store.orb);
	Assert(loaded->save_count == 2);
	Assert(str_match(loaded->title, source->title));
	orb_store_destroy(&store);
	Assert(platform_remove_file(orb_path));

	u32 ines_size = 16 + KiB(16) + KiB(8);
	u8 *ines = arena_push_zero(&source_arena, ines_size);
	ines[0] = 'N'; ines[1] = 'E'; ines[2] = 'S'; ines[3] = 0x1A;
	ines[4] = 1;
	ines[5] = 1;
	ines[6] = 1; // Vertical mirroring.
	ines[16] = 0xEA;
	const char ines_path[] = "orb_store_test.nes";
	Assert(orb_test_write_file(ines_path, byte_span(ines, ines_size)));
	orb_store_init(&store);
	loaded = orb_from_file(&store, str_from_cstr(ines_path));
	Assert(loaded && loaded->save_count == 0);
	Assert(loaded->game.metadata.mapper == 0);
	Assert(loaded->game.metadata.vmirror == true);
	Assert(loaded->game.metadata.prg_rom_size == KiB(16));
	Assert(loaded->game.metadata.chr_rom_size == KiB(8));
	Assert(str_match(loaded->title, LIT("orb_store_test")));
	orb_store_destroy(&store);
	Assert(platform_remove_file(ines_path));

	arena_destroy(&roundtrip_arena);
	arena_destroy(&decoded_arena);
	arena_destroy(&encoded_arena);
	arena_destroy(&source_arena);
	return 0;
}
