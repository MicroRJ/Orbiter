#include "orb.h"


#if 0
static b32 orb_stream_ready(ByteStream *stream, ByteStreamMode mode)
{
	Assert(stream && stream->mode == mode && !stream->ended);
	return !stream->failed;
}

static b32 orb_fail(ByteStream *stream)
{
	if (stream) stream->failed = true;
	return false;
}

static u64 orb_padding(u64 position)
{
	return (ORB_ALIGNMENT - (position & (ORB_ALIGNMENT - 1))) & (ORB_ALIGNMENT - 1);
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

void orb_write_header(ByteStream *writer)
{
	if (!orb_stream_ready(writer, BYTE_STREAM_WRITE)) return;
	Orb_FileHeader header = {
		.magic = ORB_MAGIC,
		.version = ORB_FILE_VERSION_CURRENT,
		.header_size = ORB_FILE_HEADER_SIZE,
	};
	orb_transfer_file_header(writer, &header);
}

b32 orb_read_header(ByteStream *reader)
{
	if (!orb_stream_ready(reader, BYTE_STREAM_READ)) return false;
	Orb_FileHeader header = {};
	orb_transfer_file_header(reader, &header);
	if (reader->failed) return false;
	if (header.magic != ORB_MAGIC || header.version != ORB_FILE_VERSION_CURRENT ||
		header.header_size < ORB_FILE_HEADER_SIZE || header.header_size & (ORB_ALIGNMENT - 1)) return orb_fail(reader);
	byte_stream_skip(reader, header.header_size - ORB_FILE_HEADER_SIZE);
	return !reader->failed;
}

Orb_ChunkToken orb_begin_chunk(ByteStream *writer, u32 tag, u16 version, u16 flags)
{
	Orb_ChunkToken chunk = {
		.tag = tag,
		.version = version,
		.flags = flags,
	};
	if (!orb_stream_ready(writer, BYTE_STREAM_WRITE)) return chunk;
	chunk.header_offset = writer->cursor;
	Orb_ChunkHeader header = {
		.tag = tag,
		.version = version,
		.flags = flags,
		.codec = ORB_CODEC_NONE,
		.header_size = ORB_CHUNK_HEADER_SIZE,
	};
	orb_transfer_chunk_header(writer, &header);
	chunk.data_offset = writer->cursor;
	return chunk;
}

void orb_end_chunk(ByteStream *writer, Orb_ChunkToken chunk)
{
	if (!orb_stream_ready(writer, BYTE_STREAM_WRITE)) return;
	Assert(chunk.data_offset >= chunk.header_offset && chunk.data_offset - chunk.header_offset == ORB_CHUNK_HEADER_SIZE);
	Assert(chunk.data_offset <= writer->cursor && chunk.header_offset <= writer->size && ORB_CHUNK_HEADER_SIZE <= writer->size - chunk.header_offset);

	u64 payload_size = writer->cursor - chunk.data_offset;
	Orb_ChunkHeader header = {
		.tag = chunk.tag,
		.version = chunk.version,
		.flags = chunk.flags,
		.stored_size = payload_size,
		.unpacked_size = payload_size,
		.checksum = chunk.flags & ORB_CHUNK_HAS_CRC32 ? orb_crc32(writer->data + chunk.data_offset, payload_size) : 0,
		.codec = ORB_CODEC_NONE,
		.header_size = ORB_CHUNK_HEADER_SIZE,
	};
	ByteStream patch = byte_stream_writer(byte_span(writer->data + chunk.header_offset, ORB_CHUNK_HEADER_SIZE));
	orb_transfer_chunk_header(&patch, &header);
	if (patch.failed)
	{
		orb_fail(writer);
		return;
	}

	u64 padding_size = orb_padding(writer->cursor);
	if (padding_size)
	{
		ByteSpan padding = byte_stream_take(writer, padding_size);
		if (padding.data) memory_zero(padding.data, padding.size);
	}
}

void orb_write_chunk(ByteStream *writer, u32 tag, u16 version, u16 flags, ByteSpan data)
{
	if (!orb_stream_ready(writer, BYTE_STREAM_WRITE)) return;
	Orb_ChunkToken chunk = orb_begin_chunk(writer, tag, version, flags);
	byte_transfer_bytes(writer, data);
	orb_end_chunk(writer, chunk);
}


b32 orb_end_read_chunk(ByteStream *reader, Orb_Chunk chunk)
{
	if (!orb_stream_ready(reader, BYTE_STREAM_READ)) return false;
	Assert(chunk.data_offset <= reader->size && chunk.data_size == reader->size - chunk.data_offset);
	Assert(reader->size <= chunk.stream_size);
	Assert(reader->cursor == reader->size);
	u32 checksum = chunk.flags & ORB_CHUNK_HAS_CRC32 ? orb_crc32(reader->data + chunk.data_offset, chunk.data_size) : 0;
	reader->size = chunk.stream_size;
	if (checksum != chunk.checksum) return orb_fail(reader);
	byte_stream_skip(reader, orb_padding(reader->cursor));
	return !reader->failed;
}
#endif