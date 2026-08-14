#include "app_library.h"
#include "elf.h"

static void app_library_assert_save_equal(const App_LibrarySave *left, const App_LibrarySave *right)
{
	Assert(str_match(left->id, right->id));
	Assert(left->kind == right->kind);
	Assert(left->flags == right->flags);
	Assert(str_match(left->label, right->label));
	Assert(str_match(left->path, right->path));
	Assert(left->created_unix_ms == right->created_unix_ms);
	Assert(left->updated_unix_ms == right->updated_unix_ms);
	Assert(left->play_time_ms == right->play_time_ms);
}

static void app_library_assert_game_equal(const App_LibraryGame *left, const App_LibraryGame *right)
{
	Assert(str_match(left->id, right->id));
	Assert(str_match(left->title, right->title));
	Assert(str_match(left->developer, right->developer));
	Assert(left->release_year == right->release_year);
	Assert(left->first_played_unix_ms == right->first_played_unix_ms);
	Assert(left->last_played_unix_ms == right->last_played_unix_ms);
	Assert(left->play_time_ms == right->play_time_ms);
	Assert(left->cartridge.mapper == right->cartridge.mapper);
	Assert(left->cartridge.mirroring == right->cartridge.mirroring);
	Assert(str_match(left->cartridge.trainer_path, right->cartridge.trainer_path));
	Assert(str_match(left->cartridge.prg_path, right->cartridge.prg_path));
	Assert(left->cartridge.prg_size == right->cartridge.prg_size);
	Assert(str_match(left->cartridge.chr_path, right->cartridge.chr_path));
	Assert(left->cartridge.chr_size == right->cartridge.chr_size);
	Assert(left->save_count == right->save_count);
	for (u32 index = 0; index < left->save_count; index ++) app_library_assert_save_equal(&left->saves[index], &right->saves[index]);
}

int main(void)
{
	App_LibrarySave neon_saves[] = {
		{
			.id = LIT("resume"),
			.kind = APP_LIBRARY_SAVE_RESUME,
			.flags = 3,
			.label = LIT("Continue"),
			.path = LIT("games/neon-racer/saves/resume.save"),
			.created_unix_ms = 1786000000000,
			.updated_unix_ms = 1786200000000,
			.play_time_ms = 7342000,
		},
		{
			.id = LIT("after-boss-2"),
			.kind = APP_LIBRARY_SAVE_MANUAL,
			.label = LIT("After \"Boss\" 2"),
			.path = LIT("games/neon-racer/saves/after-boss-2.save"),
			.created_unix_ms = 1786150000000,
			.updated_unix_ms = 1786150000000,
			.play_time_ms = 6120000,
		},
	};
	App_LibraryGame games[] = {
		{
			.id = LIT("neon-racer"),
			.title = LIT("Neon Racer"),
			.developer = LIT("Vanta Works"),
			.release_year = 1989,
			.first_played_unix_ms = 1786000000000,
			.last_played_unix_ms = 1786200000000,
			.play_time_ms = 7342000,
			.cartridge = {
				.mapper = 9,
				.mirroring = APP_LIBRARY_MIRROR_VERTICAL,
				.prg_path = LIT("games/neon-racer/prg.bin"),
				.prg_size = KiB(128),
				.chr_path = LIT("games/neon-racer/chr.bin"),
				.chr_size = KiB(128),
			},
			.saves = neon_saves,
			.save_count = ArrayCount(neon_saves),
		},
		{
			.id = LIT("moon-temple"),
			.title = LIT("Moon Temple"),
			.cartridge = {
				.mapper = 0,
				.mirroring = APP_LIBRARY_MIRROR_HORIZONTAL,
				.prg_path = LIT("games/moon-temple/prg.bin"),
				.prg_size = KiB(16),
			},
		},
	};
	App_Library source_library = { .games = games, .game_count = ArrayCount(games) };

	elf_State *writer = elf_create_state();
	Assert(writer);
	app_library_push_elf(writer, &source_library);
	Assert(elf_push_value_source(writer, -1));
	elf_StrSlice source;
	Assert(elf_to_str(writer, -1, &source));

	elf_State *reader = elf_create_state();
	Assert(reader);
	Assert(elf_push_constant_expr(reader, "library.elf", source));
	Arena arena = arena_create(0, "app library test");
	App_Library decoded = {};
	Assert(app_library_read_elf(reader, -1, &arena, &decoded));
	Assert(decoded.game_count == source_library.game_count);
	for (u32 index = 0; index < decoded.game_count; index ++) app_library_assert_game_equal(&source_library.games[index], &decoded.games[index]);

	u64 arena_position = arena.position;
	App_Library previous = decoded;
	const char invalid_source[] =
		"{ version = 1, games = { {"
		"id = \"broken-game\", title = \"Broken Game\", first_played_unix_ms = 0, last_played_unix_ms = 0, play_time_ms = 0,"
		"cartridge = { mapper = 0, mirroring = \"horizontal\", prg_path = \"games/broken/prg.bin\", prg_size = 16384, chr_size = 0 },"
		"saves = { { id = \"resume\", kind = \"resume\", flags = 0, path = 42, created_unix_ms = 1, updated_unix_ms = 2, play_time_ms = 1 } }"
		"} } }";
	Assert(elf_push_constant_expr(reader, "invalid-library.elf", (elf_StrSlice) { (char *)invalid_source, sizeof(invalid_source) - 1 }));
	i32 reader_top = elf_get_top(reader);
	Assert(!app_library_read_elf(reader, -1, &arena, &decoded));
	Assert(elf_get_top(reader) == reader_top);
	Assert(arena.position == arena_position);
	Assert(decoded.games == previous.games && decoded.game_count == previous.game_count);

	arena_destroy(&arena);
	elf_destroy_state(reader);
	elf_destroy_state(writer);
	return 0;
}
