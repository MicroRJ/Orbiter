#include "app_library.h"
#include "elf.h"

enum
{
	APP_LIBRARY_VERSION = 1,
	APP_LIBRARY_MAX_GAME_COUNT = 1024,
	APP_LIBRARY_MAX_TOTAL_SAVE_COUNT = 4096,
	APP_LIBRARY_MAX_ID_SIZE = 64,
	APP_LIBRARY_MAX_TEXT_SIZE = 256,
	APP_LIBRARY_MAX_PATH_SIZE = 1024,
};

#define APP_LIBRARY_MAX_INTEGER 0x7fffffffffffffffULL

static b32 app_library_id_valid(Str id)
{
	if (!id.size || id.size > APP_LIBRARY_MAX_ID_SIZE) return false;
	for (u32 index = 0; index < id.size; index ++)
	{
		char c = id.data[index];
		b32 alpha = c >= 'a' && c <= 'z';
		b32 digit = c >= '0' && c <= '9';
		if (!alpha && !digit && (index == 0 || (c != '-' && c != '_' && c != '.'))) return false;
	}
	return true;
}

static b32 app_library_string_valid(Str value, u32 max_size, b32 required)
{
	if (required && !value.size) return false;
	return value.size <= max_size && (value.data || !value.size);
}

static b32 app_library_save_valid(const App_LibrarySave *save)
{
	if (!app_library_id_valid(save->id)) return false;
	if (save->kind != APP_LIBRARY_SAVE_RESUME && save->kind != APP_LIBRARY_SAVE_MANUAL) return false;
	if (!app_library_string_valid(save->label, APP_LIBRARY_MAX_TEXT_SIZE, false)) return false;
	if (!app_library_string_valid(save->path, APP_LIBRARY_MAX_PATH_SIZE, true)) return false;
	return save->flags <= APP_LIBRARY_MAX_INTEGER && save->created_unix_ms <= APP_LIBRARY_MAX_INTEGER &&
		save->updated_unix_ms <= APP_LIBRARY_MAX_INTEGER && save->play_time_ms <= APP_LIBRARY_MAX_INTEGER &&
		save->updated_unix_ms >= save->created_unix_ms;
}

static b32 app_library_cartridge_valid(const App_LibraryCartridge *cartridge)
{
	const NES_GameMetadata *metadata = &cartridge->metadata;
	if (metadata->mirroring < NES_MIRROR_HORIZONTAL || metadata->mirroring > NES_MIRROR_FOUR_SCREEN) return false;
	if (!app_library_string_valid(cartridge->trainer_path, APP_LIBRARY_MAX_PATH_SIZE, false)) return false;
	if (!app_library_string_valid(cartridge->prg_path, APP_LIBRARY_MAX_PATH_SIZE, true) || !metadata->prg_rom_size) return false;
	if (!app_library_string_valid(cartridge->chr_path, APP_LIBRARY_MAX_PATH_SIZE, metadata->chr_rom_size != 0)) return false;
	if (metadata->trainer_size != (cartridge->trainer_path.size ? 512 : 0)) return false;
	return metadata->chr_rom_size || !cartridge->chr_path.size;
}

static b32 app_library_game_valid(const App_LibraryGame *game, u32 *total_save_count)
{
	if (!app_library_id_valid(game->id)) return false;
	if (!app_library_string_valid(game->title, APP_LIBRARY_MAX_TEXT_SIZE, true)) return false;
	if (!app_library_string_valid(game->developer, APP_LIBRARY_MAX_TEXT_SIZE, false)) return false;
	if (game->first_played_unix_ms > APP_LIBRARY_MAX_INTEGER || game->last_played_unix_ms > APP_LIBRARY_MAX_INTEGER ||
		game->play_time_ms > APP_LIBRARY_MAX_INTEGER) return false;
	if (game->last_played_unix_ms < game->first_played_unix_ms) return false;
	if (!app_library_cartridge_valid(&game->cartridge)) return false;
	if (game->save_count > APP_LIBRARY_MAX_TOTAL_SAVE_COUNT - *total_save_count || (!game->saves && game->save_count)) return false;
	*total_save_count += game->save_count;

	u32 resume_count = 0;
	for (u32 index = 0; index < game->save_count; index ++)
	{
		const App_LibrarySave *save = &game->saves[index];
		if (!app_library_save_valid(save)) return false;
		for (u32 previous = 0; previous < index; previous ++) if (str_match(game->saves[previous].id, save->id)) return false;
		resume_count += save->kind == APP_LIBRARY_SAVE_RESUME;
	}
	return resume_count <= 1;
}

static b32 app_library_valid(const App_Library *library)
{
	if (library->game_count > APP_LIBRARY_MAX_GAME_COUNT || (!library->games && library->game_count)) return false;
	u32 total_save_count = 0;
	for (u32 index = 0; index < library->game_count; index ++)
	{
		const App_LibraryGame *game = &library->games[index];
		if (!app_library_game_valid(game, &total_save_count)) return false;
		for (u32 previous = 0; previous < index; previous ++) if (str_match(library->games[previous].id, game->id)) return false;
	}
	return true;
}

static void app_library_set_string(elf_State *state, i32 table, const char *field, Str value)
{
	Assert(value.data || !value.size);
	Assert(value.size <= 0x7FFFFFFF);
	elf_push_str(state, value.data, (i32)value.size);
	Assert(elf_set_field(state, table, field));
}

static void app_library_set_integer(elf_State *state, i32 table, const char *field, u64 value)
{
	Assert(value <= 0x7FFFFFFFFFFFFFFF);
	elf_push_int(state, (elf_Integer)value);
	Assert(elf_set_field(state, table, field));
}

static const char *app_library_save_kind_name(App_LibrarySaveKind kind)
{
	switch (kind)
	{
		case APP_LIBRARY_SAVE_RESUME: return "resume";
		case APP_LIBRARY_SAVE_MANUAL: return "manual";
		default: Assert(!"invalid library save kind"); return "";
	}
}

static const char *app_library_mirroring_name(NES_Mirroring mirroring)
{
	switch (mirroring)
	{
		case NES_MIRROR_HORIZONTAL: return "horizontal";
		case NES_MIRROR_VERTICAL: return "vertical";
		case NES_MIRROR_FOUR_SCREEN: return "four_screen";
		default: Assert(!"invalid library mirroring"); return "";
	}
}

static void app_library_save_push_elf(elf_State *state, const App_LibrarySave *save)
{
	elf_new_table(state);
	i32 table = elf_abs_index(state, -1);
	app_library_set_string(state, table, "id", save->id);
	app_library_set_string(state, table, "kind", str_from_cstr(app_library_save_kind_name(save->kind)));
	app_library_set_integer(state, table, "flags", save->flags);
	if (save->label.size) app_library_set_string(state, table, "label", save->label);
	app_library_set_string(state, table, "path", save->path);
	app_library_set_integer(state, table, "created_unix_ms", save->created_unix_ms);
	app_library_set_integer(state, table, "updated_unix_ms", save->updated_unix_ms);
	app_library_set_integer(state, table, "play_time_ms", save->play_time_ms);
}

static void app_library_game_push_elf(elf_State *state, const App_LibraryGame *game)
{
	elf_new_table(state);
	i32 table = elf_abs_index(state, -1);
	app_library_set_string(state, table, "id", game->id);
	app_library_set_string(state, table, "title", game->title);
	if (game->developer.size) app_library_set_string(state, table, "developer", game->developer);
	if (game->release_year) app_library_set_integer(state, table, "release_year", game->release_year);
	app_library_set_integer(state, table, "first_played_unix_ms", game->first_played_unix_ms);
	app_library_set_integer(state, table, "last_played_unix_ms", game->last_played_unix_ms);
	app_library_set_integer(state, table, "play_time_ms", game->play_time_ms);

	const App_LibraryCartridge *cartridge = &game->cartridge;
	const NES_GameMetadata *metadata = &cartridge->metadata;
	elf_new_table(state);
	i32 cartridge_table = elf_abs_index(state, -1);
	app_library_set_integer(state, cartridge_table, "mapper", metadata->mapper);
	app_library_set_string(state, cartridge_table, "mirroring", str_from_cstr(app_library_mirroring_name(metadata->mirroring)));
	if (cartridge->trainer_path.size) app_library_set_string(state, cartridge_table, "trainer_path", cartridge->trainer_path);
	app_library_set_string(state, cartridge_table, "prg_path", cartridge->prg_path);
	app_library_set_integer(state, cartridge_table, "prg_size", metadata->prg_rom_size);
	if (cartridge->chr_path.size) app_library_set_string(state, cartridge_table, "chr_path", cartridge->chr_path);
	app_library_set_integer(state, cartridge_table, "chr_size", metadata->chr_rom_size);
	Assert(elf_set_field(state, table, "cartridge"));

	elf_new_table(state);
	i32 saves = elf_abs_index(state, -1);
	for (u32 index = 0; index < game->save_count; index ++)
	{
		app_library_save_push_elf(state, &game->saves[index]);
		Assert(elf_append(state, saves));
	}
	Assert(elf_set_field(state, table, "saves"));
}

void app_library_push_elf(elf_State *state, const App_Library *library)
{
	Assert(state && library);
	Assert(app_library_valid(library));
	i32 top = elf_get_top(state);
	elf_new_table(state);
	i32 table = elf_abs_index(state, -1);
	app_library_set_integer(state, table, "version", APP_LIBRARY_VERSION);

	elf_new_table(state);
	i32 games = elf_abs_index(state, -1);
	for (u32 index = 0; index < library->game_count; index ++)
	{
		app_library_game_push_elf(state, &library->games[index]);
		Assert(elf_append(state, games));
	}
	Assert(elf_set_field(state, table, "games"));
	Assert(elf_get_top(state) == top + 1);
}

static b32 app_library_push_table_field(elf_State *state, i32 table, const char *field)
{
	Assert(elf_get_field(state, table, field));
	if (elf_type(state, -1) == ELF_VALUE_TYPE_TABLE) return true;
	Assert(elf_pop(state, 1));
	return false;
}

static b32 app_library_read_string(elf_State *state, i32 table, const char *field, Arena *arena, u32 max_size, b32 optional, Str *result)
{
	Assert(result);
	*result = (Str) {};
	Assert(elf_get_field(state, table, field));
	if (optional && elf_is_nil(state, -1))
	{
		Assert(elf_pop(state, 1));
		return true;
	}

	elf_StrSlice value;
	b32 valid = elf_to_str(state, -1, &value) && value.size <= max_size;
	if (valid) *result = str_push_copy(arena, str_from_data(value.data, (u32)value.size));
	Assert(elf_pop(state, 1));
	return valid;
}

static b32 app_library_read_integer(elf_State *state, i32 table, const char *field, b32 optional, u64 *result)
{
	Assert(result);
	*result = 0;
	Assert(elf_get_field(state, table, field));
	if (optional && elf_is_nil(state, -1))
	{
		Assert(elf_pop(state, 1));
		return true;
	}

	elf_Integer value;
	b32 valid = elf_to_int(state, -1, &value) && value >= 0;
	if (valid) *result = (u64)value;
	Assert(elf_pop(state, 1));
	return valid;
}

static b32 app_library_read_save_kind(elf_State *state, i32 table, App_LibrarySaveKind *kind)
{
	Assert(elf_get_field(state, table, "kind"));
	elf_StrSlice string;
	b32 valid = elf_to_str(state, -1, &string) && string.size <= MAX_VALUE_U32;
	Str value = valid ? str_from_data(string.data, (u32)string.size) : (Str) {};
	Assert(elf_pop(state, 1));
	if (str_match(value, LIT("resume"))) *kind = APP_LIBRARY_SAVE_RESUME;
	else if (str_match(value, LIT("manual"))) *kind = APP_LIBRARY_SAVE_MANUAL;
	else return false;
	return true;
}

static b32 app_library_read_mirroring(elf_State *state, i32 table, NES_Mirroring *mirroring)
{
	Assert(elf_get_field(state, table, "mirroring"));
	elf_StrSlice string;
	b32 valid = elf_to_str(state, -1, &string) && string.size <= MAX_VALUE_U32;
	Str value = valid ? str_from_data(string.data, (u32)string.size) : (Str) {};
	Assert(elf_pop(state, 1));
	if (str_match(value, LIT("horizontal"))) *mirroring = NES_MIRROR_HORIZONTAL;
	else if (str_match(value, LIT("vertical"))) *mirroring = NES_MIRROR_VERTICAL;
	else if (str_match(value, LIT("four_screen"))) *mirroring = NES_MIRROR_FOUR_SCREEN;
	else return false;
	return true;
}

static b32 app_library_read_cartridge(elf_State *state, i32 index, Arena *arena, App_LibraryCartridge *cartridge)
{
	if (elf_type(state, index) != ELF_VALUE_TYPE_TABLE) return false;
	i32 table = elf_abs_index(state, index);
	u64 mapper;
	if (!app_library_read_integer(state, table, "mapper", false, &mapper) || mapper > MAX_VALUE_U32) return false;
	cartridge->metadata.mapper = (u32)mapper;
	if (!app_library_read_mirroring(state, table, &cartridge->metadata.mirroring)) return false;
	if (!app_library_read_string(state, table, "trainer_path", arena, APP_LIBRARY_MAX_PATH_SIZE, true, &cartridge->trainer_path)) return false;
	cartridge->metadata.trainer_size = cartridge->trainer_path.size ? 512 : 0;
	if (!app_library_read_string(state, table, "prg_path", arena, APP_LIBRARY_MAX_PATH_SIZE, false, &cartridge->prg_path)) return false;
	u64 prg_size;
	if (!app_library_read_integer(state, table, "prg_size", false, &prg_size) || prg_size > MAX_VALUE_U32) return false;
	cartridge->metadata.prg_rom_size = (u32)prg_size;
	if (!app_library_read_string(state, table, "chr_path", arena, APP_LIBRARY_MAX_PATH_SIZE, true, &cartridge->chr_path)) return false;
	u64 chr_size;
	if (!app_library_read_integer(state, table, "chr_size", false, &chr_size) || chr_size > MAX_VALUE_U32) return false;
	cartridge->metadata.chr_rom_size = (u32)chr_size;
	return true;
}

static b32 app_library_read_save(elf_State *state, i32 index, Arena *arena, App_LibrarySave *save)
{
	if (elf_type(state, index) != ELF_VALUE_TYPE_TABLE) return false;
	i32 table = elf_abs_index(state, index);
	if (!app_library_read_string(state, table, "id", arena, APP_LIBRARY_MAX_ID_SIZE, false, &save->id)) return false;
	if (!app_library_read_save_kind(state, table, &save->kind)) return false;
	if (!app_library_read_integer(state, table, "flags", false, &save->flags)) return false;
	if (!app_library_read_string(state, table, "label", arena, APP_LIBRARY_MAX_TEXT_SIZE, true, &save->label)) return false;
	if (!app_library_read_string(state, table, "path", arena, APP_LIBRARY_MAX_PATH_SIZE, false, &save->path)) return false;
	if (!app_library_read_integer(state, table, "created_unix_ms", false, &save->created_unix_ms)) return false;
	if (!app_library_read_integer(state, table, "updated_unix_ms", false, &save->updated_unix_ms)) return false;
	if (!app_library_read_integer(state, table, "play_time_ms", false, &save->play_time_ms)) return false;
	return true;
}

static b32 app_library_read_game(elf_State *state, i32 index, Arena *arena, u32 *total_save_count, App_LibraryGame *game)
{
	if (elf_type(state, index) != ELF_VALUE_TYPE_TABLE) return false;
	i32 table = elf_abs_index(state, index);
	if (!app_library_read_string(state, table, "id", arena, APP_LIBRARY_MAX_ID_SIZE, false, &game->id)) return false;
	if (!app_library_read_string(state, table, "title", arena, APP_LIBRARY_MAX_TEXT_SIZE, false, &game->title)) return false;
	if (!app_library_read_string(state, table, "developer", arena, APP_LIBRARY_MAX_TEXT_SIZE, true, &game->developer)) return false;
	u64 release_year;
	if (!app_library_read_integer(state, table, "release_year", true, &release_year) || release_year > MAX_VALUE_U32) return false;
	game->release_year = (u32)release_year;
	if (!app_library_read_integer(state, table, "first_played_unix_ms", false, &game->first_played_unix_ms)) return false;
	if (!app_library_read_integer(state, table, "last_played_unix_ms", false, &game->last_played_unix_ms)) return false;
	if (!app_library_read_integer(state, table, "play_time_ms", false, &game->play_time_ms)) return false;
	if (!app_library_push_table_field(state, table, "cartridge")) return false;
	b32 cartridge_valid = app_library_read_cartridge(state, -1, arena, &game->cartridge);
	Assert(elf_pop(state, 1));
	if (!cartridge_valid) return false;

	if (!app_library_push_table_field(state, table, "saves")) return false;
	i32 saves = elf_abs_index(state, -1);
	if (!elf_length(state, saves, &game->save_count) || game->save_count > APP_LIBRARY_MAX_TOTAL_SAVE_COUNT - *total_save_count)
	{
		Assert(elf_pop(state, 1));
		return false;
	}
	*total_save_count += game->save_count;
	game->saves = arena_push_zero(arena, sizeof(*game->saves) * game->save_count);
	for (u32 save_index = 0; save_index < game->save_count; save_index ++)
	{
		Assert(elf_get_index(state, saves, save_index));
		b32 valid = app_library_read_save(state, -1, arena, &game->saves[save_index]);
		Assert(elf_pop(state, 1));
		if (!valid)
		{
			Assert(elf_pop(state, 1));
			return false;
		}
	}
	Assert(elf_pop(state, 1));
	return true;
}

static b32 app_library_read_elf_impl(elf_State *state, i32 index, Arena *arena, App_Library *library)
{
	if (elf_type(state, index) != ELF_VALUE_TYPE_TABLE) return false;
	i32 table = elf_abs_index(state, index);
	u64 version;
	if (!app_library_read_integer(state, table, "version", false, &version) || version != APP_LIBRARY_VERSION) return false;
	if (!app_library_push_table_field(state, table, "games")) return false;
	i32 games = elf_abs_index(state, -1);
	if (!elf_length(state, games, &library->game_count) || library->game_count > APP_LIBRARY_MAX_GAME_COUNT)
	{
		Assert(elf_pop(state, 1));
		return false;
	}
	library->games = arena_push_zero(arena, sizeof(*library->games) * library->game_count);
	u32 total_save_count = 0;
	for (u32 game_index = 0; game_index < library->game_count; game_index ++)
	{
		Assert(elf_get_index(state, games, game_index));
		b32 valid = app_library_read_game(state, -1, arena, &total_save_count, &library->games[game_index]);
		Assert(elf_pop(state, 1));
		if (!valid)
		{
			Assert(elf_pop(state, 1));
			return false;
		}
	}
	Assert(elf_pop(state, 1));
	return app_library_valid(library);
}

b32 app_library_read_elf(elf_State *state, i32 index, Arena *arena, App_Library *library)
{
	Assert(state && arena && library);
	i32 top = elf_get_top(state);
	u64 arena_start = arena->position;
	App_Library result = {};
	b32 valid = app_library_read_elf_impl(state, index, arena, &result);
	Assert(elf_get_top(state) == top);
	if (!valid)
	{
		arena->position = arena_start;
		return false;
	}
	*library = result;
	return true;
}
