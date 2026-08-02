#ifndef FRONTEND_CATALOG_H
#define FRONTEND_CATALOG_H

#include "base.h"
#include "nes/cartridge.h"
#include "orb.h"

enum
{
	CATALOG_MAX_SOURCES = 32,
};

typedef enum
{
	CATALOG_ENTRY_ROM,
	CATALOG_ENTRY_ORB,
}
Catalog_EntryKind;

typedef enum
{
	CATALOG_ENTRY_AVAILABLE,
	CATALOG_ENTRY_UNSUPPORTED,
	CATALOG_ENTRY_INVALID,
}
Catalog_EntryStatus;

typedef struct
{
	NES_CartridgeInfo cartridge;
}
Catalog_RomInfo;

typedef struct
{
	Orb_Metadata metadata;
	u64 state_size;
	u32 thumbnail_width;
	u32 thumbnail_height;
	u32 thumbnail_stride;
	Orb_PixelFormat thumbnail_format;
	b32 has_thumbnail;
}
Catalog_OrbInfo;

typedef struct
{
	u64 id;
	Catalog_EntryKind kind;
	Catalog_EntryStatus status;
	u32 system;
	Str path;
	Str title;
	u64 file_size;
	i64 modified_unix_ms;
	union
	{
		Catalog_RomInfo rom;
		Catalog_OrbInfo orb;
	};
}
Catalog_Entry;

typedef struct
{
	Arena *arena;
	Arena entry_arena;
	Str sources[CATALOG_MAX_SOURCES];
	u32 source_count;
	Catalog_Entry *entries;
	u32 entry_count;
	u32 scan_error_count;
	u64 generation;
	b32 dirty;
}
Catalog;

typedef enum
{
	CATALOG_STATUS_OK,
	CATALOG_STATUS_PARSE_ERROR,
	CATALOG_STATUS_EVALUATION_ERROR,
	CATALOG_STATUS_INVALID_SCHEMA,
	CATALOG_STATUS_UNSUPPORTED_VERSION,
	CATALOG_STATUS_TOO_MANY_ENTRIES,
	CATALOG_STATUS_ENCODING_ERROR,
}
Catalog_Status;

typedef struct
{
	Catalog_Status status;
	u32 line;
	u32 column;
	Str message;
}
Catalog_Result;

typedef struct
{
	Catalog_Result result;
	Str source;
}
Catalog_EncodeResult;

void catalog_init(Catalog *catalog, Arena *arena);
void catalog_destroy(Catalog *catalog);
b32 catalog_add_source(Catalog *catalog, Str path);
b32 catalog_remove_source(Catalog *catalog, Str path);
// Refresh rebuilds the entry snapshot and invalidates every previously returned
// Catalog_Entry pointer. generation advances after the new snapshot is ready.
void catalog_refresh(Catalog *catalog, Arena *scratch, const Str *additional_sources, u32 additional_source_count);
const Catalog_Entry *catalog_find_path(const Catalog *catalog, Str path);
Catalog_Result catalog_from_source(Catalog *catalog, Str source_name, Str source, Arena *error_arena);
Catalog_EncodeResult catalog_to_source(const Catalog *catalog, Arena *output_arena);
void catalog_mark_saved(Catalog *catalog);

#endif
