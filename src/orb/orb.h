#ifndef ORB_H
#define ORB_H

#include "base.h"

#define ORB_FOURCC(a, b, c, d) ((u32)(u8)(a) | (u32)(u8)(b) << 8 | (u32)(u8)(c) << 16 | (u32)(u8)(d) << 24)

enum
{
	ORB_FILE_VERSION_CURRENT = 3,

	ORB_CHUNK_METADATA        = ORB_FOURCC('M', 'E', 'T', 'A'),
	ORB_CHUNK_CARTRIDGE       = ORB_FOURCC('C', 'A', 'R', 'T'),
	ORB_CHUNK_PRG_ROM         = ORB_FOURCC('P', 'R', 'G', ' '),
	ORB_CHUNK_CHR_ROM         = ORB_FOURCC('C', 'H', 'R', ' '),
	ORB_CHUNK_SAVE            = ORB_FOURCC('S', 'A', 'V', 'E'),
	ORB_CHUNK_SAVE_METADATA   = ORB_FOURCC('S', 'M', 'E', 'T'),
	ORB_CHUNK_STATE           = ORB_FOURCC('S', 'T', 'A', 'T'),
	ORB_CHUNK_THUMBNAIL       = ORB_FOURCC('T', 'H', 'M', 'B'),

	ORB_CHUNK_REQUIRED  = 1 << 0,
	ORB_CHUNK_HAS_CRC32 = 1 << 1,
	ORB_CHUNK_KNOWN_FLAGS = ORB_CHUNK_REQUIRED | ORB_CHUNK_HAS_CRC32,
};

typedef struct
{
	u32 tag;
	u16 version;
	u16 flags;
	u32 checksum;
	u64 data_offset;
	u64 data_size;
	u64 stream_size;
}
Orb_Chunk;

typedef struct
{
	u64 header_offset;
	u64 data_offset;
	u32 tag;
	u16 version;
	u16 flags;
}
Orb_ChunkToken;

void orb_write_header(ByteStream *writer);
b32 orb_read_header(ByteStream *reader);
Orb_ChunkToken orb_begin_chunk(ByteStream *writer, u32 tag, u16 version, u16 flags);
void orb_end_chunk(ByteStream *writer, Orb_ChunkToken chunk);
void orb_write_chunk(ByteStream *writer, u32 tag, u16 version, u16 flags, ByteSpan data);
b32 orb_read_chunk(ByteStream *reader, Orb_Chunk *chunk);
b32 orb_end_read_chunk(ByteStream *reader, Orb_Chunk chunk);

#endif
