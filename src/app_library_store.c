#include "app_library_store.h"
#include "elf.h"

enum { APP_LIBRARY_STORE_GAME_CAPACITY = 1024 };

static ByteSpan app_library_store_read_file(Arena *arena, const char *path)
{
	u64 arena_position = arena->position;
	Platform_File file = platform_access_file(path, PLATFORM_FILE_OPEN_EXISTING, PLATFORM_FILE_READ | PLATFORM_FILE_SHARE_READ);
	if (!platform_file_is_valid(file)) return (ByteSpan) {};
	u64 size = 0;
	b32 success = platform_get_file_size(file, &size) && size < arena->reserved_size - arena->position;
	u8 *data = 0;
	if (success)
	{
		data = arena_push_aligned(arena, size + 1, 1);
		u64 bytes_read = 0;
		success = (!size || platform_read_file(file, data, size, &bytes_read)) && bytes_read == size;
		if (success) data[size] = 0;
	}
	platform_close_file(file);
	if (!success)
	{
		arena->position = arena_position;
		return (ByteSpan) {};
	}
	return byte_span(data, size);
}

static b32 app_library_store_write_file(const char *path, ByteSpan data)
{
	Platform_File file = platform_access_file(path, PLATFORM_FILE_CREATE_ALWAYS, PLATFORM_FILE_WRITE);
	if (!platform_file_is_valid(file)) return false;
	u64 written = 0;
	b32 success = (!data.size || platform_write_file(file, data.data, data.size, &written)) && written == data.size;
	platform_close_file(file);
	return success;
}

static b32 app_library_store_write_file_atomic(Arena *scratch, const char *path, ByteSpan data)
{
	u64 arena_position = scratch->position;
	Str temporary_path = str_push_copy_f(scratch, "%s.tmp", path);
	b32 success = app_library_store_write_file(temporary_path.data, data) && platform_move_file(temporary_path.data, path, true);
	if (!success) platform_remove_file(temporary_path.data);
	scratch->position = arena_position;
	return success;
}

static b32 app_library_store_path_valid(Str path)
{
	if (!path.size || !path.data || path.data[0] == '/' || path.data[0] == '\\') return false;
	if (path.size >= 2 && path.data[1] == ':') return false;
	for (u32 index = 0; index < path.size;)
	{
		u32 begin = index;
		while (index < path.size && path.data[index] != '/' && path.data[index] != '\\') index ++;
		Str part = str_slice(path, begin, index - begin);
		if (!part.size || str_match(part, LIT(".")) || str_match(part, LIT(".."))) return false;
		index ++;
	}
	return true;
}

static Str app_library_store_resolve(Arena *arena, const App_LibraryStore *store, Str relative)
{
	if (!app_library_store_path_valid(relative)) return (Str) {};
	Str result = str_push_copy_f(arena, "%.*s\\%.*s", store->directory.size, store->directory.data, relative.size, relative.data);
	for (u32 index = store->directory.size + 1; index < result.size; index ++) if (result.data[index] == '/') result.data[index] = '\\';
	return result;
}

static Str app_library_store_parent(Arena *arena, Str path)
{
	u32 slash = path.size;
	while (slash && path.data[slash - 1] != '/' && path.data[slash - 1] != '\\') slash --;
	if (!slash) return LIT(".");
	while (slash > 1 && (path.data[slash - 1] == '/' || path.data[slash - 1] == '\\')) slash --;
	return str_push_copy(arena, str_slice(path, 0, slash));
}

static b32 app_library_store_path_matches(Str path, const char *format, Str game_id, Str save_id)
{
	char expected[1024];
	i32 size = snprintf(expected, sizeof(expected), format, (i32)game_id.size, game_id.data, (i32)save_id.size, save_id.data);
	return size >= 0 && (u32)size < sizeof(expected) && str_match(path, str_from_data(expected, (u32)size));
}

static b32 app_library_store_paths_valid(const App_Library *library)
{
	for (u32 game_index = 0; game_index < library->game_count; game_index ++)
	{
		const App_LibraryGame *game = &library->games[game_index];
		const App_LibraryCartridge *cartridge = &game->cartridge;
		if (!app_library_store_path_matches(cartridge->prg_path, "games/%.*s/prg.bin", game->id, (Str) {})) return false;
		if (cartridge->chr_path.size && !app_library_store_path_matches(cartridge->chr_path, "games/%.*s/chr.bin", game->id, (Str) {})) return false;
		if (cartridge->trainer_path.size && !app_library_store_path_matches(cartridge->trainer_path, "games/%.*s/trainer.bin", game->id, (Str) {})) return false;
		for (u32 save_index = 0; save_index < game->save_count; save_index ++)
		{
			const App_LibrarySave *save = &game->saves[save_index];
			if (!app_library_store_path_matches(save->path, "games/%.*s/saves/%.*s.save", game->id, save->id)) return false;
		}
	}
	return true;
}

static void app_library_store_prepare_games(App_LibraryStore *store)
{
	Assert(store->library.game_count <= APP_LIBRARY_STORE_GAME_CAPACITY);
	App_LibraryGame *games = arena_push_zero(&store->arena, sizeof(*games) * APP_LIBRARY_STORE_GAME_CAPACITY);
	if (store->library.game_count) memory_copy(games, store->library.games, sizeof(*games) * store->library.game_count);
	store->library.games = games;
	store->game_capacity = APP_LIBRARY_STORE_GAME_CAPACITY;
}

App_LibraryStore *app_library_store_open(const char *manifest_path)
{
	Assert(manifest_path);
	App_LibraryStore *store = calloc(1, sizeof(*store));
	if (!store) return 0;
	store->arena = arena_create(0, "app library store");
	store->manifest_path = str_push_copy(&store->arena, str_from_cstr(manifest_path));
	store->directory = app_library_store_parent(&store->arena, store->manifest_path);
	Platform_File_Info manifest_info;
	if (!platform_get_file_info(store->manifest_path.data, &manifest_info))
	{
		app_library_store_prepare_games(store);
		Arena scratch = arena_create(0, "empty app library");
		b32 created = platform_create_directories(store->directory.data) && app_library_store_write_manifest(store, &scratch);
		arena_destroy(&scratch);
		if (created) return store;
		app_library_store_close(store);
		return 0;
	}
	ByteSpan source = app_library_store_read_file(&store->arena, store->manifest_path.data);
	elf_State *state = elf_create_state();
	b32 success = source.data && state && elf_push_constant_expr(state, store->manifest_path.data, (elf_StrSlice) { (char *)source.data, source.size });
	if (success) success = app_library_read_elf(state, -1, &store->arena, &store->library);
	if (success) success = app_library_store_paths_valid(&store->library);
	if (state) elf_destroy_state(state);
	if (!success)
	{
		app_library_store_close(store);
		return 0;
	}
	app_library_store_prepare_games(store);
	return store;
}

void app_library_store_close(App_LibraryStore *store)
{
	if (!store) return;
	arena_destroy(&store->arena);
	free(store);
}

b32 app_library_store_read_save(App_LibraryStore *store, Arena *arena, const App_LibrarySave *save, App_Save *data)
{
	Assert(store && arena && save && data);
	u64 arena_position = arena->position;
	Str save_path = app_library_store_resolve(arena, store, save->path);
	ByteSpan encoded = save_path.data ? app_library_store_read_file(arena, save_path.data) : (ByteSpan) {};
	if (encoded.data && app_save_decode(arena, encoded, data)) return true;
	arena->position = arena_position;
	return false;
}


static b32 app_library_store_game_memory_is_valid(NES_Game game)
{
	return (!game.metadata.trainer_size || game.trainer) && (!game.metadata.prg_rom_size || game.prg_rom) &&
		(!game.metadata.chr_rom_size || game.chr_rom);
}

static void app_library_store_hash_u32(SHA256_Context *context, u32 value)
{
	u8 bytes[4];
	for (u32 index = 0; index < sizeof(bytes); index ++) bytes[index] = (u8)(value >> (index * 8));
	sha256_update(context, byte_span(bytes, sizeof(bytes)));
}

static Hash256 app_library_store_game_hash(NES_Game game)
{
	Assert(app_library_store_game_memory_is_valid(game));
	static const u8 domain[] = "ORB_GAME_1";
	SHA256_Context context;
	sha256_init(&context);
	sha256_update(&context, byte_span((void *)domain, sizeof(domain) - 1));
	app_library_store_hash_u32(&context, game.metadata.mapper);
	app_library_store_hash_u32(&context, game.metadata.mirroring == NES_MIRROR_VERTICAL);
	app_library_store_hash_u32(&context, !!game.metadata.trainer_size);
	app_library_store_hash_u32(&context, game.metadata.mirroring == NES_MIRROR_FOUR_SCREEN);
	app_library_store_hash_u32(&context, game.metadata.prg_rom_size);
	app_library_store_hash_u32(&context, game.metadata.chr_rom_size);
	sha256_update(&context, byte_span((void *)game.trainer, game.metadata.trainer_size));
	sha256_update(&context, byte_span((void *)game.prg_rom, game.metadata.prg_rom_size));
	sha256_update(&context, byte_span((void *)game.chr_rom, game.metadata.chr_rom_size));
	return sha256_final(&context);
}

b32 app_library_store_read_game(App_LibraryStore *store, Arena *arena, const App_LibraryGame *game, const App_LibrarySave *save, App_LibraryGameData *data)
{
	Assert(store && arena && game && save && data);
	u64 arena_position = arena->position;
	App_LibraryGameData result = {};
	const NES_GameMetadata *metadata = &game->cartridge.metadata;
	Str prg_path = app_library_store_resolve(arena, store, game->cartridge.prg_path);
	ByteSpan prg = prg_path.data ? app_library_store_read_file(arena, prg_path.data) : (ByteSpan) {};
	if (!prg.data || prg.size != metadata->prg_rom_size) goto failed;
	ByteSpan chr = {};
	if (metadata->chr_rom_size)
	{
		Str chr_path = app_library_store_resolve(arena, store, game->cartridge.chr_path);
		chr = chr_path.data ? app_library_store_read_file(arena, chr_path.data) : (ByteSpan) {};
		if (!chr.data || chr.size != metadata->chr_rom_size) goto failed;
	}
	ByteSpan trainer = {};
	if (game->cartridge.trainer_path.size)
	{
		Str trainer_path = app_library_store_resolve(arena, store, game->cartridge.trainer_path);
		trainer = trainer_path.data ? app_library_store_read_file(arena, trainer_path.data) : (ByteSpan) {};
		if (!trainer.data || trainer.size != metadata->trainer_size) goto failed;
	}
	if (!app_library_store_read_save(store, arena, save, &result.save)) goto failed;
	result.game = (NES_Game) {
		.metadata = *metadata,
		.trainer = trainer.data,
		.prg_rom = prg.data,
		.chr_rom = chr.data,
	};
	Hash256 hash = app_library_store_game_hash(result.game);
	static const char hex[] = "0123456789abcdef";
	if (game->id.size != sizeof(hash.bytes) * 2) goto failed;
	for (u32 index = 0; index < sizeof(hash.bytes); index ++)
	{
		if (game->id.data[index * 2 + 0] != hex[hash.bytes[index] >> 4]) goto failed;
		if (game->id.data[index * 2 + 1] != hex[hash.bytes[index] & 15]) goto failed;
	}
	*data = result;
	return true;

failed:
	arena->position = arena_position;
	return false;
}

b32 app_library_store_write_save(App_LibraryStore *store, Arena *scratch, const App_LibrarySave *save, const App_Save *data)
{
	Assert(store && scratch && save && data);
	u64 arena_position = scratch->position;
	Str path = app_library_store_resolve(scratch, store, save->path);
	ByteSpan encoded = path.data ? app_save_encode(scratch, data) : (ByteSpan) {};
	b32 success = encoded.data && app_library_store_write_file_atomic(scratch, path.data, encoded);
	scratch->position = arena_position;
	return success;
}

b32 app_library_store_write_manifest(App_LibraryStore *store, Arena *scratch)
{
	Assert(store && scratch);
	u64 arena_position = scratch->position;
	elf_State *state = elf_create_state();
	if (!state) return false;
	app_library_push_elf(state, &store->library);
	b32 success = elf_push_value_source(state, -1);
	elf_StrSlice source = {};
	if (success) success = elf_to_str(state, -1, &source);
	if (success) success = app_library_store_write_file_atomic(scratch, store->manifest_path.data, byte_span(source.data, source.size));
	elf_destroy_state(state);
	scratch->position = arena_position;
	return success;
}

static Str app_library_store_hash_string(Arena *arena, Hash256 hash)
{
	static const char hex[] = "0123456789abcdef";
	char *text = arena_push_aligned(arena, sizeof(hash.bytes) * 2 + 1, 1);
	for (u32 index = 0; index < sizeof(hash.bytes); index ++)
	{
		text[index * 2 + 0] = hex[hash.bytes[index] >> 4];
		text[index * 2 + 1] = hex[hash.bytes[index] & 15];
	}
	text[sizeof(hash.bytes) * 2] = 0;
	return str_from_data(text, sizeof(hash.bytes) * 2);
}

b32 app_library_store_import_game(App_LibraryStore *store, Arena *scratch, NES_Game source_game, Str title, const App_Save *save_data,
	App_LibraryGame **game, App_LibrarySave **save)
{
	Assert(store && scratch && save_data && game && save);
	Hash256 hash = app_library_store_game_hash(source_game);
	Str game_id = app_library_store_hash_string(scratch, hash);
	for (u32 index = 0; index < store->library.game_count; index ++)
	{
		App_LibraryGame *existing = &store->library.games[index];
		if (!str_match(existing->id, game_id)) continue;
		for (u32 save_index = 0; save_index < existing->save_count; save_index ++)
		{
			if (existing->saves[save_index].kind != APP_LIBRARY_SAVE_RESUME) continue;
			*game = existing;
			*save = &existing->saves[save_index];
			return true;
		}
		return false;
	}
	if (store->library.game_count >= store->game_capacity || !title.size || title.size > 256) return false;

	u64 arena_position = store->arena.position;
	u32 old_count = store->library.game_count;
	App_LibraryGame *added = &store->library.games[old_count];
	Assert(!added->id.data && !added->title.data && !added->saves);
	added->id = str_push_copy(&store->arena, game_id);
	added->title = str_push_copy(&store->arena, title);
	added->cartridge = (App_LibraryCartridge) {
		.metadata = source_game.metadata,
		.prg_path = str_push_copy_f(&store->arena, "games/%.*s/prg.bin", added->id.size, added->id.data),
	};
	if (source_game.metadata.chr_rom_size) added->cartridge.chr_path = str_push_copy_f(&store->arena, "games/%.*s/chr.bin", added->id.size, added->id.data);
	if (source_game.metadata.trainer_size) added->cartridge.trainer_path = str_push_copy_f(&store->arena, "games/%.*s/trainer.bin", added->id.size, added->id.data);
	added->saves = arena_push_zero(&store->arena, sizeof(*added->saves));
	added->save_count = 1;
	u64 now = (u64)Max(platform_unix_time_ms(), 0);
	*added->saves = (App_LibrarySave) {
		.id = str_push_copy(&store->arena, LIT("resume")),
		.kind = APP_LIBRARY_SAVE_RESUME,
		.path = str_push_copy_f(&store->arena, "games/%.*s/saves/resume.save", added->id.size, added->id.data),
		.created_unix_ms = now,
		.updated_unix_ms = now,
	};
	added->first_played_unix_ms = now;
	added->last_played_unix_ms = now;

	u64 scratch_position = scratch->position;
	Str prg_path = app_library_store_resolve(scratch, store, added->cartridge.prg_path);
	Str game_directory = app_library_store_parent(scratch, prg_path);
	Str save_path = app_library_store_resolve(scratch, store, added->saves[0].path);
	Str save_directory = app_library_store_parent(scratch, save_path);
	b32 success = platform_create_directories(game_directory.data) && platform_create_directories(save_directory.data);
	if (success) success = app_library_store_write_file_atomic(scratch, prg_path.data, byte_span((void *)source_game.prg_rom, source_game.metadata.prg_rom_size));
	if (success && source_game.metadata.chr_rom_size)
	{
		Str chr_path = app_library_store_resolve(scratch, store, added->cartridge.chr_path);
		success = app_library_store_write_file_atomic(scratch, chr_path.data, byte_span((void *)source_game.chr_rom, source_game.metadata.chr_rom_size));
	}
	if (success && source_game.metadata.trainer_size)
	{
		Str trainer_path = app_library_store_resolve(scratch, store, added->cartridge.trainer_path);
		success = app_library_store_write_file_atomic(scratch, trainer_path.data, byte_span((void *)source_game.trainer, source_game.metadata.trainer_size));
	}
	if (success) success = app_library_store_write_save(store, scratch, &added->saves[0], save_data);
	store->library.game_count = old_count + 1;
	if (success) success = app_library_store_write_manifest(store, scratch);
	if (!success)
	{
		store->library.game_count = old_count;
		memory_zero(added, sizeof(*added));
		store->arena.position = arena_position;
		scratch->position = scratch_position;
		return false;
	}
	*game = added;
	*save = added->saves;
	scratch->position = scratch_position;
	return true;
}
