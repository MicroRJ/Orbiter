#ifndef ORB_H
#define ORB_H

#include "base.h"

#define ORB_FOURCC(a, b, c, d) ((u32)(u8)(a) | (u32)(u8)(b) << 8 | (u32)(u8)(c) << 16 | (u32)(u8)(d) << 24)

enum
{
	ORB_SYSTEM_NES = ORB_FOURCC('N', 'E', 'S', ' '),
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
	Orb_SaveKind kind;
	Orb_Id id;
	Hash256 content_hash;
	u64 created_unix_ms;
	u64 first_played_unix_ms;
	u64 last_played_unix_ms;
	// Cumulative active play time represented by this save.
	u64 play_time_ms;
	Str title;
	// Informational only. Content identity must not depend on this path.
	Str source_path;
}
Orb_Metadata;

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
	Orb_Metadata   metadata;
	Orb_Thumbnail thumbnail;
	ByteSpan          state;
}
Orb_Contents;

typedef struct
{
	u32 type;
	u16 version;
	u16 flags;
	// Header and stored payload, excluding alignment padding.
	ByteSpan encoded;
	// Stored payload only.
	ByteSpan data;
}
Orb_ChunkView;

typedef struct
{
	ByteSpan source;
	Orb_Metadata metadata;
	Orb_Thumbnail thumbnail;
	Orb_ChunkView meta_chunk;
	Orb_ChunkView thumbnail_chunk;
	Orb_ChunkView state_chunk;
}
Orb_Descriptor;

typedef struct
{
	Orb_Status status;
	u64 offset;
}
Orb_Result;

// Encoding appends the complete ORB to output_arena.
Orb_Result orb_encode(Arena *output_arena, Orb_Contents contents, ByteSpan *output);
// Parsed strings and spans borrow source and remain valid only while it does.
// A zero encoded span means that the corresponding chunk was absent.
Orb_Result orb_parse(ByteSpan source, Orb_Descriptor *descriptor);
const char *orb_status_string(Orb_Status status);

#endif
