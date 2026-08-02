#include "orb.h"

enum
{
	ORB_MAGIC = ORB_FOURCC('O', 'R', 'B', 'S'),
	ORB_VERSION = 1,
	ORB_HEADER_SIZE = 16,
	ORB_CHUNK_HEADER_SIZE = 32,
	ORB_META_VERSION = 1,
	ORB_META_FIXED_SIZE = 104,
	ORB_THUMBNAIL_VERSION = 1,
	ORB_THUMBNAIL_FIXED_SIZE = 16,
	ORB_STATE_VERSION = 1,
	ORB_CHUNK_REQUIRED = 1 << 0,
	ORB_CHUNK_HAS_CRC32 = 1 << 1,
	ORB_CHUNK_KNOWN_FLAGS = ORB_CHUNK_REQUIRED | ORB_CHUNK_HAS_CRC32,
	ORB_CODEC_NONE = 0,
	ORB_CHUNK_META = ORB_FOURCC('M', 'E', 'T', 'A'),
	ORB_CHUNK_THUMBNAIL = ORB_FOURCC('T', 'H', 'M', 'B'),
	ORB_CHUNK_STATE = ORB_FOURCC('S', 'T', 'A', 'T'),
	ORB_ALIGNMENT = 8,
	ORB_MAX_CHUNKS = 1024,
};

static const u64 ORB_MAX_FILE_SIZE = MB(256);
static const u64 ORB_MAX_META_SIZE = KiB(64);
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
	u32 system;
	u32 kind;
	u32 flags;
	u32 reserved;
	Orb_Id id;
	Orb_Hash256 content_hash;
	u64 created_unix_ms;
	u64 first_played_unix_ms;
	u64 last_played_unix_ms;
	u64 play_time_ms;
	u32 title_size;
	u32 source_path_size;
}
Orb_MetaFixed;

typedef struct
{
	u32 width;
	u32 height;
	u32 stride;
	u32 format;
}
Orb_ThumbnailFixed;

STATIC_ASSERT(4 + 2 + 2 + 4 + 4 == ORB_HEADER_SIZE);
STATIC_ASSERT(4 + 2 + 2 + 8 + 8 + 4 + 2 + 2 == ORB_CHUNK_HEADER_SIZE);
STATIC_ASSERT(4 * 4 + 16 + 32 + 8 * 4 + 4 * 2 == ORB_META_FIXED_SIZE);
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

static void orb_transfer_meta_fixed(ByteStream *stream, Orb_MetaFixed *metadata)
{
	byte_transfer_u32(stream, &metadata->system);
	byte_transfer_u32(stream, &metadata->kind);
	byte_transfer_u32(stream, &metadata->flags);
	byte_transfer_u32(stream, &metadata->reserved);
	byte_transfer_bytes(stream, byte_span(metadata->id.bytes, sizeof(metadata->id.bytes)));
	byte_transfer_bytes(stream, byte_span(metadata->content_hash.bytes, sizeof(metadata->content_hash.bytes)));
	byte_transfer_u64(stream, &metadata->created_unix_ms);
	byte_transfer_u64(stream, &metadata->first_played_unix_ms);
	byte_transfer_u64(stream, &metadata->last_played_unix_ms);
	byte_transfer_u64(stream, &metadata->play_time_ms);
	byte_transfer_u32(stream, &metadata->title_size);
	byte_transfer_u32(stream, &metadata->source_path_size);
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

static b32 orb_add_chunk_size(u64 *size, u64 payload_size)
{
	return orb_add_size(size, ORB_CHUNK_HEADER_SIZE) && orb_add_size(size, payload_size) && orb_align_size(size);
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

static void orb_writer_align(ByteStream *writer)
{
	u64 aligned = writer->cursor;
	Assert(orb_align_size(&aligned));
	u64 padding = aligned - writer->cursor;
	ByteSpan destination = byte_stream_take(writer, padding);
	Assert(!writer->failed);
	memory_zero(destination.data, destination.size);
}

static u64 orb_writer_begin_chunk(ByteStream *writer, Orb_ChunkHeader *header)
{
	u64 header_offset = writer->cursor;
	orb_transfer_chunk_header(writer, header);
	Assert(!writer->failed && writer->cursor - header_offset == ORB_CHUNK_HEADER_SIZE);
	return header_offset;
}

static void orb_writer_end_chunk(ByteStream *writer, u64 header_offset, Orb_ChunkHeader *header)
{
	Assert(header->header_size == ORB_CHUNK_HEADER_SIZE);
	u64 payload_offset = header_offset + ORB_CHUNK_HEADER_SIZE;
	Assert(writer->cursor == payload_offset + header->stored_size);
	header->checksum = orb_crc32(writer->data + payload_offset, header->stored_size);
	ByteStream patch = byte_stream_writer(byte_span(writer->data + header_offset, ORB_CHUNK_HEADER_SIZE));
	orb_transfer_chunk_header(&patch, header);
	Assert(!patch.failed && patch.cursor == patch.size);
	orb_writer_align(writer);
}

static b32 orb_save_kind_valid(Orb_SaveKind kind)
{
	return kind == ORB_SAVE_RESUME || kind == ORB_SAVE_MANUAL;
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

static Orb_Result orb_decode_meta_fixed(ByteSpan source, u64 payload_size, Orb_Metadata *metadata, u32 *title_size, u32 *source_path_size)
{
	if (payload_size < ORB_META_FIXED_SIZE || source.size < ORB_META_FIXED_SIZE) return orb_result(ORB_STATUS_INVALID_FORMAT, 0);
	ByteStream reader = byte_stream_reader(source);
	Orb_MetaFixed fixed = {};
	orb_transfer_meta_fixed(&reader, &fixed);
	if (reader.failed) return orb_result(ORB_STATUS_INVALID_FORMAT, 0);
	metadata->system = fixed.system;
	metadata->kind = (Orb_SaveKind)fixed.kind;
	metadata->id = fixed.id;
	metadata->content_hash = fixed.content_hash;
	metadata->created_unix_ms = fixed.created_unix_ms;
	metadata->first_played_unix_ms = fixed.first_played_unix_ms;
	metadata->last_played_unix_ms = fixed.last_played_unix_ms;
	metadata->play_time_ms = fixed.play_time_ms;
	*title_size = fixed.title_size;
	*source_path_size = fixed.source_path_size;
	u64 strings_size = (u64)*title_size + *source_path_size;
	if (!metadata->system || !orb_save_kind_valid(metadata->kind) || fixed.flags || fixed.reserved || strings_size != payload_size - ORB_META_FIXED_SIZE) return orb_result(ORB_STATUS_INVALID_FORMAT, 0);
	return orb_result(ORB_STATUS_OK, 0);
}

static Orb_Result orb_decode_meta(ByteSpan payload, Orb_Metadata *metadata)
{
	u32 title_size = 0;
	u32 source_path_size = 0;
	Orb_Result result = orb_decode_meta_fixed(payload, payload.size, metadata, &title_size, &source_path_size);
	if (result.status != ORB_STATUS_OK) return result;
	ByteStream reader = byte_stream_reader(payload);
	byte_stream_skip(&reader, ORB_META_FIXED_SIZE);
	ByteSpan title = byte_stream_take(&reader, title_size);
	ByteSpan source_path = byte_stream_take(&reader, source_path_size);
	if (reader.failed) return orb_result(ORB_STATUS_INVALID_FORMAT, 0);
	metadata->title = str_from_data(title.data, title_size);
	metadata->source_path = str_from_data(source_path.data, source_path_size);
	return orb_result(ORB_STATUS_OK, 0);
}

static Orb_Result orb_decode_thumbnail(ByteSpan payload, Orb_Thumbnail *thumbnail)
{
	if (payload.size < ORB_THUMBNAIL_FIXED_SIZE) return orb_result(ORB_STATUS_INVALID_FORMAT, 0);
	ByteStream reader = byte_stream_reader(payload);
	Orb_ThumbnailFixed fixed = {};
	orb_transfer_thumbnail_fixed(&reader, &fixed);
	thumbnail->width = fixed.width;
	thumbnail->height = fixed.height;
	thumbnail->stride = fixed.stride;
	thumbnail->format = (Orb_PixelFormat)fixed.format;
	thumbnail->pixels = byte_stream_take(&reader, byte_stream_remaining(&reader));
	if (reader.failed) return orb_result(ORB_STATUS_INVALID_FORMAT, 0);
	return orb_thumbnail_valid(*thumbnail) ? orb_result(ORB_STATUS_OK, 0) : orb_result(ORB_STATUS_INVALID_FORMAT, 0);
}

Orb_Result orb_encode(Arena *output_arena, Orb_Contents contents, ByteSpan *output)
{
	if (output) *output = (ByteSpan) {};
	if (!output_arena || !output || !contents.metadata.system || !orb_save_kind_valid(contents.metadata.kind) || !contents.state.data || !contents.state.size || !orb_thumbnail_valid(contents.thumbnail)) return orb_result(ORB_STATUS_INVALID_ARGUMENT, 0);

	u64 meta_size = ORB_META_FIXED_SIZE;
	if (!orb_add_size(&meta_size, contents.metadata.title.size) || !orb_add_size(&meta_size, contents.metadata.source_path.size) || meta_size > ORB_MAX_META_SIZE) return orb_result(ORB_STATUS_INVALID_ARGUMENT, 0);
	u64 thumbnail_size = 0;
	if (contents.thumbnail.pixels.size)
	{
		thumbnail_size = ORB_THUMBNAIL_FIXED_SIZE;
		if (!orb_add_size(&thumbnail_size, contents.thumbnail.pixels.size)) return orb_result(ORB_STATUS_INVALID_ARGUMENT, 0);
	}
	u64 total_size = ORB_HEADER_SIZE;
	if (!orb_add_chunk_size(&total_size, meta_size) || (thumbnail_size && !orb_add_chunk_size(&total_size, thumbnail_size)) || !orb_add_chunk_size(&total_size, contents.state.size) || total_size > ORB_MAX_FILE_SIZE) return orb_result(ORB_STATUS_INVALID_ARGUMENT, 0);

	u8 *encoded = arena_push(output_arena, total_size);
	ByteStream writer = byte_stream_writer(byte_span(encoded, total_size));
	Orb_FileHeader file_header = {
		.magic = ORB_MAGIC,
		.version = ORB_VERSION,
		.header_size = ORB_HEADER_SIZE,
	};
	orb_transfer_file_header(&writer, &file_header);

	Orb_ChunkHeader chunk_header = {
		.type = ORB_CHUNK_META,
		.version = ORB_META_VERSION,
		.flags = ORB_CHUNK_REQUIRED | ORB_CHUNK_HAS_CRC32,
		.stored_size = meta_size,
		.unpacked_size = meta_size,
		.codec = ORB_CODEC_NONE,
		.header_size = ORB_CHUNK_HEADER_SIZE,
	};
	u64 chunk = orb_writer_begin_chunk(&writer, &chunk_header);
	Orb_MetaFixed metadata = {
		.system = contents.metadata.system,
		.kind = contents.metadata.kind,
		.id = contents.metadata.id,
		.content_hash = contents.metadata.content_hash,
		.created_unix_ms = contents.metadata.created_unix_ms,
		.first_played_unix_ms = contents.metadata.first_played_unix_ms,
		.last_played_unix_ms = contents.metadata.last_played_unix_ms,
		.play_time_ms = contents.metadata.play_time_ms,
		.title_size = contents.metadata.title.size,
		.source_path_size = contents.metadata.source_path.size,
	};
	orb_transfer_meta_fixed(&writer, &metadata);
	if (contents.metadata.title.size) byte_transfer_bytes(&writer, byte_span(contents.metadata.title.data, contents.metadata.title.size));
	if (contents.metadata.source_path.size) byte_transfer_bytes(&writer, byte_span(contents.metadata.source_path.data, contents.metadata.source_path.size));
	orb_writer_end_chunk(&writer, chunk, &chunk_header);

	if (thumbnail_size)
	{
		chunk_header = (Orb_ChunkHeader) {
			.type = ORB_CHUNK_THUMBNAIL,
			.version = ORB_THUMBNAIL_VERSION,
			.flags = ORB_CHUNK_HAS_CRC32,
			.stored_size = thumbnail_size,
			.unpacked_size = thumbnail_size,
			.codec = ORB_CODEC_NONE,
			.header_size = ORB_CHUNK_HEADER_SIZE,
		};
		chunk = orb_writer_begin_chunk(&writer, &chunk_header);
		Orb_ThumbnailFixed thumbnail = {
			.width = contents.thumbnail.width,
			.height = contents.thumbnail.height,
			.stride = contents.thumbnail.stride,
			.format = contents.thumbnail.format,
		};
		orb_transfer_thumbnail_fixed(&writer, &thumbnail);
		byte_transfer_bytes(&writer, contents.thumbnail.pixels);
		orb_writer_end_chunk(&writer, chunk, &chunk_header);
	}

	chunk_header = (Orb_ChunkHeader) {
		.type = ORB_CHUNK_STATE,
		.version = ORB_STATE_VERSION,
		.flags = ORB_CHUNK_REQUIRED | ORB_CHUNK_HAS_CRC32,
		.stored_size = contents.state.size,
		.unpacked_size = contents.state.size,
		.codec = ORB_CODEC_NONE,
		.header_size = ORB_CHUNK_HEADER_SIZE,
	};
	chunk = orb_writer_begin_chunk(&writer, &chunk_header);
	byte_transfer_bytes(&writer, contents.state);
	orb_writer_end_chunk(&writer, chunk, &chunk_header);
	Assert(!writer.failed && writer.cursor == writer.size);
	*output = byte_span(encoded, total_size);
	return orb_result(ORB_STATUS_OK, 0);
}

Orb_Result orb_parse(ByteSpan source, Orb_Descriptor *descriptor)
{
	if (descriptor) *descriptor = (Orb_Descriptor) {};
	if (!descriptor || !source.data) return orb_result(ORB_STATUS_INVALID_ARGUMENT, 0);
	if (source.size > ORB_MAX_FILE_SIZE) return orb_result(ORB_STATUS_INVALID_FORMAT, 0);
	if (source.size < ORB_HEADER_SIZE) return orb_result(ORB_STATUS_INVALID_FORMAT, source.size);
	ByteStream reader = byte_stream_reader(source);
	Orb_FileHeader file_header = {};
	orb_transfer_file_header(&reader, &file_header);
	if (reader.failed || file_header.magic != ORB_MAGIC || file_header.header_size < ORB_HEADER_SIZE || file_header.header_size & (ORB_ALIGNMENT - 1) || file_header.header_size > source.size || file_header.flags || file_header.reserved) return orb_result(ORB_STATUS_INVALID_FORMAT, 0);
	if (file_header.version != ORB_VERSION) return orb_result(ORB_STATUS_UNSUPPORTED_VERSION, 4);
	byte_stream_skip(&reader, file_header.header_size - ORB_HEADER_SIZE);
	if (reader.failed) return orb_result(ORB_STATUS_INVALID_FORMAT, 0);

	Orb_Descriptor parsed = { .source = source };
	b32 found_meta = false;
	b32 found_state = false;
	b32 seen_meta = false;
	b32 seen_thumbnail = false;
	b32 seen_state = false;
	u32 chunk_count = 0;
	while (reader.cursor < reader.size)
	{
		if (++chunk_count > ORB_MAX_CHUNKS) return orb_result(ORB_STATUS_INVALID_FORMAT, reader.cursor);
		u64 chunk_offset = reader.cursor;
		Orb_ChunkHeader header = {};
		orb_transfer_chunk_header(&reader, &header);
		if (reader.failed) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk_offset);
		u32 type = header.type;
		u16 chunk_version = header.version;
		u16 chunk_flags = header.flags;
		u64 stored_size = header.stored_size;
		u64 unpacked_size = header.unpacked_size;
		u32 checksum = header.checksum;
		u16 codec = header.codec;
		u16 chunk_header_size = header.header_size;
		if (chunk_header_size < ORB_CHUNK_HEADER_SIZE || chunk_header_size & (ORB_ALIGNMENT - 1)) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk_offset);
		byte_stream_skip(&reader, chunk_header_size - ORB_CHUNK_HEADER_SIZE);
		if (reader.failed) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk_offset);
		ByteSpan payload = byte_stream_take(&reader, stored_size);
		if (reader.failed) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk_offset);
		if (chunk_flags & ORB_CHUNK_HAS_CRC32) {
			if (checksum != orb_crc32(payload.data, payload.size)) return orb_result(ORB_STATUS_CHECKSUM_MISMATCH, chunk_offset);
		} else if (checksum) return orb_result(ORB_STATUS_INVALID_FORMAT, chunk_offset);
		Orb_ChunkView view = {
			.type = type,
			.version = chunk_version,
			.flags = chunk_flags,
			.encoded = byte_span(source.data + chunk_offset, reader.cursor - chunk_offset),
			.data = payload,
		};

		Orb_Result result = orb_result(ORB_STATUS_OK, 0);
		if (type == ORB_CHUNK_META)
		{
			if (seen_meta) return orb_result(ORB_STATUS_DUPLICATE_CHUNK, chunk_offset);
			seen_meta = true;
			if (chunk_flags & ~ORB_CHUNK_KNOWN_FLAGS || codec != ORB_CODEC_NONE || stored_size != unpacked_size || chunk_version != ORB_META_VERSION || stored_size > ORB_MAX_META_SIZE) return orb_result(ORB_STATUS_UNSUPPORTED_VERSION, chunk_offset);
			parsed.meta_chunk = view;
			result = orb_decode_meta(payload, &parsed.metadata);
			found_meta = result.status == ORB_STATUS_OK;
		}
		else if (type == ORB_CHUNK_THUMBNAIL)
		{
			if (seen_thumbnail) return orb_result(ORB_STATUS_DUPLICATE_CHUNK, chunk_offset);
			seen_thumbnail = true;
			parsed.thumbnail_chunk = view;
			b32 supported = !(chunk_flags & ~ORB_CHUNK_KNOWN_FLAGS) && codec == ORB_CODEC_NONE && stored_size == unpacked_size && chunk_version == ORB_THUMBNAIL_VERSION && stored_size <= ORB_MAX_THUMBNAIL_SIZE + ORB_THUMBNAIL_FIXED_SIZE;
			if (supported)
			{
				result = orb_decode_thumbnail(payload, &parsed.thumbnail);
			}
			else if (chunk_flags & ORB_CHUNK_REQUIRED) return orb_result(ORB_STATUS_UNSUPPORTED_VERSION, chunk_offset);
		}
		else if (type == ORB_CHUNK_STATE)
		{
			if (seen_state) return orb_result(ORB_STATUS_DUPLICATE_CHUNK, chunk_offset);
			seen_state = true;
			if (chunk_flags & ~ORB_CHUNK_KNOWN_FLAGS || codec != ORB_CODEC_NONE || stored_size != unpacked_size || chunk_version != ORB_STATE_VERSION || !stored_size) return orb_result(ORB_STATUS_UNSUPPORTED_VERSION, chunk_offset);
			parsed.state_chunk = view;
			found_state = true;
		}
		else if (chunk_flags & ORB_CHUNK_REQUIRED) return orb_result(ORB_STATUS_UNSUPPORTED_CHUNK, chunk_offset);
		if (result.status != ORB_STATUS_OK) return orb_result(result.status, chunk_offset + ORB_CHUNK_HEADER_SIZE + result.offset);

		u64 aligned = reader.cursor;
		if (!orb_align_size(&aligned) || aligned > reader.size) return orb_result(ORB_STATUS_INVALID_FORMAT, reader.cursor);
		byte_stream_skip(&reader, aligned - reader.cursor);
		if (reader.failed) return orb_result(ORB_STATUS_INVALID_FORMAT, reader.cursor);
	}
	if (!found_meta || !found_state) return orb_result(ORB_STATUS_MISSING_CHUNK, source.size);
	*descriptor = parsed;
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
		default:                             return "unknown ORB error";
	}
}
