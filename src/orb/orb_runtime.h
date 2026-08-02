#ifndef ORB_RUNTIME_H
#define ORB_RUNTIME_H

#include "orb.h"

typedef struct Orb_Save Orb_Save;
struct Orb_Save
{
	Orb_Save *next;
	Orb_Id id;
	Orb_SaveKind kind;
	u64 created_unix_ms;
	u64 updated_unix_ms;
	u64 play_time_ms;
	Orb_Thumbnail thumbnail;
	ByteSpan state;
};

typedef struct
{
	u32 system;
	Hash256 content_hash;
	Str title;
	Str source_path;
	ByteSpan content;
	u64 first_played_unix_ms;
	u64 last_played_unix_ms;
	u64 play_time_ms;
	b32 dirty;
	// Decoding remains forward-compatible, but the current runtime cannot
	// round-trip unknown optional chunks yet. Encoding refuses such an Orb
	// instead of silently deleting extension data.
	b32 has_unpreserved_chunks;
	Orb_Save *first_save;
	Orb_Save *last_save;
	u32 save_count;
}
Orb;

typedef enum
{
	ORB_STORE_STATUS_OK,
	ORB_STORE_STATUS_INVALID_ARGUMENT,
	ORB_STORE_STATUS_NOT_FOUND,
	ORB_STORE_STATUS_FILE_TOO_LARGE,
	ORB_STORE_STATUS_READ_FAILED,
	ORB_STORE_STATUS_INVALID_ORB,
}
Orb_StoreStatus;

typedef struct
{
	Orb_StoreStatus status;
	Orb_Result orb_result;
}
Orb_StoreResult;

typedef struct
{
	Arena arena;
	Str path;
	ByteSpan source;
	Orb orb;
	b32 loaded;
}
Orb_Store;

// Runtime objects borrow strings, content, state, and thumbnails from source.
// Save nodes are allocated from runtime_arena.
Orb_Result orb_runtime_decode(Arena *runtime_arena, ByteSpan source, Orb *orb);
Orb_Result orb_runtime_encode(Arena *output_arena, const Orb *orb, ByteSpan *output);

void orb_store_init(Orb_Store *store);
void orb_store_destroy(Orb_Store *store);
Orb_StoreResult orb_store_load(Orb_Store *store, Str path);
void orb_store_log_info(const Orb_Store *store);
const char *orb_store_status_string(Orb_StoreStatus status);

#endif
