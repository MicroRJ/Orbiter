#include "ines_importer.h"
#include "app_save.h"

static b32 ines_importer_test_write_file(const char *path, ByteSpan data)
{
	Platform_File file = platform_access_file(path, PLATFORM_FILE_CREATE_ALWAYS, PLATFORM_FILE_WRITE);
	if (!platform_file_is_valid(file)) return false;
	u64 written = 0;
	b32 success = platform_write_file(file, data.data, data.size, &written) && written == data.size;
	platform_close_file(file);
	return success;
}

static NES_Game ines_importer_test_game(Arena *arena)
{
	u8 *prg_rom = arena_push_zero(arena, KiB(16));
	u8 *chr_rom = arena_push_zero(arena, KiB(8));
	prg_rom[0] = 0xEA;
	prg_rom[0x3FFC] = 0x00;
	prg_rom[0x3FFD] = 0x80;
	chr_rom[0] = 0x80;
	return (NES_Game) {
		.metadata = {
			.mapper = 0,
			.mirroring = NES_MIRROR_VERTICAL,
			.prg_rom_size = KiB(16),
			.chr_rom_size = KiB(8),
		},
		.prg_rom = prg_rom,
		.chr_rom = chr_rom,
	};
}

static void ines_importer_test_save_state_transfer(Arena *arena)
{
	NES_Game game = ines_importer_test_game(arena);
	NES_Emulator *source = arena_push_zero(arena, sizeof(*source));
	NES_Emulator *restored = arena_push_zero(arena, sizeof(*restored));
	Assert(nes_setup_emulator(source, game));
	Assert(nes_setup_emulator(restored, game));
	memory_fill(&source->state, 0x5A, sizeof(source->state));

	NES_State *expected = arena_push_zero(arena, sizeof(*expected));
	NES_State *actual = arena_push_zero(arena, sizeof(*actual));
	*expected = source->state;
	restored->state = *expected;
	*actual = restored->state;
	Assert(memory_match(actual, expected, sizeof(*actual)));
	Assert(restored->mapper_number == game.metadata.mapper);
	Assert(restored->prg_rom_size == game.metadata.prg_rom_size);
	Assert(restored->chr_rom_size == game.metadata.chr_rom_size);
	Assert(memory_match(restored->prg_rom, game.prg_rom, game.metadata.prg_rom_size));
	Assert(memory_match(restored->chr_rom, game.chr_rom, game.metadata.chr_rom_size));
}

static void ines_importer_test_app_save(Arena *encoded_arena, Arena *decoded_arena)
{
	u8 thumbnail_pixels[] = {
		255, 0, 0, 255, 0, 255, 0, 255,
		0, 0, 255, 255, 255, 255, 255, 255,
	};
	App_Save source = {
		.thumbnail = {
			.width = 2,
			.height = 2,
			.stride = 8,
			.format = APP_PIXEL_FORMAT_RGBA8,
			.pixels = byte_span(thumbnail_pixels, sizeof(thumbnail_pixels)),
		},
	};
	source.state.cpu.PC = 0x8123;
	source.state.ppu.xtick = 123;
	source.state.video[7][11] = 0x2A;
	source.state.scheduler_clock = 123456;

	ByteSpan encoded = app_save_encode(encoded_arena, &source);
	Assert(encoded.data && encoded.size);
	App_Save decoded = {};
	Assert(app_save_decode(decoded_arena, encoded, &decoded));
	Assert(memory_match(&decoded.state, &source.state, sizeof(source.state)));
	Assert(decoded.thumbnail.width == source.thumbnail.width);
	Assert(decoded.thumbnail.height == source.thumbnail.height);
	Assert(decoded.thumbnail.stride == source.thumbnail.stride);
	Assert(decoded.thumbnail.format == source.thumbnail.format);
	Assert(decoded.thumbnail.pixels.size == source.thumbnail.pixels.size);
	Assert(memory_match(decoded.thumbnail.pixels.data, source.thumbnail.pixels.data, source.thumbnail.pixels.size));

	for (u64 size = 0; size < 8; size ++) Assert(!app_save_decode(decoded_arena, byte_span(encoded.data, size), &decoded));
	Assert(!app_save_decode(decoded_arena, byte_span(encoded.data, encoded.size - 1), &decoded));
	u8 *invalid_magic = arena_push_copy(encoded_arena, encoded.size, encoded.data);
	invalid_magic[0] ^= 0x80;
	Assert(!app_save_decode(decoded_arena, byte_span(invalid_magic, encoded.size), &decoded));
}

static void ines_importer_test_import(Arena *source_arena, Arena *game_arena)
{
	const char path[] = "ines_importer_test.nes";
	u32 source_size = 16 + 512 + KiB(16) + KiB(8);
	u8 *source = arena_push_zero(source_arena, source_size);
	source[0] = 'N'; source[1] = 'E'; source[2] = 'S'; source[3] = 0x1A;
	source[4] = 1;
	source[5] = 1;
	source[6] = 0x05;
	source[16] = 0xA5;
	source[16 + 512] = 0xEA;
	source[16 + 512 + KiB(16)] = 0x80;
	Assert(ines_importer_test_write_file(path, byte_span(source, source_size)));

	Str title = {};
	NES_Game *game = ines_import_file(game_arena, str_from_cstr(path), &title);
	Assert(game);
	Assert(str_match(title, LIT("ines_importer_test")));
	Assert(game->metadata.mapper == 0);
	Assert(game->metadata.mirroring == NES_MIRROR_VERTICAL);
	Assert(game->metadata.trainer_size == 512);
	Assert(game->metadata.prg_rom_size == KiB(16));
	Assert(game->metadata.chr_rom_size == KiB(8));
	Assert(game->trainer[0] == 0xA5);
	Assert(game->prg_rom[0] == 0xEA);
	Assert(game->chr_rom[0] == 0x80);

	u64 arena_position = game_arena->position;
	source[0] = 0;
	Assert(ines_importer_test_write_file(path, byte_span(source, source_size)));
	title = LIT("unchanged");
	Assert(!ines_import_file(game_arena, str_from_cstr(path), &title));
	Assert(!title.data && !title.size);
	Assert(game_arena->position == arena_position);
	Assert(ines_importer_test_write_file(path, byte_span(source + 1, 7)));
	Assert(!ines_import_file(game_arena, str_from_cstr(path), 0));
	Assert(game_arena->position == arena_position);
	Assert(platform_remove_file(path));
}

int main(void)
{
	Arena source_arena = arena_create(0, "iNES importer source test");
	Arena encoded_arena = arena_create(0, "iNES importer encoded test");
	Arena decoded_arena = arena_create(0, "iNES importer decoded test");
	Arena game_arena = arena_create(0, "iNES importer game test");
	ines_importer_test_save_state_transfer(&source_arena);
	ines_importer_test_app_save(&encoded_arena, &decoded_arena);
	ines_importer_test_import(&source_arena, &game_arena);
	arena_destroy(&game_arena);
	arena_destroy(&decoded_arena);
	arena_destroy(&encoded_arena);
	arena_destroy(&source_arena);
	return 0;
}
