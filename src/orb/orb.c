#include "orb.h"

enum
{
	INES_HEADER_SIZE = 16,
	INES_TRAINER_SIZE = 512,
	INES_PRG_BANK_SIZE = KiB(16),
	INES_CHR_BANK_SIZE = KiB(8),
};

static b32 orb_game_memory_is_valid(NES_Game game)
{
	return game.metadata.trainer_size == game.trainer.size && game.metadata.prg_rom_size == game.prg_rom.size &&
		game.metadata.chr_rom_size == game.chr_rom.size && (!game.trainer.size || game.trainer.data) &&
		(!game.prg_rom.size || game.prg_rom.data) && (!game.chr_rom.size || game.chr_rom.data);
}

static void orb_hash_u32(SHA256_Context *context, u32 value)
{
	u8 bytes[4];
	for (u32 index = 0; index < sizeof(bytes); index ++) bytes[index] = (u8)(value >> (index * 8));
	sha256_update(context, byte_span(bytes, sizeof(bytes)));
}

Hash256 orb_game_hash(NES_Game game)
{
	Assert(orb_game_memory_is_valid(game));
	static const u8 domain[] = "ORB_GAME_1";
	SHA256_Context context;
	sha256_init(&context);
	sha256_update(&context, byte_span((void *)domain, sizeof(domain) - 1));
	orb_hash_u32(&context, game.metadata.mapper);
	orb_hash_u32(&context, game.metadata.mirroring == NES_MIRROR_VERTICAL);
	orb_hash_u32(&context, !!game.metadata.trainer_size);
	orb_hash_u32(&context, game.metadata.mirroring == NES_MIRROR_FOUR_SCREEN);
	orb_hash_u32(&context, game.metadata.prg_rom_size);
	orb_hash_u32(&context, game.metadata.chr_rom_size);
	sha256_update(&context, game.trainer);
	sha256_update(&context, game.prg_rom);
	sha256_update(&context, game.chr_rom);
	return sha256_final(&context);
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

static b32 orb_game_from_ines(ByteSpan source, NES_Game *game)
{
	Assert(game);
	*game = (NES_Game) {};
	ByteStream stream = byte_stream_reader(source);
	const u8 magic[] = { 'N', 'E', 'S', 0x1A };
	ByteSpan header_bytes = byte_stream_take(&stream, INES_HEADER_SIZE);
	if (stream.failed || !memory_match(header_bytes.data, magic, sizeof(magic))) return false;
	const u8 *header = header_bytes.data;
	if ((header[7] & 0x0C) == 0x08 && !nes2_header_is_ines_compatible(header)) return false;

	NES_Game result = {
		.metadata = {
			.mapper = (header[6] >> 4) | (header[7] & 0xF0),
			.mirroring = (header[6] & 0x08) ? NES_MIRROR_FOUR_SCREEN :
				((header[6] & 0x01) ? NES_MIRROR_VERTICAL : NES_MIRROR_HORIZONTAL),
			.trainer_size = (header[6] & 0x04) ? INES_TRAINER_SIZE : 0,
			.prg_rom_size = (u32)header[4] * INES_PRG_BANK_SIZE,
			.chr_rom_size = (u32)header[5] * INES_CHR_BANK_SIZE,
		},
	};
	result.trainer = byte_stream_take(&stream, result.metadata.trainer_size);
	result.prg_rom = byte_stream_take(&stream, result.metadata.prg_rom_size);
	result.chr_rom = byte_stream_take(&stream, result.metadata.chr_rom_size);
	if (stream.failed) return false;
	*game = result;
	return true;
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

static Str orb_title_from_path(Str path)
{
	u32 begin = 0;
	for (u32 index = 0; index < path.size; index ++) if (path.data[index] == '/' || path.data[index] == '\\') begin = index + 1;
	u32 end = path.size;
	for (u32 index = end; index > begin; index --)
	{
		if (path.data[index - 1] != '.') continue;
		if (index - 1 > begin) end = index - 1;
		break;
	}
	return str_slice(path, begin, end - begin);
}

NES_Game *orb_game_from_ines_file(Arena *arena, Str path, Str *title)
{
	if (title) *title = (Str) {};
	if (!arena || !arena->memory || !path.data || !path.size) return 0;
	u64 arena_start = arena->position;
	Str stored_path = str_push_copy(arena, path);
	NES_Game *game = arena_push_zero(arena, sizeof(*game));
	ByteSpan source = orb_read_entire_file(arena, stored_path.data);
	if (!source.size || !orb_game_from_ines(source, game))
	{
		arena->position = arena_start;
		return 0;
	}
	if (title) *title = orb_title_from_path(stored_path);
	return game;
}
