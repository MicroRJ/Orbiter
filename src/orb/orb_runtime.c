#include "orb_runtime.h"

static const u64 ORB_STORE_MAX_FILE_SIZE = MB(256);

static Orb_StoreResult orb_store_result(Orb_StoreStatus status)
{
	return (Orb_StoreResult) { .status = status };
}

static Str orb_store_title_from_path(Str path)
{
	u32 separator = str_find_last(path, LIT("/\\"));
	u32 first = separator == MAX_VALUE_U32 ? 0 : separator + 1;
	Str title = str_slice(path, first, path.size - first);
	u32 extension = str_find_last(title, LIT("."));
	return extension == MAX_VALUE_U32 ? title : str_slice(title, 0, extension);
}

static Orb_Save *orb_store_push_save(Orb_Store *store)
{
	Orb_Save *save = arena_push_zero(&store->arena, sizeof(*save));
	if (store->orb.last_save) store->orb.last_save->next = save;
	else                      store->orb.first_save = save;
	store->orb.last_save = save;
	store->orb.save_count ++;
	return save;
}

static void orb_store_load_descriptor(Orb_Store *store, Orb_Descriptor descriptor)
{
	Orb_Metadata metadata = descriptor.metadata;
	store->orb = (Orb) {
		.system = metadata.system,
		.content_hash = metadata.content_hash,
		.title = metadata.title,
		.source_path = metadata.source_path,
		.first_played_unix_ms = metadata.first_played_unix_ms,
		.last_played_unix_ms = metadata.last_played_unix_ms,
		.play_time_ms = metadata.play_time_ms,
	};
	Orb_Save *save = orb_store_push_save(store);
	*save = (Orb_Save) {
		.id = metadata.id,
		.kind = metadata.kind,
		.created_unix_ms = metadata.created_unix_ms,
		.updated_unix_ms = metadata.last_played_unix_ms,
		.play_time_ms = metadata.play_time_ms,
		.thumbnail = descriptor.thumbnail,
		.state = descriptor.state_chunk.data,
	};
}

static void orb_store_load_legacy_state(Orb_Store *store, Platform_File_Info info)
{
	u64 created = info.created_unix_ms > 0 ? (u64)info.created_unix_ms : 0;
	u64 updated = info.modified_unix_ms > 0 ? (u64)info.modified_unix_ms : created;
	store->orb = (Orb) {
		.system = ORB_SYSTEM_NES,
		.title = orb_store_title_from_path(store->path),
		.first_played_unix_ms = created,
		.last_played_unix_ms = updated,
	};
	Orb_Save *save = orb_store_push_save(store);
	Hash256 state_hash = sha256(store->source);
	memory_copy(save->id.bytes, state_hash.bytes, sizeof(save->id.bytes));
	save->kind = ORB_SAVE_RESUME;
	save->created_unix_ms = created;
	save->updated_unix_ms = updated;
	save->state = store->source;
}

void orb_store_init(Orb_Store *store)
{
	Assert(store);
	*store = (Orb_Store) { .arena = arena_create(0, "orb store") };
}

void orb_store_destroy(Orb_Store *store)
{
	if (!store) return;
	arena_destroy(&store->arena);
	*store = (Orb_Store) {};
}

Orb_StoreResult orb_store_load(Orb_Store *store, Str path)
{
	if (!store || !store->arena.memory || !path.text || !path.size) return orb_store_result(ORB_STORE_STATUS_INVALID_ARGUMENT);
	arena_reset(&store->arena);
	store->path = str_push_copy(&store->arena, path);
	store->source = (ByteSpan) {};
	store->orb = (Orb) {};
	store->source_kind = ORB_STORE_SOURCE_NONE;
	store->loaded = false;

	Platform_File_Info info = {};
	if (!platform_get_file_info(store->path.text, &info) || info.is_directory) return orb_store_result(ORB_STORE_STATUS_NOT_FOUND);
	if (!info.size || info.size > ORB_STORE_MAX_FILE_SIZE || info.size > store->arena.reserved_size - store->arena.position) return orb_store_result(ORB_STORE_STATUS_FILE_TOO_LARGE);

	Platform_File file = platform_access_file(store->path.text, PLATFORM_FILE_OPEN_EXISTING, PLATFORM_FILE_READ | PLATFORM_FILE_SHARE_READ | PLATFORM_FILE_SHARE_WRITE | PLATFORM_FILE_SHARE_DELETE);
	if (!platform_file_is_valid(file)) return orb_store_result(ORB_STORE_STATUS_READ_FAILED);
	u8 *data = arena_push_aligned(&store->arena, info.size, 1);
	u64 bytes_read = 0;
	b32 read = platform_read_file(file, data, info.size, &bytes_read) && bytes_read == info.size;
	platform_close_file(file);
	if (!read) return orb_store_result(ORB_STORE_STATUS_READ_FAILED);
	store->source = byte_span(data, info.size);

	Orb_Descriptor descriptor = {};
	Orb_Result parsed = orb_parse(store->source, &descriptor);
	if (parsed.status == ORB_STATUS_OK)
	{
		orb_store_load_descriptor(store, descriptor);
		store->source_kind = ORB_STORE_SOURCE_ORB;
	}
	else if (str_ends_nocase(store->path, LIT(".orbiter")))
	{
		orb_store_load_legacy_state(store, info);
		store->source_kind = ORB_STORE_SOURCE_LEGACY_STATE;
	}
	else
	{
		Orb_StoreResult result = orb_store_result(ORB_STORE_STATUS_INVALID_ORB);
		result.orb_result = parsed;
		return result;
	}
	store->loaded = true;
	return orb_store_result(ORB_STORE_STATUS_OK);
}

static void orb_store_hex(char *output, const u8 *bytes, u32 size)
{
	static const char digits[] = "0123456789abcdef";
	for (u32 index = 0; index < size; index ++)
	{
		output[index * 2 + 0] = digits[bytes[index] >> 4];
		output[index * 2 + 1] = digits[bytes[index] & 15];
	}
	output[size * 2] = 0;
}

void orb_store_log_info(const Orb_Store *store)
{
	if (!store || !store->loaded) return;
	const Orb *orb = &store->orb;
	char hash[sizeof(orb->content_hash.bytes) * 2 + 1];
	if (hash256_is_zero(orb->content_hash)) snprintf(hash, sizeof(hash), "unknown");
	else orb_store_hex(hash, orb->content_hash.bytes, sizeof(orb->content_hash.bytes));
	const char *source_kind = store->source_kind == ORB_STORE_SOURCE_ORB ? "ORB v1 container" : "legacy emulator state";
	LOG_INFO("orb store loaded '%.*s'", store->path.size, store->path.text);
	LOG_INFO("  source: %s, %llu bytes", source_kind, store->source.size);
	LOG_INFO("  title: %.*s", orb->title.size, orb->title.text ? orb->title.text : "");
	LOG_INFO("  system: %c%c%c%c", orb->system & 0xFF, orb->system >> 8 & 0xFF, orb->system >> 16 & 0xFF, orb->system >> 24 & 0xFF);
	LOG_INFO("  content: %s", hash);
	LOG_INFO("  played: first=%llu last=%llu total=%llu ms", orb->first_played_unix_ms, orb->last_played_unix_ms, orb->play_time_ms);
	LOG_INFO("  saves: %u", orb->save_count);
	u32 index = 0;
	for (const Orb_Save *save = orb->first_save; save; save = save->next, index ++)
	{
		char id[sizeof(save->id.bytes) * 2 + 1];
		orb_store_hex(id, save->id.bytes, sizeof(save->id.bytes));
		const char *kind = save->kind == ORB_SAVE_RESUME ? "resume" : save->kind == ORB_SAVE_MANUAL ? "manual" : "unknown";
		LOG_INFO("    [%u] %s id=%s state=%llu bytes created=%llu updated=%llu play=%llu ms", index, kind, id, save->state.size, save->created_unix_ms, save->updated_unix_ms, save->play_time_ms);
		if (save->thumbnail.pixels.size) LOG_INFO("        thumbnail=%ux%u stride=%u bytes=%llu", save->thumbnail.width, save->thumbnail.height, save->thumbnail.stride, save->thumbnail.pixels.size);
	}
}

const char *orb_store_status_string(Orb_StoreStatus status)
{
	switch (status)
	{
		case ORB_STORE_STATUS_OK:               return "ok";
		case ORB_STORE_STATUS_INVALID_ARGUMENT: return "invalid argument";
		case ORB_STORE_STATUS_NOT_FOUND:        return "file not found";
		case ORB_STORE_STATUS_FILE_TOO_LARGE:   return "file is empty or too large";
		case ORB_STORE_STATUS_READ_FAILED:      return "file read failed";
		case ORB_STORE_STATUS_INVALID_ORB:      return "invalid ORB";
	}
	return "unknown error";
}
