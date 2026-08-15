#ifndef ORBITER_APP_LIBRARY_STORE_H
#define ORBITER_APP_LIBRARY_STORE_H

#include "app_library.h"
#include "app_save.h"

typedef struct App_LibraryGameData App_LibraryGameData;
struct App_LibraryGameData
{
	NES_Game game;
	App_Save save;
};

typedef struct App_LibraryStore App_LibraryStore;
struct App_LibraryStore
{
	Arena arena;
	Str manifest_path;
	Str directory;
	App_Library library;
	u32 game_capacity;
};

App_LibraryStore *app_library_store_open(const char *manifest_path);
void app_library_store_close(App_LibraryStore *store);
b32 app_library_store_read_game(App_LibraryStore *store, Arena *arena, const App_LibraryGame *game, const App_LibrarySave *save, App_LibraryGameData *data);
b32 app_library_store_read_save(App_LibraryStore *store, Arena *arena, const App_LibrarySave *save, App_Save *data);
b32 app_library_store_write_save(App_LibraryStore *store, Arena *scratch, const App_LibrarySave *save, const App_Save *data);
b32 app_library_store_write_manifest(App_LibraryStore *store, Arena *scratch);
b32 app_library_store_import_game(App_LibraryStore *store, Arena *scratch, NES_Game source_game, Str title, const App_Save *save_data,
	App_LibraryGame **game, App_LibrarySave **save);
#endif
