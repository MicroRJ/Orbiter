#include "orb.h"

enum
{
	ORB_MAGIC = ORB_FOURCC('O', 'R', 'B', 'S'),
	ORB_FILE_HEADER_SIZE = 16,
	ORB_CHUNK_HEADER_SIZE = 32,
	ORB_METADATA_VERSION = 1,
	ORB_METADATA_FIXED_SIZE = 68,
	ORB_CARTRIDGE_VERSION = 1,
	ORB_CARTRIDGE_FIXED_SIZE = 16,
	ORB_PRG_ROM_VERSION = 1,
	ORB_CHR_ROM_VERSION = 1,
	ORB_SAVE_VERSION = 1,
	ORB_SAVE_METADATA_VERSION = 1,
	ORB_SAVE_METADATA_FIXED_SIZE = 48,
	ORB_THUMBNAIL_VERSION = 1,
	ORB_THUMBNAIL_FIXED_SIZE = 16,
	ORB_STATE_VERSION = 1,
	ORB_ALIGNMENT = 8,
	ORB_MAX_CHUNKS = 1024,

	ORB_ROOT_METADATA = 1 << 0,
	ORB_ROOT_CARTRIDGE = 1 << 1,
	ORB_ROOT_PRG_ROM = 1 << 2,
	ORB_ROOT_CHR_ROM = 1 << 3,
	ORB_ROOT_ALL = ORB_ROOT_METADATA | ORB_ROOT_CARTRIDGE | ORB_ROOT_PRG_ROM | ORB_ROOT_CHR_ROM,
	ORB_SAVE_METADATA = 1 << 0,
	ORB_SAVE_STATE = 1 << 1,
	ORB_SAVE_THUMBNAIL = 1 << 2,

	ORB_CARTRIDGE_VERTICAL_MIRRORING = 1 << 0,
	ORB_CARTRIDGE_FOUR_SCREEN = 1 << 1,
	ORB_CARTRIDGE_KNOWN_FLAGS = ORB_CARTRIDGE_VERTICAL_MIRRORING | ORB_CARTRIDGE_FOUR_SCREEN,
};

static const u64 ORB_MAX_FILE_SIZE = MB(256);
static const u64 ORB_MAX_METADATA_SIZE = KiB(64);
static const u64 ORB_MAX_THUMBNAIL_SIZE = MB(64);

typedef struct
{
	u32 magic;
	u16 version;
	u16 header_size;
	u32 flags;
	u32 reserved;
}
Orb_FileHeader;

typedef struct
{
	u32 type;
	u16 version;
	u16 flags;
	u64 stored_size;
	u64 unpacked_size;
	u32 checksum;
	u16 codec;
	u16 header_size;
}
Orb_ChunkHeader;

typedef struct
{
	u32 flags;
	Hash256 content_hash;
	u64 first_played_unix_ms;
	u64 last_played_unix_ms;
	u64 play_time_ms;
	u32 title_size;
	u32 source_path_size;
}
Orb_MetadataFixed;

typedef struct
{
	u32 mapper;
	u32 flags;
	u32 prg_rom_size;
	u32 chr_rom_size;
}
Orb_CartridgeMetadataFixed;

typedef struct
{
	u32 kind;
	u32 flags;
	Orb_Id id;
	u64 created_unix_ms;
	u64 updated_unix_ms;
	u64 play_time_ms;
}
Orb_SaveMetadataFixed;

typedef struct
{
	u32 width;
	u32 height;
	u32 stride;
	u32 format;
}
Orb_ThumbnailFixed;

STATIC_ASSERT(4 + 2 + 2 + 4 + 4 == ORB_FILE_HEADER_SIZE);
STATIC_ASSERT(4 + 2 + 2 + 8 + 8 + 4 + 2 + 2 == ORB_CHUNK_HEADER_SIZE);
STATIC_ASSERT(4 + 32 + 8 * 3 + 4 * 2 == ORB_METADATA_FIXED_SIZE);
STATIC_ASSERT(4 * 4 == ORB_CARTRIDGE_FIXED_SIZE);
STATIC_ASSERT(4 * 2 + 16 + 8 * 3 == ORB_SAVE_METADATA_FIXED_SIZE);
STATIC_ASSERT(4 * 4 == ORB_THUMBNAIL_FIXED_SIZE);

static void orb_transfer_file_header(ByteStream *stream, Orb_FileHeader *header)
{
	byte_transfer_u32(stream, &header->magic);
	byte_transfer_u16(stream, &header->version);
	byte_transfer_u16(stream, &header->header_size);
	byte_transfer_u32(stream, &header->flags);
	byte_transfer_u32(stream, &header->reserved);
}

static void orb_transfer_chunk_header(ByteStream *stream, Orb_ChunkHeader *header)
{
	byte_transfer_u32(stream, &header->type);
	byte_transfer_u16(stream, &header->version);
	byte_transfer_u16(stream, &header->flags);
	byte_transfer_u64(stream, &header->stored_size);
	byte_transfer_u64(stream, &header->unpacked_size);
	byte_transfer_u32(stream, &header->checksum);
	byte_transfer_u16(stream, &header->codec);
	byte_transfer_u16(stream, &header->header_size);
}

static void orb_transfer_metadata_fixed(ByteStream *stream, Orb_MetadataFixed *metadata)
{
	byte_transfer_u32(stream, &metadata->flags);
	byte_transfer_bytes(stream, byte_span(metadata->content_hash.bytes, sizeof(metadata->content_hash.bytes)));
	byte_transfer_u64(stream, &metadata->first_played_unix_ms);
	byte_transfer_u64(stream, &metadata->last_played_unix_ms);
	byte_transfer_u64(stream, &metadata->play_time_ms);
	byte_transfer_u32(stream, &metadata->title_size);
	byte_transfer_u32(stream, &metadata->source_path_size);
}

static void orb_transfer_cartridge_fixed(ByteStream *stream, Orb_CartridgeMetadataFixed *cartridge)
{
	byte_transfer_u32(stream, &cartridge->mapper);
	byte_transfer_u32(stream, &cartridge->flags);
	byte_transfer_u32(stream, &cartridge->prg_rom_size);
	byte_transfer_u32(stream, &cartridge->chr_rom_size);
}

static void orb_transfer_save_metadata_fixed(ByteStream *stream, Orb_SaveMetadataFixed *metadata)
{
	byte_transfer_u32(stream, &metadata->kind);
	byte_transfer_u32(stream, &metadata->flags);
	byte_transfer_bytes(stream, byte_span(metadata->id.bytes, sizeof(metadata->id.bytes)));
	byte_transfer_u64(stream, &metadata->created_unix_ms);
	byte_transfer_u64(stream, &metadata->updated_unix_ms);
	byte_transfer_u64(stream, &metadata->play_time_ms);
}

static void orb_transfer_thumbnail_fixed(ByteStream *stream, Orb_ThumbnailFixed *thumbnail)
{
	byte_transfer_u32(stream, &thumbnail->width);
	byte_transfer_u32(stream, &thumbnail->height);
	byte_transfer_u32(stream, &thumbnail->stride);
	byte_transfer_u32(stream, &thumbnail->format);
}

static Orb_Result orb_result(Orb_Status status, u64 offset)
{
	return (Orb_Result) { .status = status, .offset = offset };
}

static b32 orb_add_size(u64 *size, u64 addition)
{
	if (addition > ~(u64)0 - *size) return false;
	*size += addition;
	return true;
}

static b32 orb_align_size(u64 *size)
{
	u64 remainder = *size & (ORB_ALIGNMENT - 1);
	return !remainder || orb_add_size(size, ORB_ALIGNMENT - remainder);
}

static u32 orb_crc32_update(u32 crc, const void *data, u64 size)
{
	const u8 *bytes = data;
	for (u64 index = 0; index < size; index ++)
	{
		crc ^= bytes[index];
		for (u32 bit = 0; bit < 8; bit ++) crc = crc >> 1 ^ (0xEDB88320u & (u32)-(i32)(crc & 1));
	}
	return crc;
}

static u32 orb_crc32(const void *data, u64 size)
{
	return ~orb_crc32_update(~0u, data, size);
}

static b32 orb_save_kind_valid(Orb_SaveKind kind)
{
	return kind == ORB_SAVE_RESUME || kind == ORB_SAVE_MANUAL;
}

static b32 orb_string_valid(Str string)
{
	return string.text || !string.size;
}

static b32 orb_thumbnail_valid(Orb_Thumbnail thumbnail)
{
	u64 pixel_size = thumbnail.pixels.size;
	if (pixel_size && !thumbnail.pixels.data) return false;
	if (!pixel_size) return !thumbnail.width && !thumbnail.height && !thumbnail.stride && !thumbnail.format;
	if (!thumbnail.width || !thumbnail.height || thumbnail.format != ORB_PIXEL_FORMAT_RGBA8) return false;
	u64 minimum_stride = (u64)thumbnail.width * 4;
	if (minimum_stride > MAX_VALUE_U32 || thumbnail.stride < minimum_stride) return false;
	u64 expected_size = (u64)thumbnail.stride * thumbnail.height;
	return expected_size <= ORB_MAX_THUMBNAIL_SIZE && expected_size == pixel_size;
}

static b32 orb_metadata_valid(Orb_Metadata metadata)
{
	if (hash256_is_zero(metadata.content_hash) || !orb_string_valid(metadata.title) || !orb_string_valid(metadata.source_path)) return false;
	if (metadata.first_played_unix_ms && metadata.last_played_unix_ms && metadata.last_played_unix_ms < metadata.first_played_unix_ms) return false;
	return true;
}

static b32 orb_cartridge_metadata_valid(Orb_CartridgeMetadata cartridge)
{
	if (!cartridge.prg_rom_size) return false;
	if ((cartridge.vertical_mirroring != 0 && cartridge.vertical_mirroring != 1) || (cartridge.four_screen != 0 && cartridge.four_screen != 1)) return false;
	return true;
}

static Orb_CartridgeMetadata orb_cartridge_metadata(NES_CartridgeDesc cartridge)
{
	if (cartridge.has_trainer || cartridge.prg_rom.size > MAX_VALUE_U32 || cartridge.chr_rom.size > MAX_VALUE_U32) return (Orb_CartridgeMetadata) {};
	return (Orb_CartridgeMetadata) {
		.mapper = cartridge.mapper,
		.prg_rom_size = (u32)cartridge.prg_rom.size,
		.chr_rom_size = (u32)cartridge.chr_rom.size,
		.vertical_mirroring = !!cartridge.vertical_mirroring,
		.four_screen = !!cartridge.four_screen,
	};
}

static Orb_CartridgeMetadataFixed orb_cartridge_fixed(Orb_CartridgeMetadata cartridge)
{
	return (Orb_CartridgeMetadataFixed) {
		.mapper = cartridge.mapper,
		.flags = (cartridge.vertical_mirroring ? ORB_CARTRIDGE_VERTICAL_MIRRORING : 0) |
			(cartridge.four_screen ? ORB_CARTRIDGE_FOUR_SCREEN : 0),
		.prg_rom_size = cartridge.prg_rom_size,
		.chr_rom_size = cartridge.chr_rom_size,
	};
}

static void orb_cartridge_hash_begin(SHA256_Context *context, Orb_CartridgeMetadata cartridge)
{
	static const u8 domain[] = "ORB NES CARTRIDGE 1";
	u8 encoded[ORB_CARTRIDGE_FIXED_SIZE];
	Orb_CartridgeMetadataFixed fixed = orb_cartridge_fixed(cartridge);
	ByteStream writer = byte_stream_writer(byte_span(encoded, sizeof(encoded)));
	orb_transfer_cartridge_fixed(&writer, &fixed);
	Assert(!writer.failed && writer.cursor == writer.size);
	sha256_init(context);
	sha256_update(context, byte_span((void *)domain, sizeof(domain) - 1));
	sha256_update(context, byte_span(encoded, sizeof(encoded)));
}

Hash256 orb_cartridge_hash(NES_CartridgeDesc cartridge)
{
	Orb_CartridgeMetadata metadata = orb_cartridge_metadata(cartridge);
	if (!orb_cartridge_metadata_valid(metadata) || !cartridge.prg_rom.data || cartridge.chr_rom.size && !cartridge.chr_rom.data) return (Hash256) {};
	SHA256_Context context;
	orb_cartridge_hash_begin(&context, metadata);
	sha256_update(&context, cartridge.prg_rom);
	sha256_update(&context, cartridge.chr_rom);
	return sha256_final(&context);
}

static b32 orb_save_metadata_valid(Orb_SaveMetadata metadata)
{
	if (!orb_save_kind_valid(metadata.kind)) return false;
	if (metadata.created_unix_ms && metadata.updated_unix_ms && metadata.updated_unix_ms < metadata.created_unix_ms) return false;
	return true;
}

static b32 orb_encoder_fail(Orb_Encoder *encoder, Orb_Status status)
{
	if (encoder && encoder->result.status == ORB_STATUS_OK)
	{
		u64 offset = encoder->arena && encoder->arena->position >= encoder->start_position ? encoder->arena->position - encoder->start_position : 0;
		encoder->result = orb_result(status, offset);
	}
	return false;
}

static ByteSpan orb_encoder_push(Orb_Encoder *encoder, u64 size)
{
	if (!encoder || encoder->result.status != ORB_STATUS_OK || encoder->ended) return (ByteSpan) {};
	if (!encoder->arena || encoder->arena->position != encoder->expected_position) {
		orb_encoder_fail(encoder, ORB_STATUS_INVALID_SEQUENCE);
		return (ByteSpan) {};
	}
	u64 file_size = encoder->arena->position - encoder->start_position;
	if (size > ORB_MAX_FILE_SIZE - file_size || size > encoder->arena->reserved_size - encoder->arena->position)
	{
		orb_encoder_fail(encoder, ORB_STATUS_OUTPUT_TOO_LARGE);
		return (ByteSpan) {};
	}
	u8 *data = arena_push_aligned(encoder->arena, size, 1);
	encoder->expected_position = encoder->arena->position;
	return byte_span(data, size);
}

static u64 orb_encoder_begin_chunk(Orb_Encoder *encoder)
{
	if (!encoder || !encoder->arena || encoder->result.status != ORB_STATUS_OK || encoder->ended) return 0;
	if (++encoder->chunk_count > ORB_MAX_CHUNKS)
	{
		orb_encoder_fail(encoder, ORB_STATUS_OUTPUT_TOO_LARGE);
		return 0;
	}
	u64 header_position = encoder->arena->position;
	ByteSpan header = orb_encoder_push(encoder, ORB_CHUNK_HEADER_SIZE);
	if (header.data) memory_zero(header.data, header.size);
	return header_position;
}

static b32 orb_encoder_end_chunk(Orb_Encoder *encoder, u64 header_position, u32 type, u16 version, u16 flags)
{
	if (!encoder || encoder->result.status != ORB_STATUS_OK) return false;
	if (header_position < encoder->start_position || header_position + ORB_CHUNK_HEADER_SIZE > encoder->arena->position) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_SEQUENCE);
	u64 payload_position = header_position + ORB_CHUNK_HEADER_SIZE;
	u64 payload_size = encoder->arena->position - payload_position;
	u8 *payload = encoder->arena->memory + payload_position;
	Orb_ChunkHeader header = {
		.type = type,
		.version = version,
		.flags = flags,
		.stored_size = payload_size,
		.unpacked_size = payload_size,
		.checksum = flags & ORB_CHUNK_HAS_CRC32 ? orb_crc32(payload, payload_size) : 0,
		.codec = ORB_CODEC_NONE,
		.header_size = ORB_CHUNK_HEADER_SIZE,
	};
	ByteStream patch = byte_stream_writer(byte_span(encoder->arena->memory + header_position, ORB_CHUNK_HEADER_SIZE));
	orb_transfer_chunk_header(&patch, &header);
	Assert(!patch.failed && patch.cursor == patch.size);
	u64 aligned = encoder->arena->position - encoder->start_position;
	if (!orb_align_size(&aligned)) return orb_encoder_fail(encoder, ORB_STATUS_OUTPUT_TOO_LARGE);
	u64 padding = aligned - (encoder->arena->position - encoder->start_position);
	ByteSpan padding_bytes = orb_encoder_push(encoder, padding);
	if (padding_bytes.data) memory_zero(padding_bytes.data, padding_bytes.size);
	return encoder->result.status == ORB_STATUS_OK;
}

static b32 orb_encoder_write_bytes_chunk(Orb_Encoder *encoder, u32 type, u16 version, u16 flags, ByteSpan data)
{
	if (data.size && !data.data) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_ARGUMENT);
	u64 header = orb_encoder_begin_chunk(encoder);
	if (encoder->result.status != ORB_STATUS_OK) return false;
	ByteSpan destination = orb_encoder_push(encoder, data.size);
	if (data.size && destination.data) memory_copy(destination.data, data.data, data.size);
	return orb_encoder_end_chunk(encoder, header, type, version, flags);
}

Orb_Encoder orb_begin_encoding(Arena *output_arena)
{
	Orb_Encoder encoder = { .arena = output_arena, .result = orb_result(ORB_STATUS_OK, 0) };
	if (!output_arena || !output_arena->memory)
	{
		encoder.result = orb_result(ORB_STATUS_INVALID_ARGUMENT, 0);
		return encoder;
	}
	encoder.rollback_position = output_arena->position;
	encoder.start_position = output_arena->position;
	encoder.expected_position = output_arena->position;
	u64 address = (u64)(uintptr_t)output_arena->memory + output_arena->position;
	u64 alignment = (ORB_ALIGNMENT - (address & (ORB_ALIGNMENT - 1))) & (ORB_ALIGNMENT - 1);
	if (alignment + ORB_FILE_HEADER_SIZE > output_arena->reserved_size - output_arena->position)
	{
		encoder.result = orb_result(ORB_STATUS_OUTPUT_TOO_LARGE, 0);
		return encoder;
	}
	arena_push_aligned(output_arena, 0, ORB_ALIGNMENT);
	encoder.start_position = output_arena->position;
	encoder.expected_position = output_arena->position;
	ByteSpan header_data = orb_encoder_push(&encoder, ORB_FILE_HEADER_SIZE);
	if (encoder.result.status == ORB_STATUS_OK)
	{
		Orb_FileHeader header = {
			.magic = ORB_MAGIC,
			.version = ORB_FILE_VERSION_CURRENT,
			.header_size = ORB_FILE_HEADER_SIZE,
		};
		ByteStream writer = byte_stream_writer(header_data);
		orb_transfer_file_header(&writer, &header);
		Assert(!writer.failed && writer.cursor == writer.size);
	}
	return encoder;
}

b32 orb_write_metadata_chunk(Orb_Encoder *encoder, Orb_Metadata metadata)
{
	if (!encoder) return false;
	if (encoder->ended || encoder->in_save || encoder->root_chunks) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_SEQUENCE);
	if (!orb_metadata_valid(metadata)) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_ARGUMENT);
	u64 payload_size = ORB_METADATA_FIXED_SIZE;
	if (!orb_add_size(&payload_size, metadata.title.size) || !orb_add_size(&payload_size, metadata.source_path.size) || payload_size > ORB_MAX_METADATA_SIZE) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_ARGUMENT);
	u64 header = orb_encoder_begin_chunk(encoder);
	ByteSpan payload = orb_encoder_push(encoder, payload_size);
	if (encoder->result.status != ORB_STATUS_OK) return false;
	Orb_MetadataFixed fixed = {
		.content_hash = metadata.content_hash,
		.first_played_unix_ms = metadata.first_played_unix_ms,
		.last_played_unix_ms = metadata.last_played_unix_ms,
		.play_time_ms = metadata.play_time_ms,
		.title_size = metadata.title.size,
		.source_path_size = metadata.source_path.size,
	};
	ByteStream writer = byte_stream_writer(payload);
	orb_transfer_metadata_fixed(&writer, &fixed);
	if (metadata.title.size) byte_transfer_bytes(&writer, byte_span(metadata.title.data, metadata.title.size));
	if (metadata.source_path.size) byte_transfer_bytes(&writer, byte_span(metadata.source_path.data, metadata.source_path.size));
	Assert(!writer.failed && writer.cursor == writer.size);
	if (!orb_encoder_end_chunk(encoder, header, ORB_CHUNK_METADATA, ORB_METADATA_VERSION, ORB_CHUNK_REQUIRED | ORB_CHUNK_HAS_CRC32)) return false;
	encoder->content_hash = metadata.content_hash;
	encoder->root_chunks |= ORB_ROOT_METADATA;
	return true;
}

b32 orb_write_cartridge_chunk(Orb_Encoder *encoder, Orb_CartridgeMetadata cartridge)
{
	if (!encoder) return false;
	if (encoder->ended || encoder->in_save || encoder->root_chunks != ORB_ROOT_METADATA) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_SEQUENCE);
	if (!orb_cartridge_metadata_valid(cartridge)) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_ARGUMENT);
	Orb_CartridgeMetadataFixed fixed = orb_cartridge_fixed(cartridge);
	u8 payload[ORB_CARTRIDGE_FIXED_SIZE];
	ByteStream writer = byte_stream_writer(byte_span(payload, sizeof(payload)));
	orb_transfer_cartridge_fixed(&writer, &fixed);
	Assert(!writer.failed && writer.cursor == writer.size);
	if (!orb_encoder_write_bytes_chunk(encoder, ORB_CHUNK_CARTRIDGE, ORB_CARTRIDGE_VERSION, ORB_CHUNK_REQUIRED | ORB_CHUNK_HAS_CRC32, byte_span(payload, sizeof(payload)))) return false;
	encoder->cartridge = cartridge;
	orb_cartridge_hash_begin(&encoder->cartridge_hash, cartridge);
	encoder->root_chunks |= ORB_ROOT_CARTRIDGE;
	return true;
}

b32 orb_write_prg_rom_chunk(Orb_Encoder *encoder, ByteSpan prg_rom)
{
	if (!encoder) return false;
	if (encoder->ended || encoder->in_save || encoder->root_chunks != (ORB_ROOT_METADATA | ORB_ROOT_CARTRIDGE)) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_SEQUENCE);
	if (!prg_rom.data || prg_rom.size != encoder->cartridge.prg_rom_size) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_ARGUMENT);
	if (!orb_encoder_write_bytes_chunk(encoder, ORB_CHUNK_PRG_ROM, ORB_PRG_ROM_VERSION, ORB_CHUNK_REQUIRED | ORB_CHUNK_HAS_CRC32, prg_rom)) return false;
	sha256_update(&encoder->cartridge_hash, prg_rom);
	encoder->root_chunks |= ORB_ROOT_PRG_ROM;
	return true;
}

b32 orb_write_chr_rom_chunk(Orb_Encoder *encoder, ByteSpan chr_rom)
{
	if (!encoder) return false;
	if (encoder->ended || encoder->in_save || encoder->root_chunks != (ORB_ROOT_METADATA | ORB_ROOT_CARTRIDGE | ORB_ROOT_PRG_ROM)) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_SEQUENCE);
	if (chr_rom.size != encoder->cartridge.chr_rom_size || chr_rom.size && !chr_rom.data) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_ARGUMENT);
	if (!orb_encoder_write_bytes_chunk(encoder, ORB_CHUNK_CHR_ROM, ORB_CHR_ROM_VERSION, ORB_CHUNK_REQUIRED | ORB_CHUNK_HAS_CRC32, chr_rom)) return false;
	sha256_update(&encoder->cartridge_hash, chr_rom);
	if (!hash256_match(encoder->content_hash, sha256_final(&encoder->cartridge_hash))) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_ARGUMENT);
	encoder->root_chunks |= ORB_ROOT_CHR_ROM;
	return true;
}

b32 orb_begin_save_chunk(Orb_Encoder *encoder)
{
	if (!encoder || encoder->ended || encoder->in_save || encoder->root_chunks != ORB_ROOT_ALL) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_SEQUENCE);
	encoder->save_header_position = orb_encoder_begin_chunk(encoder);
	if (encoder->result.status != ORB_STATUS_OK) return false;
	encoder->save_chunks = 0;
	encoder->in_save = true;
	return true;
}

b32 orb_write_save_metadata_chunk(Orb_Encoder *encoder, Orb_SaveMetadata metadata)
{
	if (!encoder) return false;
	if (encoder->ended || !encoder->in_save || encoder->save_chunks) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_SEQUENCE);
	if (!orb_save_metadata_valid(metadata)) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_ARGUMENT);
	Orb_SaveMetadataFixed fixed = {
		.kind = metadata.kind,
		.id = metadata.id,
		.created_unix_ms = metadata.created_unix_ms,
		.updated_unix_ms = metadata.updated_unix_ms,
		.play_time_ms = metadata.play_time_ms,
	};
	u8 payload[ORB_SAVE_METADATA_FIXED_SIZE];
	ByteStream writer = byte_stream_writer(byte_span(payload, sizeof(payload)));
	orb_transfer_save_metadata_fixed(&writer, &fixed);
	Assert(!writer.failed && writer.cursor == writer.size);
	if (!orb_encoder_write_bytes_chunk(encoder, ORB_CHUNK_SAVE_METADATA, ORB_SAVE_METADATA_VERSION, ORB_CHUNK_REQUIRED | ORB_CHUNK_HAS_CRC32, byte_span(payload, sizeof(payload)))) return false;
	encoder->save_chunks |= ORB_SAVE_METADATA;
	return true;
}

b32 orb_write_save_state_chunk(Orb_Encoder *encoder, ByteSpan state)
{
	if (!encoder) return false;
	if (encoder->ended || !encoder->in_save || encoder->save_chunks != ORB_SAVE_METADATA) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_SEQUENCE);
	if (!state.data || !state.size) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_ARGUMENT);
	if (!orb_encoder_write_bytes_chunk(encoder, ORB_CHUNK_STATE, ORB_STATE_VERSION, ORB_CHUNK_REQUIRED | ORB_CHUNK_HAS_CRC32, state)) return false;
	encoder->save_chunks |= ORB_SAVE_STATE;
	return true;
}

b32 orb_write_save_thumbnail_chunk(Orb_Encoder *encoder, Orb_Thumbnail thumbnail)
{
	if (!encoder) return false;
	if (encoder->ended || !encoder->in_save || encoder->save_chunks != (ORB_SAVE_METADATA | ORB_SAVE_STATE)) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_SEQUENCE);
	if (!thumbnail.pixels.size || !orb_thumbnail_valid(thumbnail)) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_ARGUMENT);
	u64 payload_size = ORB_THUMBNAIL_FIXED_SIZE + thumbnail.pixels.size;
	u64 header = orb_encoder_begin_chunk(encoder);
	ByteSpan payload = orb_encoder_push(encoder, payload_size);
	if (encoder->result.status != ORB_STATUS_OK) return false;
	Orb_ThumbnailFixed fixed = {
		.width = thumbnail.width,
		.height = thumbnail.height,
		.stride = thumbnail.stride,
		.format = thumbnail.format,
	};
	ByteStream writer = byte_stream_writer(payload);
	orb_transfer_thumbnail_fixed(&writer, &fixed);
	byte_transfer_bytes(&writer, thumbnail.pixels);
	Assert(!writer.failed && writer.cursor == writer.size);
	if (!orb_encoder_end_chunk(encoder, header, ORB_CHUNK_THUMBNAIL, ORB_THUMBNAIL_VERSION, ORB_CHUNK_HAS_CRC32)) return false;
	encoder->save_chunks |= ORB_SAVE_THUMBNAIL;
	return true;
}

b32 orb_end_save_chunk(Orb_Encoder *encoder)
{
	if (!encoder) return false;
	if (encoder->ended || !encoder->in_save) return orb_encoder_fail(encoder, ORB_STATUS_INVALID_SEQUENCE);
	if (!(encoder->save_chunks & ORB_SAVE_METADATA) || !(encoder->save_chunks & ORB_SAVE_STATE)) return orb_encoder_fail(encoder, ORB_STATUS_MISSING_CHUNK);
	if (!orb_encoder_end_chunk(encoder, encoder->save_header_position, ORB_CHUNK_SAVE, ORB_SAVE_VERSION, ORB_CHUNK_REQUIRED | ORB_CHUNK_HAS_CRC32)) return false;
	encoder->save_header_position = 0;
	encoder->save_chunks = 0;
	encoder->in_save = false;
	return true;
}

void orb_cancel_encoding(Orb_Encoder *encoder)
{
	if (!encoder || encoder->ended) return;
	Assert(!encoder->arena || encoder->arena->position == encoder->expected_position);
	if (encoder->arena) encoder->arena->position = encoder->rollback_position;
	encoder->ended = true;
}

Orb_Result orb_end_encoding(Orb_Encoder *encoder, ByteSpan *output)
{
	if (output) *output = (ByteSpan) {};
	if (!encoder) return orb_result(ORB_STATUS_INVALID_ARGUMENT, 0);
	if (!output)
	{
		orb_encoder_fail(encoder, ORB_STATUS_INVALID_ARGUMENT);
		Orb_Result result = encoder->result;
		orb_cancel_encoding(encoder);
		return result;
	}
	if (encoder->ended) return orb_result(ORB_STATUS_INVALID_SEQUENCE, encoder->result.offset);
	if (encoder->in_save) orb_encoder_fail(encoder, ORB_STATUS_INVALID_SEQUENCE);
	if (encoder->root_chunks != ORB_ROOT_ALL) orb_encoder_fail(encoder, ORB_STATUS_MISSING_CHUNK);
	if (encoder->result.status == ORB_STATUS_OK)
	{
		Assert(encoder->arena->position == encoder->expected_position);
		*output = byte_span(encoder->arena->memory + encoder->start_position, encoder->arena->position - encoder->start_position);
	}
	else
	{
		Assert(!encoder->arena || encoder->arena->position == encoder->expected_position);
		if (encoder->arena) encoder->arena->position = encoder->rollback_position;
	}
	encoder->ended = true;
	return encoder->result;
}

Orb_Result orb_begin_decoding(Orb_Decoder *decoder, ByteSpan source)
{
	if (!decoder) return orb_result(ORB_STATUS_INVALID_ARGUMENT, 0);
	*decoder = (Orb_Decoder) { .source = source, .result = orb_result(ORB_STATUS_OK, 0) };
	decoder->root = decoder;
	if (!source.data) decoder->result = orb_result(ORB_STATUS_INVALID_ARGUMENT, 0);
	else if (source.size > ORB_MAX_FILE_SIZE || source.size < ORB_FILE_HEADER_SIZE) decoder->result = orb_result(ORB_STATUS_INVALID_FORMAT, source.size < ORB_FILE_HEADER_SIZE ? source.size : 0);
	if (decoder->result.status != ORB_STATUS_OK) return decoder->result;
	ByteStream reader = byte_stream_reader(source);
	Orb_FileHeader header = {};
	orb_transfer_file_header(&reader, &header);
	if (reader.failed || header.magic != ORB_MAGIC || header.header_size < ORB_FILE_HEADER_SIZE || header.header_size & (ORB_ALIGNMENT - 1) || header.header_size > source.size || header.flags || header.reserved)
	{
		decoder->result = orb_result(ORB_STATUS_INVALID_FORMAT, 0);
		return decoder->result;
	}
	if (header.version != ORB_FILE_VERSION_CURRENT)
	{
		decoder->result = orb_result(ORB_STATUS_UNSUPPORTED_VERSION, 4);
		return decoder->result;
	}
	decoder->version = header.version;
	decoder->cursor = header.header_size;
	return decoder->result;
}

Orb_Result orb_begin_container_decoding(Orb_Decoder *decoder, Orb_Decoder *parent, Orb_Chunk container)
{
	if (!decoder || !parent || !parent->root || parent->ended || parent->child_active || parent->result.status != ORB_STATUS_OK || !container.data.data) return orb_result(ORB_STATUS_INVALID_ARGUMENT, container.offset);
	for (Orb_Decoder *ancestor = parent; ancestor; ancestor = ancestor->parent) if (decoder == ancestor) return orb_result(ORB_STATUS_INVALID_ARGUMENT, container.offset);
	*decoder = (Orb_Decoder) {
		.source = container.data,
		.base_offset = container.payload_offset,
		.result = orb_result(ORB_STATUS_OK, 0),
		.version = parent->version,
		.container = true,
		.root = parent->root,
		.parent = parent,
	};
	parent->child_active = true;
	return decoder->result;
}

static b32 orb_decoder_fail(Orb_Decoder *decoder, Orb_Status status, u64 local_offset)
{
	if (decoder && decoder->result.status == ORB_STATUS_OK) decoder->result = orb_result(status, decoder->base_offset + local_offset);
	return false;
}

b32 orb_read_chunk(Orb_Decoder *decoder, Orb_Chunk *chunk)
{
	if (chunk) *chunk = (Orb_Chunk) {};
	if (!decoder || !chunk) return false;
	if (decoder->ended || decoder->child_active) return orb_decoder_fail(decoder, ORB_STATUS_INVALID_SEQUENCE, decoder->cursor);
	if (decoder->result.status != ORB_STATUS_OK || decoder->cursor == decoder->source.size) return false;
	if (++decoder->root->chunk_count > ORB_MAX_CHUNKS) return orb_decoder_fail(decoder, ORB_STATUS_INVALID_FORMAT, decoder->cursor);
	u64 chunk_offset = decoder->cursor;
	ByteStream reader = byte_stream_reader(byte_span(decoder->source.data + chunk_offset, decoder->source.size - chunk_offset));
	Orb_ChunkHeader header = {};
	orb_transfer_chunk_header(&reader, &header);
	if (reader.failed || header.header_size < ORB_CHUNK_HEADER_SIZE || header.header_size & (ORB_ALIGNMENT - 1)) return orb_decoder_fail(decoder, ORB_STATUS_INVALID_FORMAT, chunk_offset);
	byte_stream_skip(&reader, header.header_size - ORB_CHUNK_HEADER_SIZE);
	if (reader.failed) return orb_decoder_fail(decoder, ORB_STATUS_INVALID_FORMAT, chunk_offset);
	u64 payload_offset = chunk_offset + header.header_size;
	ByteSpan payload = byte_stream_take(&reader, header.stored_size);
	if (reader.failed) return orb_decoder_fail(decoder, ORB_STATUS_INVALID_FORMAT, chunk_offset);
	if (header.flags & ORB_CHUNK_HAS_CRC32)
	{
		if (header.checksum != orb_crc32(payload.data, payload.size)) return orb_decoder_fail(decoder, ORB_STATUS_CHECKSUM_MISMATCH, chunk_offset);
	}
	else if (header.checksum) return orb_decoder_fail(decoder, ORB_STATUS_INVALID_FORMAT, chunk_offset);
	u64 end = payload_offset + header.stored_size;
	u64 aligned = end;
	if (!orb_align_size(&aligned) || aligned > decoder->source.size) return orb_decoder_fail(decoder, ORB_STATUS_INVALID_FORMAT, end);
	for (u64 index = end; index < aligned; index ++) if (decoder->source.data[index]) return orb_decoder_fail(decoder, ORB_STATUS_INVALID_FORMAT, index);
	decoder->cursor = aligned;
	*chunk = (Orb_Chunk) {
		.type = header.type,
		.version = header.version,
		.flags = header.flags,
		.codec = header.codec,
		.unpacked_size = header.unpacked_size,
		.offset = decoder->base_offset + chunk_offset,
		.payload_offset = decoder->base_offset + payload_offset,
		.encoded = byte_span(decoder->source.data + chunk_offset, header.header_size + header.stored_size),
		.data = payload,
	};
	return true;
}

Orb_Result orb_end_decoding(Orb_Decoder *decoder)
{
	if (!decoder) return orb_result(ORB_STATUS_INVALID_ARGUMENT, 0);
	if (decoder->ended) return orb_result(ORB_STATUS_INVALID_SEQUENCE, decoder->result.offset);
	if (decoder->child_active) orb_decoder_fail(decoder, ORB_STATUS_INVALID_SEQUENCE, decoder->cursor);
	if (decoder->result.status == ORB_STATUS_OK && decoder->cursor != decoder->source.size) orb_decoder_fail(decoder, ORB_STATUS_INVALID_SEQUENCE, decoder->cursor);
	decoder->ended = true;
	if (decoder->parent)
	{
		Assert(decoder->parent->child_active);
		decoder->parent->child_active = false;
		if (decoder->result.status != ORB_STATUS_OK && decoder->parent->result.status == ORB_STATUS_OK) decoder->parent->result = decoder->result;
	}
	return decoder->result;
}

static Orb_Result orb_validate_typed_chunk(Orb_Chunk chunk, u32 type, u16 version, u64 maximum_size)
{
	if (chunk.type != type) return orb_result(ORB_STATUS_INVALID_ARGUMENT, chunk.offset);
	if (chunk.flags & ~ORB_CHUNK_KNOWN_FLAGS || chunk.codec != ORB_CODEC_NONE || chunk.data.size != chunk.unpacked_size || chunk.version != version || chunk.data.size > maximum_size) return orb_result(ORB_STATUS_UNSUPPORTED_VERSION, chunk.offset);
	return orb_result(ORB_STATUS_OK, 0);
}

Orb_Result orb_decode_metadata_chunk(Orb_Chunk chunk, Orb_Metadata *metadata)
{
	if (metadata) *metadata = (Orb_Metadata) {};
	if (!metadata) return orb_result(ORB_STATUS_INVALID_ARGUMENT, chunk.offset);
	Orb_Result result = orb_validate_typed_chunk(chunk, ORB_CHUNK_METADATA, ORB_METADATA_VERSION, ORB_MAX_METADATA_SIZE);
	if (result.status != ORB_STATUS_OK) return result;
	if (chunk.data.size < ORB_METADATA_FIXED_SIZE) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk.payload_offset);
	ByteStream reader = byte_stream_reader(chunk.data);
	Orb_MetadataFixed fixed = {};
	orb_transfer_metadata_fixed(&reader, &fixed);
	u64 strings_size = (u64)fixed.title_size + fixed.source_path_size;
	if (reader.failed || fixed.flags || strings_size != chunk.data.size - ORB_METADATA_FIXED_SIZE) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk.payload_offset);
	metadata->content_hash = fixed.content_hash;
	metadata->first_played_unix_ms = fixed.first_played_unix_ms;
	metadata->last_played_unix_ms = fixed.last_played_unix_ms;
	metadata->play_time_ms = fixed.play_time_ms;
	ByteSpan title = byte_stream_take(&reader, fixed.title_size);
	ByteSpan source_path = byte_stream_take(&reader, fixed.source_path_size);
	metadata->title = str_from_data(title.data, fixed.title_size);
	metadata->source_path = str_from_data(source_path.data, fixed.source_path_size);
	if (reader.failed || !orb_metadata_valid(*metadata)) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk.payload_offset);
	return orb_result(ORB_STATUS_OK, 0);
}

Orb_Result orb_decode_cartridge_chunk(Orb_Chunk chunk, Orb_CartridgeMetadata *cartridge)
{
	if (cartridge) *cartridge = (Orb_CartridgeMetadata) {};
	if (!cartridge) return orb_result(ORB_STATUS_INVALID_ARGUMENT, chunk.offset);
	Orb_Result result = orb_validate_typed_chunk(chunk, ORB_CHUNK_CARTRIDGE, ORB_CARTRIDGE_VERSION, ORB_CARTRIDGE_FIXED_SIZE);
	if (result.status != ORB_STATUS_OK) return result;
	if (chunk.data.size != ORB_CARTRIDGE_FIXED_SIZE) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk.payload_offset);
	ByteStream reader = byte_stream_reader(chunk.data);
	Orb_CartridgeMetadataFixed fixed = {};
	orb_transfer_cartridge_fixed(&reader, &fixed);
	if (reader.failed || fixed.flags & ~ORB_CARTRIDGE_KNOWN_FLAGS) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk.payload_offset);
	*cartridge = (Orb_CartridgeMetadata) {
		.mapper = fixed.mapper,
		.prg_rom_size = fixed.prg_rom_size,
		.chr_rom_size = fixed.chr_rom_size,
		.vertical_mirroring = !!(fixed.flags & ORB_CARTRIDGE_VERTICAL_MIRRORING),
		.four_screen = !!(fixed.flags & ORB_CARTRIDGE_FOUR_SCREEN),
	};
	if (!orb_cartridge_metadata_valid(*cartridge)) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk.payload_offset);
	return orb_result(ORB_STATUS_OK, 0);
}

static Orb_Result orb_decode_rom_chunk(Orb_Chunk chunk, u32 type, u16 version, b32 allow_empty, ByteSpan *rom)
{
	if (rom) *rom = (ByteSpan) {};
	if (!rom) return orb_result(ORB_STATUS_INVALID_ARGUMENT, chunk.offset);
	Orb_Result result = orb_validate_typed_chunk(chunk, type, version, ORB_MAX_FILE_SIZE);
	if (result.status != ORB_STATUS_OK) return result;
	if (!allow_empty && !chunk.data.size) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk.payload_offset);
	*rom = chunk.data;
	return orb_result(ORB_STATUS_OK, 0);
}

Orb_Result orb_decode_prg_rom_chunk(Orb_Chunk chunk, ByteSpan *prg_rom)
{
	return orb_decode_rom_chunk(chunk, ORB_CHUNK_PRG_ROM, ORB_PRG_ROM_VERSION, false, prg_rom);
}

Orb_Result orb_decode_chr_rom_chunk(Orb_Chunk chunk, ByteSpan *chr_rom)
{
	return orb_decode_rom_chunk(chunk, ORB_CHUNK_CHR_ROM, ORB_CHR_ROM_VERSION, true, chr_rom);
}

Orb_Result orb_decode_save_metadata_chunk(Orb_Chunk chunk, Orb_SaveMetadata *metadata)
{
	if (metadata) *metadata = (Orb_SaveMetadata) {};
	if (!metadata) return orb_result(ORB_STATUS_INVALID_ARGUMENT, chunk.offset);
	Orb_Result result = orb_validate_typed_chunk(chunk, ORB_CHUNK_SAVE_METADATA, ORB_SAVE_METADATA_VERSION, ORB_SAVE_METADATA_FIXED_SIZE);
	if (result.status != ORB_STATUS_OK) return result;
	if (chunk.data.size != ORB_SAVE_METADATA_FIXED_SIZE) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk.payload_offset);
	ByteStream reader = byte_stream_reader(chunk.data);
	Orb_SaveMetadataFixed fixed = {};
	orb_transfer_save_metadata_fixed(&reader, &fixed);
	metadata->id = fixed.id;
	metadata->kind = (Orb_SaveKind)fixed.kind;
	metadata->created_unix_ms = fixed.created_unix_ms;
	metadata->updated_unix_ms = fixed.updated_unix_ms;
	metadata->play_time_ms = fixed.play_time_ms;
	if (reader.failed || fixed.flags || !orb_save_metadata_valid(*metadata)) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk.payload_offset);
	return orb_result(ORB_STATUS_OK, 0);
}

Orb_Result orb_decode_save_state_chunk(Orb_Chunk chunk, ByteSpan *state)
{
	if (state) *state = (ByteSpan) {};
	if (!state) return orb_result(ORB_STATUS_INVALID_ARGUMENT, chunk.offset);
	Orb_Result result = orb_validate_typed_chunk(chunk, ORB_CHUNK_STATE, ORB_STATE_VERSION, ORB_MAX_FILE_SIZE);
	if (result.status != ORB_STATUS_OK) return result;
	if (!chunk.data.size) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk.payload_offset);
	*state = chunk.data;
	return orb_result(ORB_STATUS_OK, 0);
}

Orb_Result orb_decode_thumbnail_chunk(Orb_Chunk chunk, Orb_Thumbnail *thumbnail)
{
	if (thumbnail) *thumbnail = (Orb_Thumbnail) {};
	if (!thumbnail) return orb_result(ORB_STATUS_INVALID_ARGUMENT, chunk.offset);
	Orb_Result result = orb_validate_typed_chunk(chunk, ORB_CHUNK_THUMBNAIL, ORB_THUMBNAIL_VERSION, ORB_MAX_THUMBNAIL_SIZE + ORB_THUMBNAIL_FIXED_SIZE);
	if (result.status != ORB_STATUS_OK) return result;
	if (chunk.data.size < ORB_THUMBNAIL_FIXED_SIZE) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk.payload_offset);
	ByteStream reader = byte_stream_reader(chunk.data);
	Orb_ThumbnailFixed fixed = {};
	orb_transfer_thumbnail_fixed(&reader, &fixed);
	thumbnail->width = fixed.width;
	thumbnail->height = fixed.height;
	thumbnail->stride = fixed.stride;
	thumbnail->format = (Orb_PixelFormat)fixed.format;
	thumbnail->pixels = byte_stream_take(&reader, byte_stream_remaining(&reader));
	if (reader.failed || !orb_thumbnail_valid(*thumbnail)) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk.payload_offset);
	return orb_result(ORB_STATUS_OK, 0);
}

const char *orb_status_string(Orb_Status status)
{
	switch (status)
	{
		case ORB_STATUS_OK:                  return "ok";
		case ORB_STATUS_INVALID_ARGUMENT:    return "invalid argument";
		case ORB_STATUS_INVALID_FORMAT:      return "invalid ORB format";
		case ORB_STATUS_UNSUPPORTED_VERSION: return "unsupported ORB version";
		case ORB_STATUS_UNSUPPORTED_CHUNK:   return "unsupported critical ORB chunk";
		case ORB_STATUS_MISSING_CHUNK:       return "missing required ORB chunk";
		case ORB_STATUS_DUPLICATE_CHUNK:     return "duplicate ORB chunk";
		case ORB_STATUS_CHECKSUM_MISMATCH:   return "ORB chunk checksum mismatch";
		case ORB_STATUS_INVALID_SEQUENCE:    return "invalid ORB API sequence";
		case ORB_STATUS_OUTPUT_TOO_LARGE:    return "ORB output is too large";
	}
	return "unknown ORB error";
}
