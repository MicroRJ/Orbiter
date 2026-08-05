#ifndef ORBITER_APP_H
#define ORBITER_APP_H

#include "actions.h"
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

typedef struct
{
	Orb *orb;
}
App_OrbLibEntry;

typedef struct App App;
struct App
{
	App_Transport transport;
	Orb_SaveNode *active_save;

	App_OrbLibEntry *orb_library;
	u32              orb_library_count;

	f32 ppu_volume;
	f32 ppu_volume_target;
	Arena arena;
	Arena frame_arena;
	Arena game_arena;
	NES_Emulator emulator;
	Debugger *debugger;
	ExecutionActivity execution_activity;
	NES_TargetPublication published;
	GFX_Texture *video_texture;
	GFX_Texture *chr_texture;
	GFX_Texture *dummy_texture;
	GFX_Renderer *renderer;
	Text_Context *text;
	Text_GFX *text_gfx;
	App_Window *window;
	Audio_Stream *audio;
	Audio_Mixer *audio_mixer;
	Audio_Clip ui_click;
	u32 audio_backend_capacity;
	b32 audio_backend_available;
	b32 user_config_save_suppressed;

	Str last_rom_path;
	Str current_game_title;
	Orb_Id current_save_id;
	Hash256 current_content_hash;
	u64 save_created_unix_ms;
	u64 first_played_unix_ms;

	f64 play_time_seconds;

	Orb_Store orb_store;

	GifRecorder ppu_gif;
	b32 ppu_screenshot_requested;
	Seconds frame_begin;
};

#endif
