#include "orb_runtime.h"

static const u64 ORB_STORE_MAX_FILE_SIZE = MB(256);

static Orb_StoreResult orb_store_result(Orb_StoreStatus status)
{
	return (Orb_StoreResult) { .status = status };
}

static Orb_Save *orb_runtime_push_save(Arena *arena, Orb *orb)
{
	if (!arena || !arena->memory || !orb || arena->position > arena->reserved_size) return 0;
	u64 address = (u64)(uintptr_t)arena->memory + arena->position;
	u64 padding = (ARENA_DEFAULT_ALIGNMENT - (address & (ARENA_DEFAULT_ALIGNMENT - 1))) & (ARENA_DEFAULT_ALIGNMENT - 1);
	u64 remaining = arena->reserved_size - arena->position;
	if (padding > remaining || sizeof(Orb_Save) > remaining - padding) return 0;
	Orb_Save *save = arena_push_zero(arena, sizeof(*save));
	if (orb->last_save) orb->last_save->next = save;
	else                orb->first_save = save;
	orb->last_save = save;
	orb->save_count ++;
	return save;
}

static b32 orb_runtime_has_save_id(const Orb *orb, Orb_Id id)
{
	for (const Orb_Save *save = orb->first_save; save; save = save->next) if (memory_match(save->id.bytes, id.bytes, sizeof(id.bytes))) return true;
	return false;
}

static Orb_Result orb_runtime_decode_save(Arena *arena, Orb *orb, Orb_Decoder *parent, Orb_Chunk container)
{
	if (container.version != 1 || container.flags & ~ORB_CHUNK_KNOWN_FLAGS || container.codec != ORB_CODEC_NONE || container.data.size != container.unpacked_size) return (Orb_Result) { .status = ORB_STATUS_UNSUPPORTED_VERSION, .offset = container.offset };
	Orb_Decoder decoder = {};
	Orb_Result result = orb_begin_container_decoding(&decoder, parent, container);
	if (result.status != ORB_STATUS_OK) return result;
	Orb_SaveMetadata metadata = {};
	Orb_Thumbnail thumbnail = {};
	ByteSpan state = {};
	b32 seen_metadata = false;
	b32 seen_state = false;
	b32 seen_thumbnail = false;
	Orb_Chunk chunk = {};
	while (orb_read_chunk(&decoder, &chunk))
	{
		if (chunk.type == ORB_CHUNK_SAVE_METADATA)
		{
			if (seen_metadata) result = (Orb_Result) { .status = ORB_STATUS_DUPLICATE_CHUNK, .offset = chunk.offset };
			else
			{
				seen_metadata = true;
				result = orb_decode_save_metadata_chunk(chunk, &metadata);
			}
		}
		else if (chunk.type == ORB_CHUNK_STATE)
		{
			if (seen_state) result = (Orb_Result) { .status = ORB_STATUS_DUPLICATE_CHUNK, .offset = chunk.offset };
			else
			{
				seen_state = true;
				result = orb_decode_save_state_chunk(chunk, &state);
			}
		}
		else if (chunk.type == ORB_CHUNK_THUMBNAIL)
		{
			if (seen_thumbnail) result = (Orb_Result) { .status = ORB_STATUS_DUPLICATE_CHUNK, .offset = chunk.offset };
			else
			{
				seen_thumbnail = true;
				result = orb_decode_thumbnail_chunk(chunk, &thumbnail);
				if (result.status == ORB_STATUS_UNSUPPORTED_VERSION && !(chunk.flags & ORB_CHUNK_REQUIRED))
				{
					orb->has_unpreserved_chunks = true;
					result = (Orb_Result) { .status = ORB_STATUS_OK };
				}
			}
		}
		else if (chunk.flags & ORB_CHUNK_REQUIRED) result = (Orb_Result) { .status = ORB_STATUS_UNSUPPORTED_CHUNK, .offset = chunk.offset };
		else orb->has_unpreserved_chunks = true;
		if (result.status != ORB_STATUS_OK)
		{
			decoder.result = result;
			return orb_end_decoding(&decoder);
		}
	}
	result = orb_end_decoding(&decoder);
	if (result.status != ORB_STATUS_OK) return result;
	if (!seen_metadata || !seen_state) return (Orb_Result) { .status = ORB_STATUS_MISSING_CHUNK, .offset = container.offset };
	if (orb_runtime_has_save_id(orb, metadata.id)) return (Orb_Result) { .status = ORB_STATUS_DUPLICATE_CHUNK, .offset = container.offset };
	Orb_Save *save = orb_runtime_push_save(arena, orb);
	if (!save) return (Orb_Result) { .status = ORB_STATUS_OUTPUT_TOO_LARGE, .offset = container.offset };
	*save = (Orb_Save) {
		.id = metadata.id,
		.kind = metadata.kind,
		.created_unix_ms = metadata.created_unix_ms,
		.updated_unix_ms = metadata.updated_unix_ms,
		.play_time_ms = metadata.play_time_ms,
		.thumbnail = thumbnail,
		.state = state,
	};
	return (Orb_Result) { .status = ORB_STATUS_OK };
}

static Orb_Result orb_runtime_decode_v2(Arena *arena, Orb_Decoder *decoder, Orb *orb)
{
	Orb_GameMetadata metadata = {};
	ByteSpan content = {};
	b32 seen_metadata = false;
	b32 seen_content = false;
	Orb_Result result = { .status = ORB_STATUS_OK };
	Orb_Chunk chunk = {};
	while (orb_read_chunk(decoder, &chunk))
	{
		if (chunk.type == ORB_CHUNK_GAME_METADATA)
		{
			if (seen_metadata) result = (Orb_Result) { .status = ORB_STATUS_DUPLICATE_CHUNK, .offset = chunk.offset };
			else
			{
				seen_metadata = true;
				result = orb_decode_game_metadata_chunk(chunk, &metadata);
			}
		}
		else if (chunk.type == ORB_CHUNK_GAME_CONTENT)
		{
			if (seen_content) result = (Orb_Result) { .status = ORB_STATUS_DUPLICATE_CHUNK, .offset = chunk.offset };
			else
			{
				seen_content = true;
				result = orb_decode_game_content_chunk(chunk, &content);
			}
		}
		else if (chunk.type == ORB_CHUNK_SAVE) result = orb_runtime_decode_save(arena, orb, decoder, chunk);
		else if (chunk.flags & ORB_CHUNK_REQUIRED) result = (Orb_Result) { .status = ORB_STATUS_UNSUPPORTED_CHUNK, .offset = chunk.offset };
		else orb->has_unpreserved_chunks = true;
		if (result.status != ORB_STATUS_OK) return result;
	}
	result = orb_end_decoding(decoder);
	if (result.status != ORB_STATUS_OK) return result;
	if (!seen_metadata || !seen_content) return (Orb_Result) { .status = ORB_STATUS_MISSING_CHUNK, .offset = decoder->source.size };
	if (!hash256_match(metadata.content_hash, sha256(content))) return (Orb_Result) { .status = ORB_STATUS_CHECKSUM_MISMATCH, .offset = 0 };
	orb->system = metadata.system;
	orb->content_hash = metadata.content_hash;
	orb->title = metadata.title;
	orb->source_path = metadata.source_path;
	orb->content = content;
	orb->first_played_unix_ms = metadata.first_played_unix_ms;
	orb->last_played_unix_ms = metadata.last_played_unix_ms;
	orb->play_time_ms = metadata.play_time_ms;
	return (Orb_Result) { .status = ORB_STATUS_OK };
}

Orb_Result orb_runtime_decode(Arena *runtime_arena, ByteSpan source, Orb *orb)
{
	if (orb) *orb = (Orb) {};
	if (!runtime_arena || !orb) return (Orb_Result) { .status = ORB_STATUS_INVALID_ARGUMENT };
	u64 arena_position = runtime_arena->position;
	Orb_Decoder decoder = {};
	Orb_Result result = orb_begin_decoding(&decoder, source);
	if (result.status == ORB_STATUS_OK) result = orb_runtime_decode_v2(runtime_arena, &decoder, orb);
	if (result.status != ORB_STATUS_OK)
	{
		runtime_arena->position = arena_position;
		*orb = (Orb) {};
	}
	return result;
}

Orb_Result orb_runtime_encode(Arena *output_arena, const Orb *orb, ByteSpan *output)
{
	if (output) *output = (ByteSpan) {};
	if (!output_arena || !orb || !output || !orb->content.data || !orb->content.size) return (Orb_Result) { .status = ORB_STATUS_INVALID_ARGUMENT };
	if (orb->has_unpreserved_chunks) return (Orb_Result) { .status = ORB_STATUS_UNSUPPORTED_CHUNK };
	Hash256 content_hash = sha256(orb->content);
	if (!hash256_is_zero(orb->content_hash) && !hash256_match(orb->content_hash, content_hash)) return (Orb_Result) { .status = ORB_STATUS_CHECKSUM_MISMATCH };
	Orb_GameMetadata game = {
		.system = orb->system,
		.content_hash = content_hash,
		.first_played_unix_ms = orb->first_played_unix_ms,
		.last_played_unix_ms = orb->last_played_unix_ms,
		.play_time_ms = orb->play_time_ms,
		.title = orb->title,
		.source_path = orb->source_path,
	};
	Orb_Encoder encoder = orb_begin_encoding(output_arena);
	orb_write_game_metadata_chunk(&encoder, game);
	orb_write_game_content_chunk(&encoder, orb->content);
	for (const Orb_Save *save = orb->first_save; save && encoder.result.status == ORB_STATUS_OK; save = save->next)
	{
		if (orb_runtime_has_save_id(&(Orb) { .first_save = save->next }, save->id))
		{
			encoder.result = (Orb_Result) { .status = ORB_STATUS_DUPLICATE_CHUNK, .offset = encoder.arena->position - encoder.start_position };
			break;
		}
		orb_begin_save_chunk(&encoder);
		orb_write_save_metadata_chunk(&encoder, (Orb_SaveMetadata) {
			.id = save->id,
			.kind = save->kind,
			.created_unix_ms = save->created_unix_ms,
			.updated_unix_ms = save->updated_unix_ms,
			.play_time_ms = save->play_time_ms,
		});
		orb_write_save_state_chunk(&encoder, save->state);
		if (save->thumbnail.pixels.size) orb_write_save_thumbnail_chunk(&encoder, save->thumbnail);
		orb_end_save_chunk(&encoder);
	}
	return orb_end_encoding(&encoder, output);
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

	Orb_Result parsed = orb_runtime_decode(&store->arena, store->source, &store->orb);
	if (parsed.status != ORB_STATUS_OK)
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
	LOG_INFO("orb store loaded '%.*s'", store->path.size, store->path.text);
	LOG_INFO("  source: ORB v%u container, %llu bytes", ORB_FILE_VERSION_CURRENT, store->source.size);
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
