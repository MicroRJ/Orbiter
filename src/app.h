#ifndef ORBITER_APP_H
#define ORBITER_APP_H

#include "actions.h"
#include "app_library.h"
#include "app_save.h"
#include "audio_mixer.h"
#include "audio_stream.h"
#include "debugger.h"
#include "execution_activity.h"
#include "gif_recorder.h"
#include "nes_target.h"
#include "orb.h"
#include "text.h"
#include "text_gfx.h"

typedef struct App_Window App_Window;

typedef struct App_LibraryStore App_LibraryStore;

enum { APP_THUMBNAIL_CACHE_CAPACITY = 32 };

typedef struct
{
	const App_LibrarySave *save;
	u64 updated_unix_ms;
	u64 last_used_frame;
	GFX_Texture *texture;
}
App_ThumbnailCacheEntry;

typedef struct
{
	App_ThumbnailCacheEntry entries[APP_THUMBNAIL_CACHE_CAPACITY];
}
App_ThumbnailCache;

typedef struct App App;
struct App
{
	App_Transport transport;
	App_LibraryStore *library_store;
	App_LibraryGame *active_game;
	App_LibrarySave *active_save;
	App_SaveData active_save_data;
	App_ThumbnailCache thumbnail_cache;
	u64 frame_index;

	f32 ppu_volume;
	f32 ppu_volume_target;
	f32 ppu_volume_restore;
	Arena arena;
	Arena frame_arena;
	Arena game_arena;
	NES_Emulator emulator;
	Debugger *debugger;
	ExecutionActivity execution_activity;
	NES_TargetPublication published;
	GFX_Texture *video_texture;
	GFX_Texture *chr_texture;
	GFX_Renderer *renderer;
	Text_Context *text;
	Text_GFX *text_gfx;
	App_Window *window;
	Audio_Stream *audio;
	Audio_Mixer *audio_mixer;
	u32 audio_backend_capacity;
	b32 audio_backend_available;
	b32 user_config_save_suppressed;

	f64 play_time_seconds;

	GifRecorder ppu_gif;
	b32 ppu_screenshot_requested;
	Seconds frame_begin;
};

GFX_Texture *app_thumbnail_texture(App *app, const App_LibrarySave *save);

#endif
