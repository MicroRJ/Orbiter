#ifndef ORB_H
#define ORB_H

#include "base.h"

#define ORB_FOURCC(a, b, c, d) ((u32)(u8)(a) | (u32)(u8)(b) << 8 | (u32)(u8)(c) << 16 | (u32)(u8)(d) << 24)

enum
{
	ORB_SYSTEM_NES = ORB_FOURCC('N', 'E', 'S', ' '),

	ORB_FILE_VERSION_CURRENT = 2,

	ORB_CHUNK_GAME_METADATA   = ORB_FOURCC('G', 'A', 'M', 'E'),
	ORB_CHUNK_GAME_CONTENT    = ORB_FOURCC('C', 'O', 'N', 'T'),
	ORB_CHUNK_SAVE            = ORB_FOURCC('S', 'A', 'V', 'E'),
	ORB_CHUNK_SAVE_METADATA   = ORB_FOURCC('S', 'M', 'E', 'T'),
	ORB_CHUNK_STATE           = ORB_FOURCC('S', 'T', 'A', 'T'),
	ORB_CHUNK_THUMBNAIL       = ORB_FOURCC('T', 'H', 'M', 'B'),

	ORB_CHUNK_REQUIRED  = 1 << 0,
	ORB_CHUNK_HAS_CRC32 = 1 << 1,
	ORB_CHUNK_KNOWN_FLAGS = ORB_CHUNK_REQUIRED | ORB_CHUNK_HAS_CRC32,

	ORB_CODEC_NONE = 0,
};

typedef enum
{
	ORB_STATUS_OK,
	ORB_STATUS_INVALID_ARGUMENT,
	ORB_STATUS_INVALID_FORMAT,
	ORB_STATUS_UNSUPPORTED_VERSION,
	ORB_STATUS_UNSUPPORTED_CHUNK,
	ORB_STATUS_MISSING_CHUNK,
	ORB_STATUS_DUPLICATE_CHUNK,
	ORB_STATUS_CHECKSUM_MISMATCH,
	ORB_STATUS_INVALID_SEQUENCE,
	ORB_STATUS_OUTPUT_TOO_LARGE,
}
Orb_Status;

typedef enum
{
	ORB_SAVE_RESUME = 1,
	ORB_SAVE_MANUAL,
}
Orb_SaveKind;

typedef enum
{
	ORB_PIXEL_FORMAT_RGBA8 = 1,
}
Orb_PixelFormat;

typedef struct
{
	u8 bytes[16];
}
Orb_Id;

typedef struct
{
	u32 system;
	Hash256 content_hash;
	u64 first_played_unix_ms;
	u64 last_played_unix_ms;
	u64 play_time_ms;
	Str title;
	// Informational only. Content identity must not depend on this path.
	Str source_path;
}
Orb_GameMetadata;

typedef struct
{
	Orb_Id id;
	Orb_SaveKind kind;
	u64 created_unix_ms;
	u64 updated_unix_ms;
	// Cumulative active play time represented by this save.
	u64 play_time_ms;
}
Orb_SaveMetadata;

typedef struct
{
	u32 width;
	u32 height;
	u32 stride;
	Orb_PixelFormat format;
	ByteSpan pixels;
}
Orb_Thumbnail;

typedef struct
{
	Orb_Status status;
	u64 offset;
}
Orb_Result;

typedef struct
{
	u32 type;
	u16 version;
	u16 flags;
	u16 codec;
	u64 unpacked_size;
	u64 offset;
	u64 payload_offset;
	// Header and stored payload, excluding alignment padding.
	ByteSpan encoded;
	// Stored payload only. Current typed decoders accept only ORB_CODEC_NONE.
	ByteSpan data;
}
Orb_Chunk;

typedef struct
{
	Arena *arena;
	u64 rollback_position;
	u64 start_position;
	u64 expected_position;
	u64 save_header_position;
	Orb_Result result;
	Hash256 content_hash;
	u32 root_chunks;
	u32 save_chunks;
	u32 chunk_count;
	b32 in_save;
	b32 ended;
}
Orb_Encoder;

typedef struct Orb_Decoder Orb_Decoder;
struct Orb_Decoder
{
	ByteSpan source;
	u64 base_offset;
	u64 cursor;
	Orb_Result result;
	u16 version;
	u32 chunk_count;
	b32 container;
	b32 ended;
	b32 child_active;
	Orb_Decoder *root;
	Orb_Decoder *parent;
};

// Encoding appends one complete ORB to the arena. On failure, end_encoding
// restores the arena position captured by begin_encoding. The encoder owns the
// arena tail until end_encoding or cancel_encoding; do not push or pop that
// arena between calls.
Orb_Encoder orb_begin_encoding(Arena *output_arena);
b32 orb_write_game_metadata_chunk(Orb_Encoder *encoder, Orb_GameMetadata metadata);
b32 orb_write_game_content_chunk(Orb_Encoder *encoder, ByteSpan content);
b32 orb_begin_save_chunk(Orb_Encoder *encoder);
b32 orb_write_save_metadata_chunk(Orb_Encoder *encoder, Orb_SaveMetadata metadata);
b32 orb_write_save_state_chunk(Orb_Encoder *encoder, ByteSpan state);
b32 orb_write_save_thumbnail_chunk(Orb_Encoder *encoder, Orb_Thumbnail thumbnail);
b32 orb_end_save_chunk(Orb_Encoder *encoder);
Orb_Result orb_end_encoding(Orb_Encoder *encoder, ByteSpan *output);
void orb_cancel_encoding(Orb_Encoder *encoder);

// Decoded chunks and their payloads borrow the input source.
Orb_Result orb_begin_decoding(Orb_Decoder *decoder, ByteSpan source);
Orb_Result orb_begin_container_decoding(Orb_Decoder *decoder, Orb_Decoder *parent, Orb_Chunk container);
b32 orb_read_chunk(Orb_Decoder *decoder, Orb_Chunk *chunk);
Orb_Result orb_end_decoding(Orb_Decoder *decoder);

Orb_Result orb_decode_game_metadata_chunk(Orb_Chunk chunk, Orb_GameMetadata *metadata);
Orb_Result orb_decode_save_metadata_chunk(Orb_Chunk chunk, Orb_SaveMetadata *metadata);
Orb_Result orb_decode_game_content_chunk(Orb_Chunk chunk, ByteSpan *content);
Orb_Result orb_decode_save_state_chunk(Orb_Chunk chunk, ByteSpan *state);
Orb_Result orb_decode_thumbnail_chunk(Orb_Chunk chunk, Orb_Thumbnail *thumbnail);

const char *orb_status_string(Orb_Status status);

#endif
