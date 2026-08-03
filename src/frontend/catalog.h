// #ifndef FRONTEND_CATALOG_H
// #define FRONTEND_CATALOG_H

// //
// // The user has a set of games and game_saves.
// //
// // A game is the executable itself along with meta-data.
// // For instance, when it was last played, how long you've played it for, whether you're so addicted to it that you neglect
// // the people around you ... A thumbnail or preview video too ...
// //
// // This is what Steam does, the only difference is that we're adding saves on top.
// //
// // A save points to the game but it also has meta-data. When was the save was created, when did it
// // end, what was the playtime, some captures or highlights from that save ...
// //
// // Each save represents overlapping forks or branches in time.
// //
// // A game can come from an Orb, or a ROM. Either way, the games are cataloged uniformly, added to
// // the games list, where you can see and manage its saves.
// //
// // When displaying a game in the library, we can use save meta-data as well for enhancing the display.
// //
// // So an orb file is the native file format for the library.
// //
// // It contains the game, plus all the saves.
// //
// // When we click on a game, that doesn't have any saves, we create a new save point for it.
// //
// // The model am thinking of is something like this:
// //
// // typedef struct
// // {
// // 	b32     in_orb;
// // 	Str     name;
// // 	Str     path;
// // 	Hash256 hash;
// // 	u64     creation_time_ms;
// // 	u64     last_opened_time_ms;
// // 	u64     total_play_time_ms;
// // 	// ...
// // 	Image   preview_thumbnail;
// // 	// ...
// // 	// This would be empty if it comes a ROM.
// // 	GameSave   **saves;
// // 	u32              num_saves;
// // }
// // Game;
// //
// // typedef struct
// // {
// // 	Game *game;
// // 	// optional name, later feature, given by the user
// // 	Str           name;
// // 	Image         highlight_thumbnail;
// // 	u64           creation_time_ms;
// // 	u64           last_opened_time_ms;
// // 	u64           total_play_time_ms;
// // 	// Additional data
// // }
// // GameSave;
// //
// // The question is whether we the orb name-space refers to the file format only, or whether we extend to mean the overall library
// // ecosystem.
// //
// // We could instead have Orb_Catalog, Orb_Game, Orb_GameSave.
// //
// // Imported roms are added to the runtime orb model, and automatically stored as orb files when the program
// // shuts-down.
// //
// // So orb would be a complete module that does importing & runtime-model management & cataloging & exporting.
// //
// //
// //


// #include "base.h"
// #include "nes/cartridge.h"
// #include "orb.h"

// enum
// {
// 	CATALOG_MAX_SOURCES = 32,
// };

// typedef enum
// {
// 	CATALOG_ENTRY_ROM,
// 	CATALOG_ENTRY_ORB,
// }
// Catalog_EntryKind;

// typedef enum
// {
// 	CATALOG_ENTRY_AVAILABLE,
// 	CATALOG_ENTRY_UNSUPPORTED,
// 	CATALOG_ENTRY_INVALID,
// }
// Catalog_EntryStatus;

// typedef struct
// {
// 	NES_CartridgeInfo cartridge;
// }
// Catalog_RomInfo;

// typedef struct
// {
// 	Orb_GameMetadata metadata;
// 	Orb_Thumbnail thumbnail;
// 	b32 has_thumbnail;
// 	u64 state_size;
// }
// Catalog_OrbInfo;

// typedef struct
// {
// 	u64 id;
// 	Catalog_EntryKind kind;
// 	Catalog_EntryStatus status;
// 	u32 system;
// 	Str path;
// 	Str title;
// 	u64 file_size;
// 	i64 modified_unix_ms;
// 	union
// 	{
// 		Catalog_RomInfo rom;
// 		Catalog_OrbInfo orb;
// 	};
// }
// Catalog_Entry;

// // Runtime game metadata extracted from one successfully inspected source.
// // Content identity will eventually allow multiple sources to collapse into one
// // game; until then, id is the source entry's path-based identifier.
// typedef struct
// {
// 	u64 id;
// 	u32 system;
// 	Catalog_EntryStatus status;
// 	Str title;
// 	Hash256 content_hash;
// 	u64 first_played_unix_ms;
// 	u64 last_played_unix_ms;
// 	u64 play_time_ms;
// 	Orb_Thumbnail thumbnail;
// 	b32 has_thumbnail;
// 	const Catalog_Entry *source;
// }
// Catalog_Game;

// typedef struct
// {
// 	Arena *arena;
// 	Arena entry_arena;
// 	Str sources[CATALOG_MAX_SOURCES];
// 	u32 source_count;
// 	Catalog_Entry *entries;
// 	u32 entry_count;
// 	Catalog_Game *games;
// 	u32 game_count;
// 	u32 scan_error_count;
// 	u64 generation;
// 	b32 dirty;
// }
// Catalog;

// typedef enum
// {
// 	CATALOG_STATUS_OK,
// 	CATALOG_STATUS_PARSE_ERROR,
// 	CATALOG_STATUS_EVALUATION_ERROR,
// 	CATALOG_STATUS_INVALID_SCHEMA,
// 	CATALOG_STATUS_UNSUPPORTED_VERSION,
// 	CATALOG_STATUS_TOO_MANY_ENTRIES,
// 	CATALOG_STATUS_ENCODING_ERROR,
// }
// Catalog_Status;

// typedef struct
// {
// 	Catalog_Status status;
// 	u32 line;
// 	u32 column;
// 	Str message;
// }
// Catalog_Result;

// typedef struct
// {
// 	Catalog_Result result;
// 	Str source;
// }
// Catalog_EncodeResult;

// void catalog_init(Catalog *catalog, Arena *arena);
// void catalog_destroy(Catalog *catalog);
// b32 catalog_add_source(Catalog *catalog, Str path);
// b32 catalog_remove_source(Catalog *catalog, Str path);
// // Refresh rebuilds the entry and game snapshots and invalidates every previously
// // returned Catalog_Entry and Catalog_Game pointer. generation advances after
// // both new snapshots are ready.
// void catalog_refresh(Catalog *catalog, Arena *scratch, const Str *additional_sources, u32 additional_source_count);
// const Catalog_Entry *catalog_find_path(const Catalog *catalog, Str path);
// Catalog_Result catalog_from_source(Catalog *catalog, Str source_name, Str source, Arena *error_arena);
// Catalog_EncodeResult catalog_to_source(const Catalog *catalog, Arena *output_arena);
// void catalog_mark_saved(Catalog *catalog);

// #endif
