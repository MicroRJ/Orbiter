#include "orb.h"
#include "nes_serialize.h"

#define ORB_FOURCC(a, b, c, d) ((u32)(u8)(a) | (u32)(u8)(b) << 8 | (u32)(u8)(c) << 16 | (u32)(u8)(d) << 24)

enum
{
	ORB_MAGIC = ORB_FOURCC('O', 'R', 'B', 'S'),
	ORB_FILE_VERSION = 1,
	ORB_MAX_SAVE_COUNT = 4096,

	INES_HEADER_SIZE = 16,
	INES_TRAINER_SIZE = 512,
	INES_PRG_BANK_SIZE = KiB(16),
	INES_CHR_BANK_SIZE = KiB(8),
};

typedef struct
{
	u32 magic;
	u32 version;
}
Orb_FileHeader;

static b32 orb_game_memory_is_valid(Orb_Game game)
{
	if (game.metadata.prg_rom_size && !game.prg_rom_data) return false;
	if (game.metadata.chr_rom_size && !game.chr_rom_data) return false;
	if (game.metadata.has_trainer && !game.trainer_data) return false;
	return true;
}

static void orb_hash_u32(SHA256_Context *context, u32 value)
{
	u8 bytes[4];
	for (u32 index = 0; index < sizeof(bytes); index ++) bytes[index] = (u8)(value >> (index * 8));
	sha256_update(context, byte_span(bytes, sizeof(bytes)));
}

Hash256 orb_game_hash(Orb_Game game)
{
	Assert(orb_game_memory_is_valid(game));
	static const u8 domain[] = "ORB_GAME_1";
	SHA256_Context context;
	sha256_init(&context);
	sha256_update(&context, byte_span((void *)domain, sizeof(domain) - 1));
	orb_hash_u32(&context, game.metadata.mapper);
	orb_hash_u32(&context, !!game.metadata.vmirror);
	orb_hash_u32(&context, !!game.metadata.has_trainer);
	orb_hash_u32(&context, !!game.metadata.four_screen);
	orb_hash_u32(&context, game.metadata.prg_rom_size);
	orb_hash_u32(&context, game.metadata.chr_rom_size);
	if (game.metadata.has_trainer) sha256_update(&context, byte_span(game.trainer_data, INES_TRAINER_SIZE));
	sha256_update(&context, byte_span(game.prg_rom_data, game.metadata.prg_rom_size));
	sha256_update(&context, byte_span(game.chr_rom_data, game.metadata.chr_rom_size));
	return sha256_final(&context);
}

static void *orb_transfer_memory(ByteStream *stream, void *memory, u64 size)
{
	if (stream->mode == BYTE_STREAM_READ) return size ? byte_stream_take(stream, size).data : 0;
	byte_transfer_bytes(stream, byte_span(memory, size));
	return memory;
}

static void orb_transfer_bool(ByteStream *stream, b32 *value)
{
	u32 encoded = !!*value;
	byte_transfer_u32(stream, &encoded);
	if (stream->mode == BYTE_STREAM_READ)
	{
		if (encoded > 1) stream->failed = true;
		*value = encoded == 1;
	}
}

static void orb_transfer_string(ByteStream *stream, Str *string)
{
	byte_transfer_u32(stream, &string->size);
	string->data = orb_transfer_memory(stream, string->data, string->size);
}

static void orb_transfer_file_header(ByteStream *stream, Orb_FileHeader *header)
{
	byte_transfer_u32(stream, &header->magic);
	byte_transfer_u32(stream, &header->version);
}

static void orb_transfer_metadata(ByteStream *stream, Orb *orb)
{
	byte_transfer_u64(stream, &orb->first_played_unix_ms);
	byte_transfer_u64(stream, &orb->last_played_unix_ms);
	byte_transfer_u64(stream, &orb->play_time_ms);
	orb_transfer_string(stream, &orb->title);
}

static void orb_transfer_game(ByteStream *stream, Orb_Game *game)
{
	byte_transfer_u32(stream, &game->metadata.mapper);
	orb_transfer_bool(stream, &game->metadata.vmirror);
	orb_transfer_bool(stream, &game->metadata.has_trainer);
	orb_transfer_bool(stream, &game->metadata.four_screen);
	byte_transfer_u32(stream, &game->metadata.prg_rom_size);
	byte_transfer_u32(stream, &game->metadata.chr_rom_size);
	if (game->metadata.has_trainer) game->trainer_data = orb_transfer_memory(stream, game->trainer_data, INES_TRAINER_SIZE);
	game->prg_rom_data = orb_transfer_memory(stream, game->prg_rom_data, game->metadata.prg_rom_size);
	game->chr_rom_data = orb_transfer_memory(stream, game->chr_rom_data, game->metadata.chr_rom_size);
}

static void orb_transfer_thumbnail(ByteStream *stream, Orb_Thumbnail *thumbnail)
{
	byte_transfer_u32(stream, &thumbnail->width);
	byte_transfer_u32(stream, &thumbnail->height);
	byte_transfer_u32(stream, &thumbnail->stride);
	u32 format = thumbnail->format;
	byte_transfer_u32(stream, &format);
	thumbnail->format = format;
	byte_transfer_u64(stream, &thumbnail->pixels.size);
	thumbnail->pixels.data = orb_transfer_memory(stream, thumbnail->pixels.data, thumbnail->pixels.size);
}

static void orb_transfer_save_metadata(ByteStream *stream, Orb_SaveMetadata *metadata)
{
	byte_transfer_u64(stream, &metadata->kind);
	byte_transfer_u64(stream, &metadata->flags);
	byte_transfer_bytes(stream, byte_span(metadata->id.bytes, sizeof(metadata->id.bytes)));
	byte_transfer_u64(stream, &metadata->created_unix_ms);
	byte_transfer_u64(stream, &metadata->updated_unix_ms);
	byte_transfer_u64(stream, &metadata->play_time_ms);
}

static void orb_transfer_save(ByteStream *stream, Orb_SaveNode *save)
{
	orb_transfer_save_metadata(stream, &save->metadata);
	if (!stream->failed) orb_transfer_save_state(stream, &save->state);
	u32 has_thumbnail = save->thumbnail.pixels.size != 0;
	byte_transfer_u32(stream, &has_thumbnail);
	if (stream->mode == BYTE_STREAM_READ && has_thumbnail > 1) stream->failed = true;
	if (has_thumbnail == 1) orb_transfer_thumbnail(stream, &save->thumbnail);
}

static b32 orb_arena_can_push(Arena *arena, u64 size)
{
	if (!arena || !arena->memory || arena->position > arena->reserved_size) return false;
	u64 remaining = arena->reserved_size - arena->position;
	return size <= remaining && ARENA_DEFAULT_ALIGNMENT - 1 <= remaining - size;
}

ByteSpan orb_write(Arena *arena, Orb *orb)
{
	Assert(arena && orb);
	Assert(orb_game_memory_is_valid(orb->game));
	Assert(!orb->title.size || orb->title.data);

	ByteStream stream = byte_stream_arena_writer(arena);
	Orb_FileHeader header = { .magic = ORB_MAGIC, .version = ORB_FILE_VERSION };
	orb_transfer_file_header(&stream, &header);
	orb_transfer_metadata(&stream, orb);
	orb_transfer_game(&stream, &orb->game);

	u32 save_count = 0;
	for (Orb_SaveNode *save = orb->first_save; save; save = save->next)
	{
		Assert(save_count < ORB_MAX_SAVE_COUNT);
		save_count ++;
	}
	orb->save_count = save_count;
	byte_transfer_u32(&stream, &save_count);
	for (Orb_SaveNode *save = orb->first_save; save && !stream.failed; save = save->next) orb_transfer_save(&stream, save);
	return byte_stream_written(&stream);
}

Orb *orb_read(Arena *arena, ByteSpan source)
{
	if (!arena || !arena->memory || (!source.data && source.size)) return 0;
	u64 arena_start = arena->position;
	ByteStream stream = byte_stream_reader(source);
	Orb_FileHeader header = {};
	orb_transfer_file_header(&stream, &header);
	if (stream.failed || header.magic != ORB_MAGIC || header.version != ORB_FILE_VERSION || !orb_arena_can_push(arena, sizeof(Orb)))
	{
		arena->position = arena_start;
		return 0;
	}

	Orb *orb = arena_push_zero(arena, sizeof(*orb));
	orb_transfer_metadata(&stream, orb);
	orb_transfer_game(&stream, &orb->game);
	byte_transfer_u32(&stream, &orb->save_count);
	if (orb->save_count > ORB_MAX_SAVE_COUNT) stream.failed = true;

	for (u32 index = 0; index < orb->save_count && !stream.failed; index ++)
	{
		if (!orb_arena_can_push(arena, sizeof(Orb_SaveNode)))
		{
			stream.failed = true;
			break;
		}
		Orb_SaveNode *save = arena_push_zero(arena, sizeof(*save));
		save->orb = orb;
		orb_transfer_save(&stream, save);
		if (orb->last_save) orb->last_save->next = save;
		else                orb->first_save = save;
		orb->last_save = save;
	}

	if (stream.failed || byte_stream_remaining(&stream) || !orb_game_memory_is_valid(orb->game))
	{
		arena->position = arena_start;
		return 0;
	}
	orb->game_hash = orb_game_hash(orb->game);
	return orb;
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

static b32 nes2_ram_layout_is_ines_compatible(u8 layout)
{
	return layout == 0 || layout == 0x07 || layout == 0x70;
}

static b32 nes2_header_is_ines_compatible(const u8 *header)
{
	u8 timing = header[12] & 0x03;
	return !(header[7] & 0x03) && !header[8] && !header[9] && nes2_ram_layout_is_ines_compatible(header[10]) && nes2_ram_layout_is_ines_compatible(header[11]) &&
		!(header[12] & ~0x03) && (timing == 0 || timing == 2) && !header[13] && !header[14] && header[15] <= 1;
}

static b32 orb_game_from_ines(ByteStream *stream, Orb_Game *game)
{
	Assert(stream && stream->mode == BYTE_STREAM_READ && game);
	*game = (Orb_Game) {};
	const u8 magic[] = { 'N', 'E', 'S', 0x1A };
	ByteSpan header_bytes = byte_stream_take(stream, INES_HEADER_SIZE);
	if (stream->failed || !memory_match(header_bytes.data, magic, sizeof(magic))) return false;
	const u8 *header = header_bytes.data;
	if ((header[7] & 0x0C) == 0x08 && !nes2_header_is_ines_compatible(header)) return false;

	Orb_Game result = {
		.metadata = {
			.mapper = (header[6] >> 4) | (header[7] & 0xF0),
			.vmirror = !!(header[6] & 0x01),
			.has_trainer = !!(header[6] & 0x04),
			.four_screen = !!(header[6] & 0x08),
			.prg_rom_size = (u32)header[4] * INES_PRG_BANK_SIZE,
			.chr_rom_size = (u32)header[5] * INES_CHR_BANK_SIZE,
		},
	};
	if (result.metadata.has_trainer) result.trainer_data = byte_stream_take(stream, INES_TRAINER_SIZE).data;
	result.prg_rom_data = byte_stream_take(stream, result.metadata.prg_rom_size).data;
	result.chr_rom_data = byte_stream_take(stream, result.metadata.chr_rom_size).data;
	if (stream->failed) return false;
	*game = result;
	return true;
}

Orb *orb_from_game(Orb_Store *store, Orb_Game game)
{
	if (!store || !store->arena.memory || !orb_game_memory_is_valid(game) || !orb_arena_can_push(&store->arena, sizeof(Orb))) return 0;
	Orb *orb = arena_push_zero(&store->arena, sizeof(*orb));
	orb->game = game;
	orb->game_hash = orb_game_hash(game);
	store->orb = orb;
	return orb;
}

static ByteSpan orb_read_entire_file(Arena *arena, const char *path)
{
	Platform_File file = platform_access_file(path, PLATFORM_FILE_OPEN_EXISTING, PLATFORM_FILE_READ | PLATFORM_FILE_SHARE_READ | PLATFORM_FILE_SHARE_WRITE | PLATFORM_FILE_SHARE_DELETE);
	if (!platform_file_is_valid(file)) return (ByteSpan) {};
	u64 file_size = 0;
	u64 available = arena->reserved_size - arena->position;
	b32 valid_size = platform_get_file_size(file, &file_size) && file_size && file_size <= available;
	if (!valid_size)
	{
		platform_close_file(file);
		return (ByteSpan) {};
	}

	u64 arena_start = arena->position;
	u8 *data = arena_push_aligned(arena, file_size, 1);
	u64 bytes_read = 0;
	b32 success = platform_read_file(file, data, file_size, &bytes_read) && bytes_read == file_size;
	platform_close_file(file);
	if (!success)
	{
		arena->position = arena_start;
		return (ByteSpan) {};
	}
	return byte_span(data, file_size);
}

static Str orb_title_from_path(Arena *arena, Str path)
{
	u32 begin = 0;
	for (u32 index = 0; index < path.size; index ++)
	{
		if (path.data[index] == '/' || path.data[index] == '\\') begin = index + 1;
	}

	u32 end = path.size;
	for (u32 index = end; index > begin; index --)
	{
		if (path.data[index - 1] == '.')
		{
			if (index - 1 > begin) end = index - 1;
			break;
		}
	}
	return str_push_copy(arena, str_slice(path, begin, end - begin));
}

Orb *orb_from_file(Orb_Store *store, Str path)
{
	if (!store || !store->arena.memory || !path.data || !path.size) return 0;
	arena_reset(&store->arena);
	store->path = str_push_copy(&store->arena, path);
	store->orb = 0;
	ByteSpan source = orb_read_entire_file(&store->arena, store->path.data);
	if (!source.size) goto failed;

	const u8 orb_magic[] = { 'O', 'R', 'B', 'S' };
	const u8 ines_magic[] = { 'N', 'E', 'S', 0x1A };
	Orb *orb = 0;
	b32 imported_ines = false;
	if (source.size >= sizeof(orb_magic) && memory_match(source.data, orb_magic, sizeof(orb_magic))) {
		orb = orb_read(&store->arena, source);
	}
	else if (source.size >= sizeof(ines_magic) && memory_match(source.data, ines_magic, sizeof(ines_magic)))
	{
		ByteStream stream = byte_stream_reader(source);
		Orb_Game game;
		if (orb_game_from_ines(&stream, &game))
		{
			orb = orb_from_game(store, game);
			imported_ines = orb != 0;
		}
	}
	if (!orb) goto failed;

	orb->disk_path = store->path;
	if (imported_ines) orb->title = orb_title_from_path(&store->arena, store->path);
	store->orb = orb;
	return orb;

failed:
	arena_reset(&store->arena);
	store->path = (Str) {};
	store->orb = 0;
	return 0;
}
