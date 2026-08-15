#include "app_library_store.h"
#include "ines_importer.h"
#include "elf.h"

global const char test_directory[] = "app_library_store_test_tmp";
global const char test_manifest_path[] = "app_library_store_test_tmp\\library.elf";
global const char test_game_id[] = "28730633cffbfe02bd32cbf89002cb562603ffc46177f5cc2219fba3fd6834bb";
global const char test_game_directory[] = "app_library_store_test_tmp\\games\\28730633cffbfe02bd32cbf89002cb562603ffc46177f5cc2219fba3fd6834bb\\saves";
global const char test_prg_path[] = "app_library_store_test_tmp\\games\\28730633cffbfe02bd32cbf89002cb562603ffc46177f5cc2219fba3fd6834bb\\prg.bin";
global const char test_chr_path[] = "app_library_store_test_tmp\\games\\28730633cffbfe02bd32cbf89002cb562603ffc46177f5cc2219fba3fd6834bb\\chr.bin";
global const char test_save_path[] = "app_library_store_test_tmp\\games\\28730633cffbfe02bd32cbf89002cb562603ffc46177f5cc2219fba3fd6834bb\\saves\\resume.save";

static b32 app_library_store_test_write_file(const char *path, ByteSpan data)
{
	Platform_File file = platform_access_file(path, PLATFORM_FILE_CREATE_ALWAYS, PLATFORM_FILE_WRITE);
	if (!platform_file_is_valid(file)) return false;
	u64 written = 0;
	b32 success = (!data.size || platform_write_file(file, data.data, data.size, &written)) && written == data.size;
	platform_close_file(file);
	return success;
}

static b32 app_library_store_test_write_manifest(const char *path, const App_Library *library)
{
	elf_State *state = elf_create_state();
	if (!state) return false;
	app_library_push_elf(state, library);
	b32 success = elf_push_value_source(state, -1);
	elf_StrSlice source = {};
	if (success) success = elf_to_str(state, -1, &source);
	if (success) success = app_library_store_test_write_file(path, byte_span(source.data, source.size));
	elf_destroy_state(state);
	return success;
}

static void app_library_store_test_cleanup(void)
{
	char current_directory[2048];
	char absolute_target[2048];
	Platform_String_Result current = platform_get_current_directory(current_directory, sizeof(current_directory));
	Platform_String_Result target = platform_get_absolute_path(test_directory, absolute_target, sizeof(absolute_target));
	Assert(current.error == PLATFORM_ERROR_NONE && target.error == PLATFORM_ERROR_NONE);

	u64 current_size = current.size;
	b32 has_separator = current_size && (current_directory[current_size - 1] == '/' || current_directory[current_size - 1] == '\\');
	u64 name_size = sizeof(test_directory) - 1;
	u64 name_offset = current_size + !has_separator;
	Assert(target.size == name_offset + name_size);
	Assert(memory_match(absolute_target, current_directory, current_size));
	if (!has_separator) Assert(absolute_target[current_size] == '/' || absolute_target[current_size] == '\\');
	Assert(memory_match(absolute_target + name_offset, test_directory, name_size));
	Assert(platform_remove_tree(absolute_target));
}

static void app_library_store_test_compare_game(const NES_Game *game, const App_Save *actual_save, ByteSpan prg, ByteSpan chr, const App_Save *expected_save)
{
	Assert(game->metadata.mapper == 0);
	Assert(game->metadata.mirroring == NES_MIRROR_VERTICAL);
	Assert(!game->metadata.trainer_size);
	Assert(game->metadata.prg_rom_size == prg.size);
	Assert(game->metadata.chr_rom_size == chr.size);
	Assert(memory_match(game->prg_rom, prg.data, prg.size));
	Assert(memory_match(game->chr_rom, chr.data, chr.size));
	Assert(memory_match(&actual_save->state, &expected_save->state, sizeof(expected_save->state)));
	Assert(actual_save->thumbnail.width == expected_save->thumbnail.width);
	Assert(actual_save->thumbnail.height == expected_save->thumbnail.height);
	Assert(actual_save->thumbnail.stride == expected_save->thumbnail.stride);
	Assert(actual_save->thumbnail.format == expected_save->thumbnail.format);
	Assert(actual_save->thumbnail.pixels.size == expected_save->thumbnail.pixels.size);
	Assert(memory_match(actual_save->thumbnail.pixels.data, expected_save->thumbnail.pixels.data, expected_save->thumbnail.pixels.size));
}

static b32 app_library_store_test_verify_manifest(const char *path)
{
	App_LibraryStore *store = app_library_store_open(path);
	if (!store) return false;
	Arena arena = arena_create(0, "app library verification");
	b32 valid = true;
	for (u32 game_index = 0; game_index < store->library.game_count && valid; game_index ++)
	{
		App_LibraryGame *game = &store->library.games[game_index];
		App_LibrarySave *save = 0;
		for (u32 save_index = 0; save_index < game->save_count; save_index ++)
		{
			if (game->saves[save_index].kind == APP_LIBRARY_SAVE_RESUME) save = &game->saves[save_index];
		}
		NES_Game *loaded_game = arena_push_zero(&arena, sizeof(*loaded_game));
		App_Save *loaded_save = arena_push_zero(&arena, sizeof(*loaded_save));
		valid = save && app_library_store_read_game(store, &arena, game, save, loaded_game, loaded_save);
		NES_Emulator *emulator = arena_push_zero(&arena, sizeof(*emulator));
		if (valid)
		{
			valid = nes_setup_emulator(emulator, *loaded_game);
		}
		if (valid)
		{
			emulator->state = loaded_save->state;
			valid = nes_emulator_valid(emulator);
		}
		if (!valid) fprintf(stderr, "failed to verify '%.*s'\n", game->title.size, game->title.data);
		arena_reset(&arena);
	}
	if (valid) printf("verified %u library games\n", store->library.game_count);
	arena_destroy(&arena);
	app_library_store_close(store);
	return valid;
}

static void app_library_store_test_fresh_import_resume(void)
{
	app_library_store_test_cleanup();

	Arena source_arena = arena_create(0, "app library lifecycle source");
	Arena game_arena = arena_create(0, "app library lifecycle game");
	Arena emulator_arena = arena_create(0, "app library lifecycle emulator");
	Arena scratch = arena_create(0, "app library lifecycle scratch");
	App_LibraryStore *store = app_library_store_open(test_manifest_path);
	Assert(store && store->library.game_count == 0);

	enum { ines_header_size = 16, prg_size = KiB(16), chr_size = KiB(8) };
	u8 *ines = arena_push_zero(&source_arena, ines_header_size + prg_size + chr_size);
	ines[0] = 'N'; ines[1] = 'E'; ines[2] = 'S'; ines[3] = 0x1A;
	ines[4] = 1;
	ines[5] = 1;
	ines[6] = 0x01;
	u8 *source_prg = ines + ines_header_size;
	u8 *source_chr = source_prg + prg_size;
	memory_fill(source_prg, 0xEA, prg_size);
	for (u32 index = 0; index < chr_size; index ++) source_chr[index] = (u8)(index * 19 + 7);
	const u8 program[] = {
		0x78,             // SEI
		0xD8,             // CLD
		0xA2, 0xFF,       // LDX #$FF
		0x9A,             // TXS
		0xA9, 0x42,       // LDA #$42
		0x85, 0x02,       // STA $02
		0xE6, 0x03,       // INC $03
		0x4C, 0x09, 0x80, // JMP $8009
	};
	memory_copy(source_prg, program, sizeof(program));
	for (u32 vector = 0x3FFA; vector < 0x4000; vector += 2)
	{
		source_prg[vector + 0] = 0x00;
		source_prg[vector + 1] = 0x80;
	}
	ByteSpan ines_bytes = byte_span(ines, ines_header_size + prg_size + chr_size);
	NES_Game source_game = {};
	Assert(ines_import(ines_bytes, &source_game));
	Str title = LIT("lifecycle_game");
	Assert(source_game.metadata.mapper == 0);
	Assert(source_game.metadata.mirroring == NES_MIRROR_VERTICAL);
	Assert(!source_game.metadata.trainer_size);
	Assert(source_game.metadata.prg_rom_size == prg_size);
	Assert(source_game.metadata.chr_rom_size == chr_size);
	Assert(memory_match(source_game.prg_rom, source_prg, prg_size));
	Assert(memory_match(source_game.chr_rom, source_chr, chr_size));

	NES_Emulator *emulator = arena_push_zero(&emulator_arena, sizeof(*emulator));
	Assert(nes_setup_emulator(emulator, source_game));

	u8 thumbnail_pixels[] = {
		255, 0, 0, 255, 0, 255, 0, 255,
		0, 0, 255, 255, 255, 255, 255, 255,
	};
	App_Save *expected_save = arena_push_zero(&source_arena, sizeof(*expected_save));
	expected_save->thumbnail = (App_Thumbnail) {
		.width = 2,
		.height = 2,
		.stride = 8,
		.format = APP_PIXEL_FORMAT_RGBA8,
		.pixels = byte_span(thumbnail_pixels, sizeof(thumbnail_pixels)),
	};
	expected_save->state = emulator->state;

	App_LibraryGame *game = 0;
	App_LibrarySave *save = 0;
	Assert(app_library_store_import_game(store, &scratch, source_game, title, expected_save, &game, &save));
	Assert(store->library.game_count == 1);
	Assert(game == &store->library.games[0]);
	Assert(save == &game->saves[0] && save->kind == APP_LIBRARY_SAVE_RESUME);
	Assert(str_match(game->title, title));

	u64 initial_clock = emulator->scheduler_clock;
	for (u32 index = 0; index < 64; index ++) Assert(nes_emulator_step(emulator, 0));
	Assert(emulator->scheduler_clock > initial_clock);
	Assert(emulator->_wram[2] == 0x42);
	Assert(emulator->_wram[3] != 0);
	emulator->prg_ram[0x321] = 0x5A;
	thumbnail_pixels[0] = 17;
	expected_save->state = emulator->state;

	game->first_played_unix_ms = 1000;
	game->last_played_unix_ms = 9000;
	game->play_time_ms = 8000;
	save->created_unix_ms = 1000;
	save->updated_unix_ms = 9000;
	save->play_time_ms = 8000;
	Assert(app_library_store_write_save(store, &scratch, save, expected_save));
	Assert(app_library_store_write_manifest(store, &scratch));

	char expected_id[65];
	Assert(game->id.size == sizeof(expected_id) - 1);
	memory_copy(expected_id, game->id.data, game->id.size);
	expected_id[sizeof(expected_id) - 1] = 0;
	app_library_store_close(store);

	arena_reset(&game_arena);
	store = app_library_store_open(test_manifest_path);
	Assert(store && store->library.game_count == 1);
	game = &store->library.games[0];
	Assert(str_match(game->id, str_from_cstr(expected_id)));
	Assert(str_match(game->title, LIT("lifecycle_game")));
	Assert(game->first_played_unix_ms == 1000);
	Assert(game->last_played_unix_ms == 9000);
	Assert(game->play_time_ms == 8000);
	Assert(game->cartridge.metadata.mapper == 0);
	Assert(game->cartridge.metadata.mirroring == NES_MIRROR_VERTICAL);
	Assert(!game->cartridge.trainer_path.size);
	Assert(game->cartridge.metadata.prg_rom_size == prg_size);
	Assert(game->cartridge.metadata.chr_rom_size == chr_size);
	Assert(game->save_count == 1);
	save = &game->saves[0];
	Assert(save->kind == APP_LIBRARY_SAVE_RESUME);
	Assert(save->created_unix_ms == 1000);
	Assert(save->updated_unix_ms == 9000);
	Assert(save->play_time_ms == 8000);

	NES_Game *loaded_game = arena_push_zero(&game_arena, sizeof(*loaded_game));
	App_Save *loaded_save = arena_push_zero(&game_arena, sizeof(*loaded_save));
	Assert(app_library_store_read_game(store, &game_arena, game, save, loaded_game, loaded_save));
	app_library_store_test_compare_game(loaded_game, loaded_save, byte_span(source_prg, prg_size), byte_span(source_chr, chr_size), expected_save);
	Assert(!loaded_game->trainer);

	NES_Emulator *restored = arena_push_zero(&emulator_arena, sizeof(*restored));
	Assert(nes_setup_emulator(restored, *loaded_game));
	restored->state = loaded_save->state;
	Assert(nes_emulator_valid(restored));
	NES_State *recaptured = arena_push_zero(&source_arena, sizeof(*recaptured));
	*recaptured = restored->state;
	Assert(memory_match(recaptured, &expected_save->state, sizeof(*recaptured)));

	app_library_store_close(store);
	arena_destroy(&scratch);
	arena_destroy(&emulator_arena);
	arena_destroy(&game_arena);
	arena_destroy(&source_arena);
	app_library_store_test_cleanup();
}

int main(int argc, char **argv)
{
	if (argc == 2) return app_library_store_test_verify_manifest(argv[1]) ? 0 : 1;
	Assert(argc == 1);
	app_library_store_test_fresh_import_resume();
	app_library_store_test_cleanup();
	Assert(platform_create_directories(test_game_directory));

	Arena source_arena = arena_create(0, "app library store test source");
	Arena game_arena = arena_create(0, "app library store test game");
	Arena scratch = arena_create(0, "app library store test scratch");
	u8 *prg = arena_push_aligned(&source_arena, KiB(16), 1);
	u8 *chr = arena_push_aligned(&source_arena, KiB(8), 1);
	for (u32 index = 0; index < KiB(16); index ++) prg[index] = (u8)(index * 37 + 11);
	for (u32 index = 0; index < KiB(8); index ++) chr[index] = (u8)(index * 19 + 7);
	ByteSpan prg_bytes = byte_span(prg, KiB(16));
	ByteSpan chr_bytes = byte_span(chr, KiB(8));
	Assert(app_library_store_test_write_file(test_prg_path, prg_bytes));
	Assert(app_library_store_test_write_file(test_chr_path, chr_bytes));

	u8 thumbnail_pixels[] = {
		255, 0, 0, 255, 0, 255, 0, 255,
		0, 0, 255, 255, 255, 255, 255, 255,
	};
	App_Save *expected_save = arena_push_zero(&source_arena, sizeof(*expected_save));
	expected_save->state.scheduler_clock = 123456789;
	expected_save->state.sample_phase = 987654;
	expected_save->state.cpu.PC = 0x8123;
	expected_save->state.ppu.dot = 123;
	expected_save->state._wram[0x123] = 0xA5;
	expected_save->state.video[17][29] = 0x2A;
	expected_save->thumbnail = (App_Thumbnail) {
		.width = 2,
		.height = 2,
		.stride = 8,
		.format = APP_PIXEL_FORMAT_RGBA8,
		.pixels = byte_span(thumbnail_pixels, sizeof(thumbnail_pixels)),
	};
	ByteSpan encoded_save = app_save_encode(&source_arena, expected_save);
	Assert(encoded_save.data && encoded_save.size);
	Assert(app_library_store_test_write_file(test_save_path, encoded_save));

	App_LibrarySave saves[] = {
		{
			.id = LIT("resume"),
			.kind = APP_LIBRARY_SAVE_RESUME,
			.label = LIT("Continue"),
			.path = LIT("games/28730633cffbfe02bd32cbf89002cb562603ffc46177f5cc2219fba3fd6834bb/saves/resume.save"),
			.created_unix_ms = 1000,
			.updated_unix_ms = 2000,
			.play_time_ms = 3000,
		},
	};
	App_LibraryGame games[] = {
		{
			.id = LIT("28730633cffbfe02bd32cbf89002cb562603ffc46177f5cc2219fba3fd6834bb"),
			.title = LIT("Minimal NROM"),
			.developer = LIT("Orbiter Test"),
			.release_year = 1985,
			.first_played_unix_ms = 1000,
			.last_played_unix_ms = 2000,
			.play_time_ms = 3000,
			.cartridge = {
				.metadata = { .mapper = 0, .mirroring = NES_MIRROR_VERTICAL, .prg_rom_size = KiB(16), .chr_rom_size = KiB(8) },
				.prg_path = LIT("games/28730633cffbfe02bd32cbf89002cb562603ffc46177f5cc2219fba3fd6834bb/prg.bin"),
				.chr_path = LIT("games/28730633cffbfe02bd32cbf89002cb562603ffc46177f5cc2219fba3fd6834bb/chr.bin"),
			},
			.saves = saves,
			.save_count = ArrayCount(saves),
		},
	};
	App_Library source_library = { .games = games, .game_count = ArrayCount(games) };
	Assert(app_library_store_test_write_manifest(test_manifest_path, &source_library));

	App_LibraryStore *store = app_library_store_open(test_manifest_path);
	Assert(store && store->library.game_count == 1);
	App_LibraryGame *game = &store->library.games[0];
	Assert(str_match(game->id, str_from_cstr(test_game_id)));
	Assert(game->save_count == 1);
	App_LibrarySave *save = &game->saves[0];
	NES_Game *loaded_game = arena_push_zero(&game_arena, sizeof(*loaded_game));
	App_Save *loaded_save = arena_push_zero(&game_arena, sizeof(*loaded_save));
	Assert(app_library_store_read_game(store, &game_arena, game, save, loaded_game, loaded_save));
	app_library_store_test_compare_game(loaded_game, loaded_save, prg_bytes, chr_bytes, expected_save);

	loaded_save->state.scheduler_clock = 777777777;
	loaded_save->state.cpu.PC = 0x9234;
	loaded_save->state.prg_ram[0x321] = 0x5A;
	loaded_save->thumbnail.pixels.data[0] = 17;
	expected_save->state = loaded_save->state;
	thumbnail_pixels[0] = 17;
	game->last_played_unix_ms = 9000;
	game->play_time_ms = 8000;
	save->updated_unix_ms = 9000;
	save->play_time_ms = 8000;
	Assert(app_library_store_write_save(store, &scratch, save, loaded_save));
	Assert(app_library_store_write_manifest(store, &scratch));
	app_library_store_close(store);

	arena_reset(&game_arena);
	store = app_library_store_open(test_manifest_path);
	Assert(store && store->library.game_count == 1);
	game = &store->library.games[0];
	Assert(game->last_played_unix_ms == 9000);
	Assert(game->play_time_ms == 8000);
	Assert(game->save_count == 1);
	save = &game->saves[0];
	Assert(save->updated_unix_ms == 9000);
	Assert(save->play_time_ms == 8000);
	loaded_game = arena_push_zero(&game_arena, sizeof(*loaded_game));
	loaded_save = arena_push_zero(&game_arena, sizeof(*loaded_save));
	Assert(app_library_store_read_game(store, &game_arena, game, save, loaded_game, loaded_save));
	app_library_store_test_compare_game(loaded_game, loaded_save, prg_bytes, chr_bytes, expected_save);

	App_LibraryGame *original_game = game;
	u8 *imported_prg = arena_push_aligned(&source_arena, prg_bytes.size, 1);
	memory_copy(imported_prg, prg_bytes.data, prg_bytes.size);
	imported_prg[0] ^= 0xFF;
	NES_Game imported_source = *loaded_game;
	imported_source.prg_rom = imported_prg;
	App_LibraryGame *imported_game = 0;
	App_LibrarySave *imported_save = 0;
	Assert(app_library_store_import_game(store, &scratch, imported_source, LIT("Imported NROM"), loaded_save, &imported_game, &imported_save));
	Assert(store->library.game_count == 2);
	Assert(original_game == &store->library.games[0]);
	Assert(imported_game == &store->library.games[1]);
	Assert(imported_save == &imported_game->saves[0]);
	app_library_store_close(store);

	arena_reset(&game_arena);
	store = app_library_store_open(test_manifest_path);
	Assert(store && store->library.game_count == 2);
	imported_game = &store->library.games[1];
	imported_save = &imported_game->saves[0];
	loaded_game = arena_push_zero(&game_arena, sizeof(*loaded_game));
	loaded_save = arena_push_zero(&game_arena, sizeof(*loaded_save));
	Assert(app_library_store_read_game(store, &game_arena, imported_game, imported_save, loaded_game, loaded_save));
	app_library_store_test_compare_game(loaded_game, loaded_save, byte_span(imported_prg, prg_bytes.size), chr_bytes, expected_save);
	app_library_store_close(store);

	arena_destroy(&scratch);
	arena_destroy(&game_arena);
	arena_destroy(&source_arena);
	app_library_store_test_cleanup();
	return 0;
}
