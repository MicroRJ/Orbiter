#ifndef ORBITER_APP_LIBRARY_H
#define ORBITER_APP_LIBRARY_H

#include "base.h"

typedef struct elf_State elf_State;

typedef enum
{
	APP_LIBRARY_SAVE_RESUME = 1,
	APP_LIBRARY_SAVE_MANUAL,
}
App_LibrarySaveKind;

typedef enum
{
	APP_LIBRARY_MIRROR_HORIZONTAL = 1,
	APP_LIBRARY_MIRROR_VERTICAL,
	APP_LIBRARY_MIRROR_FOUR_SCREEN,
}
App_LibraryMirroring;

typedef struct
{
	u32 mapper;
	App_LibraryMirroring mirroring;
	Str trainer_path;
	Str prg_path;
	u32 prg_size;
	Str chr_path;
	u32 chr_size;
}
App_LibraryCartridge;

typedef struct
{
	Str id;
	App_LibrarySaveKind kind;
	u64 flags;
	Str label;
	Str path;
	u64 created_unix_ms;
	u64 updated_unix_ms;
	u64 play_time_ms;
}
App_LibrarySave;

typedef struct
{
	Str id;
	Str title;
	Str developer;
	u32 release_year;
	u64 first_played_unix_ms;
	u64 last_played_unix_ms;
	u64 play_time_ms;
	App_LibraryCartridge cartridge;
	App_LibrarySave *saves;
	u32 save_count;
}
App_LibraryGame;

typedef struct
{
	App_LibraryGame *games;
	u32 game_count;
}
App_Library;

// Persisted paths are relative to the manifest file's directory. Resolving
// them belongs to the file-loading layer, not this table codec.
// Pushes the library as one Elf table.
void app_library_push_elf(elf_State *state, const App_Library *library);
// Copies an Elf library table into arena-owned C storage.
b32 app_library_read_elf(elf_State *state, i32 index, Arena *arena, App_Library *library);

#endif
