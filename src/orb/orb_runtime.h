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
	u64 first_played_unix_ms;
	u64 last_played_unix_ms;
	u64 play_time_ms;
	Orb_Save *first_save;
	Orb_Save *last_save;
	u32 save_count;
}
Orb;

typedef enum
{
	ORB_STORE_SOURCE_NONE,
	ORB_STORE_SOURCE_ORB,
	ORB_STORE_SOURCE_LEGACY_STATE,
}
Orb_StoreSource;

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
	Orb_StoreSource source_kind;
	b32 loaded;
}
Orb_Store;

void orb_store_init(Orb_Store *store);
void orb_store_destroy(Orb_Store *store);
Orb_StoreResult orb_store_load(Orb_Store *store, Str path);
void orb_store_log_info(const Orb_Store *store);
const char *orb_store_status_string(Orb_StoreStatus status);

#endif
