#include "debugger.h"
#include "audio_mixer.h"
#include "audio_stream.h"
#include "graphics.h"
#include "text.h"
#include "panels.h"
#include "ttf_api.h"
#include "text_gfx.h"
#include "ui_box.h"
#include "ui_widgets.h"
#include "views.h"
#include "execution_activity.h"
#include "gif_recorder.h"
#include "nes_target.h"
#include "orb_runtime.h"
#include "nes_serialize.h"
#include "os.h"
#include "actions.h"

global const char orb_save_path[]         = "data/resume.orb";
global const char debugger_config_path[]  = "data/debugger.cfg";
global const char debugger_default_config_path[] = "data/default_debugger.cfg";
global const char debugger_log_path[]     = "data/debugger.log";
global const char debugger_program_path[] = "data/program.dump";
global const char app_font_path[]         = "data/fonts/Saira/static/Saira-Medium.ttf";

typedef struct
{
	AppAction action;
}
AppInput;

typedef enum
{
	APP_MODE_UNARMED = 0,
	APP_MODE_EMULATOR,
	APP_MODE_REWINDING,
}
AppMode;

typedef struct
{
	AppMode                    mode;
	b32            emulator_running;
	b32     resume_emulator_running;
	i32            rewind_direction;
	b32          library_overlay_on;

	f32 ppu_volume;
	f32 ppu_volume_target;
	f32 ppu_animation;
	Arena arena;
	Arena frame_arena;
	Arena game_arena;
	NES_Emulator emulator;
	Debugger *debugger;
	ExecutionActivity execution_activity;
	NES_TargetPublication published;
	GFX_Texture *video_texture;
	GFX_Texture *chr_texture;
	OS_Window *os_window;
	GFX_Renderer *renderer;
	GFX_Window *gfx_window;
	Draw_Context *draw;
	Text_Context *text;
	Text_GFX *text_gfx;
	UI_Context *ui;
	Panels *panels;
	Audio_Stream *audio;
	Audio_Mixer *audio_mixer;
	Audio_Clip ui_click;
	u32 audio_backend_capacity;
	b32 audio_backend_available;

	Str last_rom_path;
	Str current_game_title;
	Orb_Id current_save_id;
	Hash256 current_content_hash;
	u64 save_created_unix_ms;
	u64 first_played_unix_ms;

	f64 play_time_seconds;

	Orb_Store orb_store;
	Orb *active_orb;
	Orb_SaveNode *active_save;

	b32 exclusive_ppu_mode;
	b32 fullscreen_mode;
	b32 crt_enabled;
	GifRecorder ppu_gif;
	GifRecorder app_gif;
	Seconds frame_begin;
	Seconds previous_draw_time;
	f32 frames_per_second;
}
App;

global App app = { };
global FILE *debugger_log_file;

static Str app_read_file(Arena *arena, const char *path)
{
	Str result = {};
	Platform_File file = platform_access_file(path, PLATFORM_FILE_OPEN_EXISTING, PLATFORM_FILE_READ | PLATFORM_FILE_SHARE_READ | PLATFORM_FILE_SHARE_WRITE | PLATFORM_FILE_SHARE_DELETE);
	if (!platform_file_is_valid(file)) return result;
	u64 size = 0;
	if (platform_get_file_size(file, &size) && size <= MAX_VALUE_U32)
	{
		u8 *data = arena_push(arena, size + 1);
		u64 bytes_read = 0;
		if (platform_read_file(file, data, size, &bytes_read) && bytes_read == size)
		{
			data[size] = 0;
			result = str_from_data((char *)data, (u32)size);
		}
	}
	platform_close_file(file);
	return result;
}

static b32 app_write_file(const char *path, const void *data, u32 size)
{
	Platform_File file = platform_access_file(path, PLATFORM_FILE_CREATE_ALWAYS, PLATFORM_FILE_WRITE);
	if (!platform_file_is_valid(file)) return false;
	u64 bytes_written = 0;
	b32 result = platform_write_file(file, data, size, &bytes_written) && bytes_written == size;
	platform_close_file(file);
	return result;
}

// NOTE(RJ) now file writes are "atomic"
static b32 app_write_file_atomic(const char *path, const void *data, u32 size)
{
	char temporary_path[1024];
	i32 length = snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", path);
	if (length <= 0 || (u32) length >= sizeof(temporary_path)) return false;
	if (!app_write_file(temporary_path, data, size)) return false;
	if (platform_move_file(temporary_path, path, true)) return true;
	platform_remove_file(temporary_path);
	return false;
}

static Str app_rom_name(Str path) {return LIT("WTF");}

static u64 app_unix_time_ms(void)
{
	i64 value = platform_unix_time_ms();
	return value > 0 ? (u64)value : 0;
}

static u64 app_play_time_ms(void)
{
	return (u64)(app.play_time_seconds + 0.5);
}

// TODO(RJ) why are we using CRT files for this!?
static void app_file_log_sink(const LogRecord *record, void *user_data)
{
	FILE *file = user_data;
	fprintf(file, "%llu %s(%i) %-7s: %*s%s\n", record->sequence, record->source.file, record->source.line, record->tag, record->indent * 2, "", record->message);
	fflush(file);
}

static void app_restore_state()
{
}
static void app_save_state()
{
}

static const KeyBind app_emulator_mode_key_binds[] =
{
	{APP_LOWER_VOLUME                    , {KEY_CHORD_ON_PRESSED, OS_Key_Down , OS_MODIFIER_CONTROL}},
	{APP_RAISE_VOLUME                    , {KEY_CHORD_ON_PRESSED, OS_Key_Up   , OS_MODIFIER_CONTROL}},
	{APP_ACTION_TOGGLE_LIBRARY_OVERLAY   , {KEY_CHORD_ON_RELEASE, OS_Key_Tab}},

	{APP_ACTION_SPLIT_PANEL_HORIZONTALLY , {KEY_CHORD_ON_RELEASE, OS_Key_H, OS_MODIFIER_CONTROL}},
	{APP_ACTION_SPLIT_PANEL_VERTICALLY   , {KEY_CHORD_ON_RELEASE, OS_Key_V, OS_MODIFIER_CONTROL}},
	{APP_ACTION_CLOSE_PANEL              , {KEY_CHORD_ON_RELEASE, OS_Key_Q, OS_MODIFIER_CONTROL}},

	{APP_ACTION_OPEN_VIEW_0 , {KEY_CHORD_ON_RELEASE, OS_Key_1                      }},
	{APP_ACTION_OPEN_VIEW_1 , {KEY_CHORD_ON_RELEASE, OS_Key_2                      }},
	{APP_ACTION_OPEN_VIEW_2 , {KEY_CHORD_ON_RELEASE, OS_Key_3                      }},
	{APP_ACTION_OPEN_VIEW_3 , {KEY_CHORD_ON_RELEASE, OS_Key_4                      }},
	{APP_ACTION_OPEN_VIEW_4 , {KEY_CHORD_ON_RELEASE, OS_Key_5                      }},
	{APP_ACTION_OPEN_VIEW_5 , {KEY_CHORD_ON_RELEASE, OS_Key_6                      }},
	{APP_ACTION_OPEN_VIEW_6 , {KEY_CHORD_ON_RELEASE, OS_Key_7                      }},
	{APP_ACTION_OPEN_VIEW_7 , {KEY_CHORD_ON_RELEASE, OS_Key_8                      }},
	{APP_ACTION_OPEN_VIEW_8 , {KEY_CHORD_ON_RELEASE, OS_Key_9                      }},
	{APP_ACTION_OPEN_VIEW_9 , {KEY_CHORD_ON_RELEASE, OS_Key_0                      }},
	{APP_ACTION_OPEN_VIEW_10, {KEY_CHORD_ON_RELEASE, OS_Key_1, OS_MODIFIER_CONTROL }},
	{APP_ACTION_OPEN_VIEW_11, {KEY_CHORD_ON_RELEASE, OS_Key_2, OS_MODIFIER_CONTROL }},
	{APP_ACTION_OPEN_VIEW_12, {KEY_CHORD_ON_RELEASE, OS_Key_3, OS_MODIFIER_CONTROL }},
	{APP_ACTION_OPEN_VIEW_13, {KEY_CHORD_ON_RELEASE, OS_Key_4, OS_MODIFIER_CONTROL }},
	{APP_ACTION_OPEN_VIEW_14, {KEY_CHORD_ON_RELEASE, OS_Key_5, OS_MODIFIER_CONTROL }},
	{APP_ACTION_OPEN_VIEW_15, {KEY_CHORD_ON_RELEASE, OS_Key_6, OS_MODIFIER_CONTROL }},

	{APP_ACTION_BEGIN_REWINDING_BACKWARDS , {KEY_CHORD_ON_PRESSED, OS_Key_Left , OS_MODIFIER_CONTROL}},
	{APP_ACTION_BEGIN_REWINDING_FORWARD   , {KEY_CHORD_ON_PRESSED, OS_Key_Right, OS_MODIFIER_CONTROL}},
	{APP_ACTION_OPEN_ROM                  , {KEY_CHORD_ON_RELEASE, OS_Key_O    , OS_MODIFIER_CONTROL}},
	{APP_ACTION_RESET                     , {KEY_CHORD_ON_RELEASE, OS_Key_R    , OS_MODIFIER_CONTROL}},
	{APP_ACTION_SAVE_STATE                , {KEY_CHORD_ON_RELEASE, OS_Key_S    , OS_MODIFIER_CONTROL}},
	{APP_ACTION_RESTORE_STATE             , {KEY_CHORD_ON_RELEASE, OS_Key_L    , OS_MODIFIER_CONTROL}},
	{APP_ACTION_DUMP_PROGRAM              , {KEY_CHORD_ON_RELEASE, OS_Key_K    , OS_MODIFIER_CONTROL}},
	{APP_ACTION_INCREASE_UI_FONT_SIZE      , {KEY_CHORD_ON_RELEASE, OS_Key_Equal       , OS_MODIFIER_CONTROL}},
	{APP_ACTION_INCREASE_UI_FONT_SIZE      , {KEY_CHORD_ON_RELEASE, OS_Key_NumPadPlus  , OS_MODIFIER_CONTROL}},
	{APP_ACTION_DECREASE_UI_FONT_SIZE      , {KEY_CHORD_ON_RELEASE, OS_Key_Minus       , OS_MODIFIER_CONTROL}},
	{APP_ACTION_DECREASE_UI_FONT_SIZE      , {KEY_CHORD_ON_RELEASE, OS_Key_NumPadMinus , OS_MODIFIER_CONTROL}},
	{APP_ACTION_RESET_UI_FONT_SIZE         , {KEY_CHORD_ON_RELEASE, OS_Key_0           , OS_MODIFIER_CONTROL}},
	{APP_ACTION_MUTE                      , {KEY_CHORD_ON_RELEASE, OS_Key_M    , }},
	{APP_ACTION_TOGGLE_PPU_FULLSCREEN     , {KEY_CHORD_ON_RELEASE, OS_Key_F    , }},
	{APP_ACTION_EXIT_PPU_FULLSCREEN       , {KEY_CHORD_ON_RELEASE, OS_Key_Esc  , }},
	{APP_ACTION_TOGGLE_FULLSCREEN         , {KEY_CHORD_ON_RELEASE, OS_Key_F11  , }},
	{APP_ACTION_TOGGLE_FULLSCREEN         , {KEY_CHORD_ON_RELEASE, OS_Key_Enter, OS_MODIFIER_ALT }},
	{APP_ACTION_TOGGLE_RUNNING            , {KEY_CHORD_ON_RELEASE, OS_Key_F5   , }},
	{APP_ACTION_TOGGLE_CRT                , {KEY_CHORD_ON_RELEASE, OS_Key_F7   , }},
	{APP_ACTION_TOGGLE_PPU_CAPTURE        , {KEY_CHORD_ON_RELEASE, OS_Key_F8   , }},
	{APP_ACTION_TOGGLE_APP_CAPTURE        , {KEY_CHORD_ON_RELEASE, OS_Key_F9   , }},
	{APP_ACTION_STEP                      , {KEY_CHORD_ON_RELEASE, OS_Key_F10  , }},
};

static AppAction app_find_action_for_keychord(KeyChord chord, const KeyBind *binds, u32 nbinds) {
	for (u32 i = 0; i < nbinds; ++ i)
	{
		if (binds[i].key_chord.key == chord.key && binds[i].key_chord.modifiers == chord.modifiers && binds[i].key_chord.activation == chord.activation) {
			return binds[i].action;
		}
	}
	return APP_ACTION_NONE;
}

static NES_Input app_translate_keyboard_input_for_emulator(u32 player)
{
	if (player != 0) return 0;
	OS_KeyState *keys = app.os_window->keys;
	NES_Input input = 0;
	if (keys[OS_Key_Up]    & OS_KEY_DOWN) input |= NES_INPUT_UP;
	if (keys[OS_Key_Down]  & OS_KEY_DOWN) input |= NES_INPUT_DOWN;
	if (keys[OS_Key_Left]  & OS_KEY_DOWN) input |= NES_INPUT_LEFT;
	if (keys[OS_Key_Right] & OS_KEY_DOWN) input |= NES_INPUT_RIGHT;
	if (keys[OS_Key_W] & OS_KEY_DOWN) input |= NES_INPUT_UP;
	if (keys[OS_Key_S] & OS_KEY_DOWN) input |= NES_INPUT_DOWN;
	if (keys[OS_Key_A] & OS_KEY_DOWN) input |= NES_INPUT_LEFT;
	if (keys[OS_Key_D] & OS_KEY_DOWN) input |= NES_INPUT_RIGHT;
	if (keys[OS_Key_Z] & OS_KEY_DOWN) input |= NES_INPUT_A;
	if (keys[OS_Key_X] & OS_KEY_DOWN) input |= NES_INPUT_B;
	if (keys[OS_Key_C] & OS_KEY_DOWN) input |= NES_INPUT_START;
	if (keys[OS_Key_V] & OS_KEY_DOWN) input |= NES_INPUT_SELECT;
	return input;
}

static NES_Input app_translate_controller_input_for_emulator(u32 player)
{
	OS_ControllerState controller;
	if (!os_controller_get_state(player, &controller)) return 0;

	NES_Input input = 0;
	if (controller.buttons[2]) input |= NES_INPUT_SELECT;
	if (controller.start)      input |= NES_INPUT_START;
	if (controller.buttons[0] || controller.triggers[1]       >  0.15f) input |= NES_INPUT_A;
	if (controller.buttons[1] || controller.triggers[0]       >  0.15f) input |= NES_INPUT_B;
	if (controller.dpad[0]    || controller.thumb_sticks[0].y >  0.25f) input |= NES_INPUT_UP;
	if (controller.dpad[1]    || controller.thumb_sticks[0].y < -0.25f) input |= NES_INPUT_DOWN;
	if (controller.dpad[2]    || controller.thumb_sticks[0].x < -0.25f) input |= NES_INPUT_LEFT;
	if (controller.dpad[3]    || controller.thumb_sticks[0].x >  0.25f) input |= NES_INPUT_RIGHT;
	return input;
}

static void app_update_emulator_input(void)
{
	for (u32 player = 0; player < 2; player ++)
	{
		NES_Input input = app_translate_keyboard_input_for_emulator(player) | app_translate_controller_input_for_emulator(player);
		nes_emulator_set_input(&app.emulator, player, input);
	}
}

static void app_clear_emulator_input(void)
{
	for (u32 player = 0; player < 2; player++) {
		nes_emulator_set_input(&app.emulator, player, 0);
	}
}

static Audio_Clip app_make_ui_click(Arena *arena, u32 sample_rate)
{
	Assert(arena);
	Assert(sample_rate);
	u32 frame_count = Max((u32)(sample_rate * 0.035f), 2);
	f32 *samples = arena_push(arena, sizeof(*samples) * frame_count);
	f32 duration = frame_count / (f32)sample_rate;
	u32 noise_state = 0xB5297A4Du;
	for (u32 frame = 0; frame < frame_count; frame ++)
	{
		f32 t = frame / (f32)sample_rate;
		f32 u = frame / (f32)(frame_count - 1);
		f32 attack = Min(frame * 0.25f, 1.f);
		f32 release = 1.f - u;
		f32 envelope = attack * release * release * expf(-80.f * t);
		f32 chirp_phase = 6.28318530718f * (1800.f * t - (900.f / (2.f * duration)) * t * t);
		noise_state = noise_state * 1664525u + 1013904223u;
		f32 noise = ((noise_state >> 8) * (1.f / 16777215.f)) * 2.f - 1.f;
		f32 tone = sinf(chirp_phase) * 0.70f + sinf(6.28318530718f * 320.f * t) * 0.20f + noise * 0.10f;
		samples[frame] = tone * envelope;
	}
	return (Audio_Clip) { .samples = samples, .frame_count = frame_count };
}

static void app_play_ui_feedback(void)
{
	UI_Feedback feedback = ui_feedback_take(app.ui);
	if (feedback & UI_FEEDBACK_PRESS) audio_mixer_play(app.audio_mixer, app.ui_click, (Audio_PlayDesc) { .gain = 0.35f });
}

static AppInput app_translate_input_events_based_on_mode(void)
{
	AppInput input = { 0 };

	OS_KeyState *keys = app.os_window->keys;

	if (app.mode == APP_MODE_REWINDING)
	{
		input.action = APP_ACTION_SUPPRESS_EMULATOR_INPUT;

		if (keys[OS_Key_LeftControl] & OS_KEY_DOWN) {
			i32 rewind_direction = 0;
			if (keys[OS_Key_Left] & OS_KEY_DOWN) rewind_direction -= 1;
			if (keys[OS_Key_Right] & OS_KEY_DOWN) rewind_direction += 1;
			app.rewind_direction = rewind_direction;
		}
		else {
			input.action = APP_ACTION_STOP_REWINDING;
		}
		return input;
	}

	if ((keys[OS_Key_LeftControl] | keys[OS_Key_RightControl]) & OS_KEY_DOWN) {
		input.action = APP_ACTION_SUPPRESS_EMULATOR_INPUT;
	}

	for (u32 index = 0; index < os_window_event_count(app.os_window); ++index)
	{
		const OS_Event *event = os_window_event(app.os_window, index);
		input.action = app_find_action_for_keychord((KeyChord){(KeyChordActivation)event->type,event->key,event->modifiers}, app_emulator_mode_key_binds, ArrayCount(app_emulator_mode_key_binds));
	}
	return input;
}

static b32 app_setup_emulator_from_orb(Orb *orb)
{
	Orb_Game game = orb->game;
	Orb_GameMetadata metadata = game.metadata;
	NES_SetupParams setup_params = {};
	setup_params.mapper       = metadata.mapper;
	setup_params.vmirror      = metadata.vmirror;
	setup_params.four_screen  = metadata.four_screen;
	setup_params.has_trainer  = metadata.has_trainer;
	setup_params.prg_rom      = byte_span(game.prg_rom_data, metadata.prg_rom_size);
	setup_params.chr_rom      = byte_span(game.chr_rom_data, metadata.chr_rom_size);
	b32 success = nes_setup_emulator(&app.emulator, setup_params);
	return success;
}

static b32 app_handle_input(AppInput input)
{
	b32 handled = false;
	switch (input.action)
	{
		case APP_ACTION_TOGGLE_LIBRARY_OVERLAY:
		{
			app.library_overlay_on = !app.library_overlay_on;
			// if (app.library_overlay_on) app.catalog_refresh_pending = true;
		}
		break;

		case APP_ACTION_SUPPRESS_EMULATOR_INPUT:
		{
			handled = true;
		} break;

		case APP_ACTION_SPLIT_PANEL_HORIZONTALLY:
		{
			LOG_DEBUG("split panel horizontally");
			Assert(app.panels->focused);
			panel_split(app.panels, app.panels->focused, AXIS_X, 0.5f);
		}
		break;
		case APP_ACTION_SPLIT_PANEL_VERTICALLY:
		{
			LOG_DEBUG("split panel vertically");
			Assert(app.panels->focused);
			panel_split(app.panels, app.panels->focused, AXIS_Y, 0.5f);
		}
		break;
		case APP_ACTION_CLOSE_PANEL:
		{
			Assert(app.panels->focused);
			panel_close(app.panels, app.panels->focused);
		}
		break;
		case APP_ACTION_OPEN_VIEW_0:
		case APP_ACTION_OPEN_VIEW_1:
		case APP_ACTION_OPEN_VIEW_2:
		case APP_ACTION_OPEN_VIEW_3:
		case APP_ACTION_OPEN_VIEW_4:
		case APP_ACTION_OPEN_VIEW_5:
		case APP_ACTION_OPEN_VIEW_6:
		case APP_ACTION_OPEN_VIEW_7:
		case APP_ACTION_OPEN_VIEW_8:
		case APP_ACTION_OPEN_VIEW_9:
		case APP_ACTION_OPEN_VIEW_10:
		case APP_ACTION_OPEN_VIEW_11:
		case APP_ACTION_OPEN_VIEW_12:
		case APP_ACTION_OPEN_VIEW_13:
		case APP_ACTION_OPEN_VIEW_14:
		case APP_ACTION_OPEN_VIEW_15:
		{
			if (input.action - APP_ACTION_OPEN_VIEW_0 < view_desc_count) {
				const ViewDesc *desc = &view_descs[input.action - APP_ACTION_OPEN_VIEW_0];
				panel_open_view(app.panels, app.panels->focused, desc);
			}
		}
		break;
		case APP_ACTION_OPEN_ROM:
		{
			Str path = os_dialog_open_file(&app.frame_arena);
			if (path.size) {
				Orb *orb = orb_from_file(&app.orb_store, path);
				if (orb && app_setup_emulator_from_orb(orb)) {
					Assert(nes_is_booted(& app.emulator));
					app.mode = APP_MODE_EMULATOR;
					app.library_overlay_on = false;
					LOG_INFO("emulator setup successfully");
				}
				else {
					LOG_INFO("could not setup emulator");
				}
			}
			handled = true;
		}
		break;
		case APP_ACTION_RESET:
		{
			// if (app.current_cartridge.prg_rom.size) debugger_load_cartridge(app.debugger, app.current_cartridge);
			// else if (app.last_rom_path.size) app_open_rom_path(app.last_rom_path);
			handled = true;
		}
		break;
		case APP_ACTION_SAVE_STATE:    app_save_state();    break;
		case APP_ACTION_RESTORE_STATE: app_restore_state(); break;
		case APP_ACTION_DUMP_PROGRAM:
		{
			if (program_dump(app.debugger, debugger_program_path, &app.frame_arena)) {
				LOG_INFO("dumped program model to '%s'", debugger_program_path);
			} else {
				LOG_ERROR("failed to dump program model to '%s'", debugger_program_path);
			}
			handled = true;
		} break;
		case APP_ACTION_TOGGLE_RUNNING:
		{
			app.emulator_running = !app.emulator_running;
			LOG_INFO(app.emulator_running ? "running realtime" : "paused");
			handled = true;
		} break;
		case APP_ACTION_TOGGLE_PPU_CAPTURE:
		{
			if (app.ppu_gif.recording) gif_recorder_end(&app.ppu_gif);
			else if (!gif_recorder_begin(&app.ppu_gif, v2i(NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT), "ppu_capture")) LOG_ERROR("failed to begin PPU GIF capture");
			handled = true;
		} break;
		case APP_ACTION_TOGGLE_APP_CAPTURE:
		{
			if (app.app_gif.recording) gif_recorder_end(&app.app_gif);
			else if (!gif_recorder_begin(&app.app_gif, app.os_window->size, "orbiter_capture")) LOG_ERROR("failed to begin application GIF capture");
			handled = true;
		} break;
		case APP_ACTION_TOGGLE_CRT:
		{
			app.crt_enabled = !app.crt_enabled;
			handled = true;
		} break;
		case APP_ACTION_INCREASE_UI_FONT_SIZE:
		case APP_ACTION_DECREASE_UI_FONT_SIZE:
		case APP_ACTION_RESET_UI_FONT_SIZE:
		{
			i32 font_size = app.ui->theme.code.size;
			if (input.action == APP_ACTION_INCREASE_UI_FONT_SIZE) font_size = Min(font_size + 1, UI_CODE_FONT_SIZE_MAX);
			else if (input.action == APP_ACTION_DECREASE_UI_FONT_SIZE) font_size = Max(font_size - 1, UI_CODE_FONT_SIZE_MIN);
			else font_size = UI_CODE_FONT_SIZE_DEFAULT;
			if (font_size != app.ui->theme.code.size)
			{
				app.ui->theme.code.size = font_size;
				LOG_INFO("UI font size %d px", font_size);
			}
			handled = true;
		} break;
		case APP_ACTION_MUTE:
		{
			app.ppu_volume_target = 0.f;
			app.ppu_animation = 1.f;
			handled = true;
		} break;
		case APP_RAISE_VOLUME:
		{
			app.ppu_volume_target += 0.1;
			app.ppu_volume_target = Min(app.ppu_volume_target, 1.f);
			app.ppu_animation = 1.f;
			handled = true;
		} break;
		case APP_LOWER_VOLUME:
		{
			app.ppu_volume_target -= 0.1;
			app.ppu_volume_target = Max(app.ppu_volume_target, 0.f);
			app.ppu_animation = 1.f;
			handled = true;
		} break;
		case APP_ACTION_BEGIN_REWINDING_BACKWARDS:
		{
			Assert(app.mode != APP_MODE_REWINDING);
			app.mode = APP_MODE_REWINDING;
			app.rewind_direction = -1;
			app.resume_emulator_running = app.emulator_running;
			app.emulator_running = false;
			handled = true;
		} break;
		case APP_ACTION_BEGIN_REWINDING_FORWARD:
		{
			Assert(app.mode != APP_MODE_REWINDING);
			app.mode = APP_MODE_REWINDING;
			app.rewind_direction = +1;
			app.resume_emulator_running = app.emulator_running;
			app.emulator_running = false;
			handled = true;
		} break;
		case APP_ACTION_STOP_REWINDING:
		{
			app.mode = APP_MODE_EMULATOR;
			app.emulator_running = app.resume_emulator_running;
			handled = true;
		} break;

		case APP_ACTION_TOGGLE_PPU_FULLSCREEN:
		{
			app.exclusive_ppu_mode = !app.exclusive_ppu_mode;
			app.fullscreen_mode = app.exclusive_ppu_mode;
			os_window_set_fullscreen(app.os_window, app.fullscreen_mode);
			handled = true;
		} break;
		case APP_ACTION_EXIT_PPU_FULLSCREEN:
		{
			if (app.exclusive_ppu_mode)
			{
				app.exclusive_ppu_mode = false;
				app.fullscreen_mode = false;
				os_window_set_fullscreen(app.os_window, false);
			}
			handled = true;
		} break;
		case APP_ACTION_TOGGLE_FULLSCREEN:
		{
			app.fullscreen_mode = !app.fullscreen_mode;
			os_window_set_fullscreen(app.os_window, app.fullscreen_mode);
			handled = true;
		} break;
		default: ;
	}
	return handled;
}

static void app_run_frame(void)
{
	u64 sample_capacity = nes_required_sample_capacity();
	if (!app.audio_backend_available)
	{
		NES_RunFrameResult frame = debugger_run_frame(app.debugger, 0, 0);
		prof_add_metric(PROF_METRIC_AUDIO_SAMPLES_GENERATED, frame.samples);
		if (debugger_breakpoint_hit(app.debugger))
		{
			app.emulator_running = false;
			return;
		}
		app.play_time_seconds += frame.samples / (f64)nes_sample_rate(0);
		return;
	}

	u32 stream_capacity = audio_stream_capacity_frames(app.audio);
	Assert(sample_capacity <= stream_capacity);
	u32 target_limit = stream_capacity - (u32)sample_capacity;
	u32 target = Min(app.audio_backend_capacity * 2, target_limit);
	target = Max(target, 1);
	f32 *samples = arena_push(&app.frame_arena, sizeof(*samples) * sample_capacity);

	while (audio_stream_queued_frames(app.audio) < target)
	{
		NES_RunFrameResult frame = debugger_run_frame(app.debugger, samples, sample_capacity);
		if (debugger_breakpoint_hit(app.debugger))
		{
			app.emulator_running = false;
			return;
		}

		app.play_time_seconds += frame.samples / (f64)nes_sample_rate(0);
		prof_add_metric(PROF_METRIC_AUDIO_SAMPLES_GENERATED, frame.samples);
		PROF_BLOCK("audio stream write") audio_stream_write(app.audio, samples, (u32)frame.samples);
	}
}

static void app_mix_and_output_audio(void)
{
	if (!app.audio_backend_available) return;
	u32 writable = os_audio_writable_frames();
	if (!writable) return;
	if (!audio_stream_queued_frames(app.audio) && !audio_mixer_active_voice_count(app.audio_mixer)) return;
	f32 *output = arena_push(&app.frame_arena, sizeof(*output) * writable);

	while (writable)
	{
		Audio_ReadSpan span = audio_stream_acquire(app.audio);
		u32 count = Min(writable, span.frame_count);
		const f32 *emulator_samples = span.samples;
		if (!count)
		{
			audio_stream_consume(app.audio, 0);
			emulator_samples = 0;
			if (!audio_mixer_active_voice_count(app.audio_mixer)) break;
			count = writable;
		}
		audio_mixer_render(app.audio_mixer, output, emulator_samples, count, app.ppu_volume, 1.f, 1.f);
		u32 written = os_audio_write_mono(output, count);
		if (written > count)
		{
			LOG_ERROR("audio backend reported writing %u frames from a %u-frame span", written, count);
			written = count;
		}
		audio_mixer_advance(app.audio_mixer, written);
		if (span.frame_count) audio_stream_consume(app.audio, written);
		if (!written) break;
		writable -= written;
	}
}

static void app_upload_video_texture(void)
{
	gfx_update_texture(app.video_texture, (GFX_UpdateTextureParams) {
		.dest = v2i(0, 0),
		.size = v2i(NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT),
		.stride = NES_VIDEO_WIDTH * sizeof(*app.published.video),
		.data = app.published.video,
	});
}

static void app_upload_chr_texture(void)
{
	gfx_update_texture(app.chr_texture, (GFX_UpdateTextureParams) {
		.dest = v2i(0, 0),
		.size = v2i(NES_TARGET_CHR_WIDTH, NES_TARGET_CHR_HEIGHT),
		.stride = NES_TARGET_CHR_WIDTH * sizeof(*app.published.chr_image),
		.data = app.published.chr_image,
	});
}
#if 0
static Str app_rom_name(Str path)
{
	u32 separator = str_find_last(path, LIT("/\\"));
	u32 first = separator == MAX_VALUE_U32 ? 0 : separator + 1;
	return str_slice(path, first, path.size - first);
}
#endif

static void app_update_fps(void)
{
	Seconds now = seconds_now();
	if (app.previous_draw_time.seconds > 0.0)
	{
		f64 elapsed = now.seconds - app.previous_draw_time.seconds;
		if (elapsed > 0.0)
		{
			f32 measured = (f32)(1.0 / elapsed);
			app.frames_per_second = app.frames_per_second > 0.f ? app.frames_per_second * 0.9f + measured * 0.1f : measured;
		}
	}
	app.previous_draw_time = now;
}

static UI_Box *app_status_text(UI_Context *ui, u64 key, Str text, UI_TextStyle style, f32 emission)
{
	ui_emission(ui, emission);
	UI_Box *box = ui_text_box_string(ui, key, style, text);
	return box;
}

static UI_Box *app_status_bar_begin(UI_Context *ui, UI_Key key, Str name, f32 height)
{
	ui_clean(ui);
	ui_axis(ui, AXIS_Y);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(height));
	UI_Box *box = ui_box_begin(ui, key, name);
	box->paint = (UI_BoxPaintDesc) {
		.flags = UI_BOX_DRAW_BACKGROUND,
		.background = ui->theme.slider_track,
	};
	return box;
}

static UI_Box *app_status_row_begin(UI_Context *ui, UI_Key key, Str name)
{
	ui_clean(ui);
	ui_axis(ui, AXIS_X);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_padd(ui, AXIS_X, 6.f, 6.f);
	return ui_box_begin(ui, key, name);
}

static UI_Box *app_status_divider(UI_Context *ui, UI_Key key, b32 at_top)
{
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(1.f));
	if (at_top) ui_margin(ui, AXIS_Y, 0.f, -1.f);
	else        ui_margin(ui, AXIS_Y, -1.f, 0.f);
	UI_Box *box = ui_box_make(ui, key, LIT("status divider"));
	box->paint = (UI_BoxPaintDesc) {
		.flags = UI_BOX_DRAW_BACKGROUND,
		.background = ui->theme.panel_outline,
	};
	return box;
}

static void app_draw_box_tree(UI_Box *box)
{
	ui_box_paint(box);
	for (UI_Box *child = box->first; child; child = child->next) {
		app_draw_box_tree(child);
	}
}

#include "library_ui.c"

static UI_Box *app_build_shell(rect_f32 window_rect, ViewFrameData *frame)
{
	UI_Context *ui = app.ui;
	Assert(frame);
	UI_BoxDesc root_desc = ui_defaults();
	root_desc.axis = AXIS_Y;
	root_desc.size[AXIS_X] = ui_grow(1.f);
	root_desc.size[AXIS_Y] = ui_grow(1.f);
	UI_Box *root = ui_build_begin(app.ui, UI_KEY("application shell"), LIT("application shell"), root_desc);

	f32 status_height = app.ui->theme.code.size + 8.f;
	UI_TextStyle style = app.ui->theme.code;
	style.align.y = 0.5f;

	app_status_bar_begin(ui, 1, LIT("top status"), status_height);
	app_status_row_begin(ui, 1, LIT("top status row"));

	style.color = app.ui->theme.palette.cyan;
	ui_clean(app.ui);
	ui_size(app.ui, AXIS_X, ui_wrap());
	ui_size(app.ui, AXIS_Y, ui_wrap());
	app_status_text(app.ui, 1, LIT("ORBITER"), style, app.ui->theme.palette.emission_medium);

	style.color = app.ui->theme.text_subtle;
	ui_clean(app.ui);
	ui_size(app.ui, AXIS_X, ui_flex(0.f, 1.f));
	ui_size(app.ui, AXIS_Y, ui_wrap());
	app_status_text(app.ui, 2, LIT("  |  github.com/MicroRJ  |  "), style, 0.f);

	f32 pulse = 0.5f + 0.5f * sinf((f32)seconds_now().seconds * 3.f * 4);

	if (app.mode == APP_MODE_UNARMED)
	{
		style.color = app.ui->theme.palette.amber;
		ui_clean(app.ui);
		ui_size(app.ui, AXIS_X, ui_flex(0.f, 1.f));
		ui_size(app.ui, AXIS_Y, ui_wrap());
		app_status_text(app.ui, 3, LIT("* INSERT CARTRIDGE *"), style, app.ui->theme.palette.emission_high * pulse);
	}
	else if (app.mode == APP_MODE_REWINDING && app.rewind_direction == -1)
	{
		style.color = app.ui->theme.palette.error;
		ui_clean(app.ui);
		ui_size(app.ui, AXIS_X, ui_flex(0.f, 1.f));
		ui_size(app.ui, AXIS_Y, ui_wrap());
		app_status_text(app.ui, 3, LIT("<< REWINDING"), style, app.ui->theme.palette.emission_high * pulse);
	}
	else if (app.mode == APP_MODE_REWINDING && app.rewind_direction == +1)
	{
		style.color = app.ui->theme.palette.amber;
		ui_clean(app.ui);
		ui_size(app.ui, AXIS_X, ui_flex(0.f, 1.f));
		ui_size(app.ui, AXIS_Y, ui_wrap());
		app_status_text(app.ui, 3, LIT("REPLAYING >>"), style, app.ui->theme.palette.emission_high * pulse);
	}
	else if (app.mode == APP_MODE_REWINDING)
	{
		style.color = app.ui->theme.palette.error;
		ui_clean(app.ui);
		ui_size(app.ui, AXIS_X, ui_flex(0.f, 1.f));
		ui_size(app.ui, AXIS_Y, ui_wrap());
		app_status_text(app.ui, 3, LIT("<< REWINDING >>"), style, app.ui->theme.palette.emission_high);
	}
	else if (app.mode == APP_MODE_EMULATOR)
	{
		if (app.emulator_running)
		{
			style.color = app.ui->theme.palette.amber;
			ui_clean(app.ui);
			ui_size(app.ui, AXIS_X, ui_flex(0.f, 1.f));
			ui_size(app.ui, AXIS_Y, ui_wrap());
			app_status_text(app.ui, 3, LIT("RUNNING"), style, app.ui->theme.palette.emission_medium);
		}
		else
		{
			style.color = app.ui->theme.palette.error;
			ui_clean(app.ui);
			ui_size(app.ui, AXIS_X, ui_flex(0.f, 1.f));
			ui_size(app.ui, AXIS_Y, ui_wrap());
			app_status_text(app.ui, 3, LIT("PAUSED"), style, 0.06f + pulse * 0.16f);
		}
	}

	style.color = app.ui->theme.text_subtle;
	if (app.app_gif.recording)
	{
		ui_clean(app.ui);
		ui_size(app.ui, AXIS_X, ui_flex(0.f, 1.f));
		ui_size(app.ui, AXIS_Y, ui_wrap());
		app_status_text(app.ui, 4, LIT("   REC APP"), style, 0.f);
	}
	if (app.ppu_gif.recording)
	{
		ui_clean(app.ui);
		ui_size(app.ui, AXIS_X, ui_flex(0.f, 1.f));
		ui_size(app.ui, AXIS_Y, ui_wrap());
		app_status_text(app.ui, 4, LIT("   REC PPU"), style, 0.f);
	}

	ui_clean(app.ui);
	ui_size(app.ui, AXIS_X, ui_grow(1.f));
	ui_size(app.ui, AXIS_Y, ui_wrap());
	ui_box_make(app.ui, 6, LIT("top spacer"));

	ui_clean(app.ui);
	ui_size(app.ui, AXIS_X, ui_flex(0.f, 1.f));
	ui_size(app.ui, AXIS_Y, ui_wrap());
	style.align.x = 0.f;

	Color_SRGBA ppu_volume_base_color = app.ui->theme.text_subtle;
	if (app.ppu_volume <= 0.01f) {
		ppu_volume_base_color = app.ui->theme.palette.error;
	}
	style.color = color_srgba_mix(ppu_volume_base_color, app.ui->theme.palette.amber, app.ppu_animation);
	ui_emission(ui, app.ui->theme.palette.emission_high * app.ppu_animation);
	UI_Box *volume_box = ui_text_box_sized(app.ui, UI_KEY("volume"), style, LIT("VOL 100%"), "VOL %i%%", (i32) roundf(app.ppu_volume * 100.f));
	if (ui_signal_from_box(volume_box).hovered && ui_tooltip_begin(ui, UI_KEY("volume_tooltip"), ui->mouse))
	{
		ui_clean(ui);
		ui_axis(ui, AXIS_Y);
		ui_size(ui, AXIS_X, ui_wrap());
		ui_size(ui, AXIS_Y, ui_wrap());
		ui_padd(ui, AXIS_X, 8.f, 8.f);
		ui_padd(ui, AXIS_Y, 8.f, 8.f);
		ui_gap(ui, 4.f);

		ui_box_begin(ui, 0, LIT(""));
		UI_TextStyle style = ui->theme.code;
		style.color = ui->theme.text_neutral;
		ui_clean(ui);
		ui_text_box_string(ui, UI_KEY("1"), style, str_push_copy_f(&ui->frame_arena, "Ctrl+Up Raise Volume"));
		ui_clean(ui);
		ui_text_box_string(ui, UI_KEY("2"), style, str_push_copy_f(&ui->frame_arena, "Ctrl+Down Lower Volume"));
		ui_box_end(ui);

		ui_tooltip_end(ui);
	}
	app.ppu_animation *= 0.95f;
	app.ppu_volume += (app.ppu_volume_target - app.ppu_volume) * 0.35f;

	style.color = app.ui->theme.text_subtle;
	ui_clean(app.ui);
	ui_size(app.ui, AXIS_X, ui_flex(0.f, 1.f));
	ui_size(app.ui, AXIS_Y, ui_wrap());
	ui_text_box_sized(app.ui, UI_KEY("fps"), style, LIT("FPS 999.9"), "FPS %02.2f", app.frames_per_second);

	style.color = app.ui->theme.text_subtle;
	ui_clean(app.ui);
	ui_size(app.ui, AXIS_X, ui_flex(0.f, 1.f));
	ui_size(app.ui, AXIS_Y, ui_wrap());
	ui_text_box_sized(app.ui, UI_KEY("frame"), style, LIT("FRAME 999999999"), " FRAME %llu", app.published.generation);

	ui_box_end(ui);
	app_status_divider(ui, 2, false);
	ui_box_end(ui);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_layout(ui, &UI_FlatLayoutHooks);
	ui_box_begin(ui, 2, LIT("belly"));
	{
		if (app.library_overlay_on)
		{
			ui_push_box_z(ui, UI_Z_OVERLAY);

			ui_clean(ui);
			ui_rect(ui, window_rect);
			ui_backdrop(ui, 0.f);
			ui_background(ui, (Color_SRGBA){0,0,0,0.2});
			ui_box_begin(ui, UI_KEY("overlay"), LIT(""));

			library_build_ui(ui, window_rect);

			ui_box_end(ui);

			ui_pop_box_z(ui);
		}
		if (app.mode == APP_MODE_EMULATOR || app.mode == APP_MODE_REWINDING)
		{
			Assert(nes_is_booted(&app.emulator));

			rect_f32 panel_rect = window_rect;
			panel_rect.y += status_height;
			panel_rect.h = Max(0.f, panel_rect.h - status_height * 2.f);
			panels_build_ui(app.panels, app.os_window, frame, panel_rect);
		}

	}
	ui_box_end(ui);

	app_status_bar_begin(ui, 3, LIT("bottom status"), status_height);
	app_status_divider(ui, 1, true);
	app_status_row_begin(ui, 2, LIT("bottom status row"));

	Str rom_name = app_rom_name(app.last_rom_path);
	Str bottom_left = rom_name.size ? str_push_copy_f(&app.ui->frame_arena, "ROM   %.*s", rom_name.size, rom_name.text) : LIT("NO CARTRIDGE");
	ui_clean(app.ui);
	ui_size(app.ui, AXIS_X, ui_flex(0.f, 1.f));
	ui_size(app.ui, AXIS_Y, ui_grow(1.f));
	style.align.x = 0.f;
	app_status_text(app.ui, 1, bottom_left, style, 0.f);
	ui_clean(app.ui);
	ui_size(app.ui, AXIS_X, ui_grow(1.f));
	ui_size(app.ui, AXIS_Y, ui_grow(1.f));
	ui_box_make(app.ui, 2, LIT("bottom spacer"));
	ui_clean(app.ui);
	ui_size(app.ui, AXIS_X, ui_flex(0.f, 3.f));
	ui_size(app.ui, AXIS_Y, ui_grow(1.f));
	style.align.x = 1.f;
	app_status_text(app.ui, 3, LIT("F PPU   F5 RUN   F7 CRT   F8 PPU GIF   F9 APP GIF   F10 STEP   F11 FULLSCREEN"), style, 0.f);
	ui_box_end(ui);
	ui_box_end(ui);

	ui_build_end(app.ui);
	PROF_BLOCK("ui measure") ui_box_measure(root, (UI_BoxConstraints) { .min = window_rect.size, .max = window_rect.size });
	PROF_BLOCK("ui layout") ui_box_layout(root, window_rect);
	PROF_BLOCK("ui hittest") {
		UI_Box *hit = ui_box_find_deepest(root, ui->mouse);
		if (hit) {
			ui->hot = hit->id;
		}
	}
	return root;
}

static void app_draw_shell(UI_Box *root)
{
	app_draw_box_tree(root);
}

static u8 app_linear_to_srgb_u8(f32 value)
{
	value = CLAMP(value, 0.f, 1.f);
	f32 srgb = value <= 0.0031308f ? value * 12.92f : 1.055f * powf(value, 1.f / 2.4f) - 0.055f;
	return (u8)(srgb * 255.f + 0.5f);
}

static void app_capture_gifs(GFX_Texture *frame_texture)
{
	enum { GIF_CAPTURE_MAX_FRAMES = 60 * 30 };

	PROF_BLOCK("ppu gif recording") if (app.ppu_gif.recording)
	{
		if (!gif_recorder_frame(&app.ppu_gif, app.published.video, NES_VIDEO_WIDTH * sizeof(*app.published.video))) {
			LOG_ERROR("PPU GIF capture failed");
		}
		if (app.ppu_gif.frame_count >= GIF_CAPTURE_MAX_FRAMES) {
			gif_recorder_end(&app.ppu_gif);
		}
	}
	PROF_BLOCK("app gif recording") if (app.app_gif.recording)
	{
		vec2i size = gfx_texture_size(frame_texture);
		if (size.x != app.app_gif.size.x || size.y != app.app_gif.size.y)
		{
			LOG_WARN("ending application GIF because the window size changed");
			gif_recorder_end(&app.app_gif);
			return;
		}
		u32 pixel_count = size.x * size.y;
		vec4 *linear = arena_push(&app.frame_arena, pixel_count * sizeof(*linear));
		Color_RGBA8 *pixels = arena_push(&app.frame_arena, pixel_count * sizeof(*pixels));
		if (!gfx_read_texture(frame_texture, linear, size.x * sizeof(*linear)))
		{
			LOG_ERROR("application GIF texture readback failed");
			gif_recorder_end(&app.app_gif);
			return;
		}
		for (u32 index = 0; index < pixel_count; ++index) {
			pixels[index] = (Color_RGBA8) {
				app_linear_to_srgb_u8(linear[index].x),
				app_linear_to_srgb_u8(linear[index].y),
				app_linear_to_srgb_u8(linear[index].z),
				(u8)(CLAMP(linear[index].w, 0.f, 1.f) * 255.f + 0.5f),
			};
		}
		if (!gif_recorder_frame(&app.app_gif, pixels, size.x * sizeof(*pixels))) {
			LOG_ERROR("application GIF capture failed");
		}
		if (app.app_gif.frame_count >= GIF_CAPTURE_MAX_FRAMES) {
			gif_recorder_end(&app.app_gif);
		}
	}
}

static void app_resize_graphics_outputs(vec2i size)
{
	Assert(size.x > 1 && size.y > 1);
	gfx_resize_window(app.gfx_window, size);
}

// Render passes

// TODO(RJ) we need to free intermediate textures!
static GFX_Texture *app_acquire_pass_output(vec2i size, GFX_Sampler sampler, const char *label)
{
	return gfx_acquire_transient_texture(app.renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_RARE_UPDATES,
		.bind_flags = GFX_TEXTURE_BIND_INPUT | GFX_TEXTURE_BIND_OUTPUT,
		.format = GRAPHICS_FORMAT_RGBA_F32,
		.size = size,
		.sampler = sampler,
		.label = label,
	});
}

static GFX_Texture *app_acquire_hdr_pass_output(GFX_Texture *input, const char *label)
{
	return app_acquire_pass_output(gfx_texture_size(input), GRAPHICS_SAMPLER_POINT, label);
}

static GFX_Texture *app_crt_barrel_pass(GFX_Texture *input)
{
	GFX_Texture *output = app_acquire_hdr_pass_output(input, "barrel pass output");
	gfx_begin_pass(app.draw, (GFX_PassDesc) { .output = output, .clear = true, .clear_color = COLOR_BLACK });
	draw_barrel(app.draw, (Draw_BarrelParams) { .texture = input, .strength = 1.f });
	gfx_end_pass(app.draw);
	return output;
}

static GFX_Texture *app_rewind_pass(GFX_Texture *input)
{
	GFX_Texture *output = app_acquire_hdr_pass_output(input, "rewind pass output");
	gfx_begin_pass(app.draw, (GFX_PassDesc) { .output = output, .clear = true, .clear_color = COLOR_BLACK });
	draw_rewind(app.draw, (Draw_RewindParams) { .texture = input, .time = (f32)fmod(seconds_now().seconds, 1024.0), .strength = 1.f });
	gfx_end_pass(app.draw);
	return output;
}

static GFX_Texture *app_crt_scanlines_pass(GFX_Texture *input)
{
	GFX_Texture *output = app_acquire_hdr_pass_output(input, "scanlines pass output");
	gfx_begin_pass(app.draw, (GFX_PassDesc) { .output = output, .clear = true, .clear_color = COLOR_BLACK });
	draw_crt_scanlines(app.draw, input);
	gfx_end_pass(app.draw);
	return output;
}

static void app_draw_exclusive_ppu(GFX_Texture *frame_texture, rect_f32 window_rect)
{
	vec2 presentation_size = v2(4.f, 3.f);
	f32 scale = Min(window_rect.w / presentation_size.x, window_rect.h / presentation_size.y);
	rect_f32 video_rect = rect_f32_align(window_rect, v2(presentation_size.x * scale, presentation_size.y * scale), v2(0.5f, 0.5f));
	video_rect = rect_f32_round_out(video_rect);

	GFX_Texture *video_texture = app.crt_enabled ? app_crt_scanlines_pass(app.video_texture) : app.video_texture;
	gfx_begin_pass(app.draw, (GFX_PassDesc) {
		.output = frame_texture,
		.clear = true,
		.clear_color = COLOR_BLACK,
	});
	draw_image(app.draw, (Draw_TextureParams) {
		.rect = video_rect,
		.texture = video_texture,
		.region = { 0, 0, NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT },
		.tint = COLOR_WHITE,
		.sampler = GRAPHICS_SAMPLER_POINT,
	});
	gfx_end_pass(app.draw);
}

static void app_draw_debugger(GFX_Texture *frame_texture, rect_f32 window_rect)
{
	ViewFrameData frame = {
		.emulator = &app.emulator,
		.debugger = app.debugger,
		.execution_graph = debugger_execution_graph(app.debugger),
		.execution_activity = &app.execution_activity,
		.publication = &app.published,
		.video_texture = app.video_texture,
		.chr_texture = app.chr_texture,
		.ui = app.ui,
		.scratch = &app.ui->frame_arena,
	};
	UI_Box *shell = app_build_shell(window_rect, &frame);

	gfx_begin_pass(app.draw, (GFX_PassDesc) {
		.output = frame_texture,
		.clear = true,
		.clear_color = COLOR_BLACK,
	});
	draw_rect(app.draw, (Draw_RectParams) {
		.rect = window_rect,
		.color = app.ui->theme.background,
	});
	app_draw_shell(shell);
	gfx_end_pass(app.draw);

	draw_compose(app.draw, app.text_gfx, frame_texture, window_rect);
}

static void app_draw(void)
{
	rect_f32 window_rect = rect_f32_from_size(v2_from_v2i(app.os_window->size));
	app_update_fps();
	app_resize_graphics_outputs(app.os_window->size);
	gfx_begin_frame(app.draw);
	ui_begin_frame(app.ui);
	os_window_set_cursor(app.os_window, OS_CURSOR_POINTER);

	GFX_Texture *frame_texture = app_acquire_pass_output(app.os_window->size, GRAPHICS_SAMPLER_POINT, "application frame");

	if (app.exclusive_ppu_mode) {
		app_draw_exclusive_ppu(frame_texture, window_rect);
	}
	else {
		app_draw_debugger(frame_texture, window_rect);
	}


	GFX_Texture *present_texture = frame_texture;
	if (app.mode == APP_MODE_REWINDING) present_texture = app_rewind_pass(present_texture);
	if (app.crt_enabled) present_texture = app_crt_barrel_pass(present_texture);

	gfx_begin_pass(app.draw, (GFX_PassDesc) {
		.output = gfx_window_texture(app.gfx_window),
	});
	draw_blit(app.draw, present_texture);
	gfx_end_pass(app.draw);

	text_gfx_sync(app.text_gfx);

	gfx_end_frame(app.draw);

	app_capture_gifs(present_texture);

	PROF_BLOCK("present wait") gfx_present_window(app.gfx_window);

	ui_end_frame(app.ui);
}

static void app_tick(void)
{
	AppInput input = app_translate_input_events_based_on_mode();
	b32 app_consumed_input = app_handle_input(input);

	const Program *program = debugger_program(app.debugger);
	u32 crawler_budget = program->refinement_pass_count < 2 ? 2048 : 128;

	if (nes_is_booted(&app.emulator))
	{
		if (app.mode == APP_MODE_EMULATOR)
		{
			app_clear_emulator_input();
			if (!app_consumed_input) app_update_emulator_input();

			if (input.action == APP_ACTION_STEP) {
				app.emulator_running = false;
				PROF_BLOCK("emulation step") debugger_step(app.debugger);
			}
			else if (app.emulator_running) {
				PROF_BLOCK("emulation") app_run_frame();
			}
		}
		else if (app.mode == APP_MODE_REWINDING && app.rewind_direction == -1)
		{
			// TODO(RJ) this needs to be configurable, and also, we should just be able
			// to skip backwards arbitrarily instead of doing one step at a time
			for(u32 i=0;i<4;++i) {
				if(!debugger_undo_snapshot(app.debugger)) break;
			}
		}
		else if (app.mode == APP_MODE_REWINDING && app.rewind_direction == +1)
		{
			for(u32 i=0;i<4;++i) {
				if(!debugger_redo_snapshot(app.debugger)) break;
			}
		}

		PROF_BLOCK("update cpu mapping")   debugger_update_cpu_mapping(app.debugger);
		// PROF_BLOCK("program refinement")   debugger_run_program_crawler(app.debugger, crawler_budget);
		PROF_BLOCK("execution activity")   execution_activity_update(&app.execution_activity, debugger_execution_graph(app.debugger), seconds_now().seconds);
		PROF_BLOCK("publish NES target")   nes_target_publish(&app.published, &app.emulator);
		PROF_BLOCK("upload video texture") app_upload_video_texture();
		PROF_BLOCK("upload CHR texture")   app_upload_chr_texture();

		if (app.mode != APP_MODE_EMULATOR || !app.emulator_running) {
			audio_stream_discard(app.audio);
		}
	}
	PROF_BLOCK("UI audio feedback") app_play_ui_feedback();

	PROF_BLOCK("mix and output audio") app_mix_and_output_audio();

}

static void app_frame(void)
{
	arena_reset(&app.frame_arena);
	prof_begin_frame();
	PROF_BLOCK("app tick") app_tick();
	PROF_BLOCK("app draw") app_draw();
	prof_close_frame();
}

static b32 app_init(void)
{
	if (!os_init()) {
		LOG_ERROR("failed to initialize os");
		return false;
	}

	if (!os_graphical_init()) {
		LOG_ERROR("failed to initialize graphics");
		return false;
	}

	app.arena = arena_create(0, "app arena");
	app.frame_arena = arena_create(0, "app frame arena");
	app.game_arena = arena_create(0, "app game arena");

	orb_store_init(&app.orb_store);

	OS_AudioInfo audio_info;
	app.audio_backend_available = os_audio_init(&audio_info);
	if (app.audio_backend_available && audio_info.sample_rate != nes_sample_rate(0))
	{
		LOG_WARN("audio output rate %u Hz is unsupported; expected %llu Hz", audio_info.sample_rate, nes_sample_rate(0));
		os_audio_shutdown();
		app.audio_backend_available = false;
	}
	if (!app.audio_backend_available)
	{
		audio_info = (OS_AudioInfo) {
			.buffer_frame_count = 800,
			.sample_rate = 48000,
			.channel_count = 1,
		};
		LOG_WARN("audio output is unavailable; emulation will continue without sound");
	}
	app.audio_backend_capacity = audio_info.buffer_frame_count;
	u32 audio_capacity = Max(audio_info.buffer_frame_count * 4, Max(audio_info.sample_rate / 10, 1));
	app.audio = audio_stream_create(&app.arena, (Audio_StreamDesc) {
		.frame_capacity = audio_capacity,
	});
	app.audio_mixer = audio_mixer_create(&app.arena, (Audio_MixerDesc) {
		.voice_capacity = 32,
	});
	app.ui_click = app_make_ui_click(&app.arena, audio_info.sample_rate);

	ttf_init_api();
	Font_Handle code_font = ttf_load(app_read_file(&app.arena, app_font_path));
	UI_Theme theme = ui_default_theme(code_font);

	app.os_window = os_window_create((OS_WindowDesc) {
		.title = "Orbiter v0.1.0",
		.title_bar = {
			.enabled = true,
			.dark = true,
			.background_rgb = 0x050A0C,
			.text_rgb = 0x718783,
			.border_rgb = 0x718783,
		},
	});
	Assert(app.os_window);
	app.renderer = gfx_create_renderer(&app.arena);
	app.gfx_window = gfx_create_window(&app.arena, app.renderer, app.os_window);
	app.draw = draw_create(&app.arena, app.renderer);
	app.text = text_create(&app.arena);
	app.text_gfx = text_gfx_create(&app.arena, app.renderer, app.text);
	app.ui = ui_create(&app.arena, app.os_window, app.text, app.draw, theme);
	app.panels = panels_create(&app.arena);
	app.crt_enabled = true;

	app.video_texture = gfx_create_texture(app.renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_PER_FRAME,
		.bind_flags = GFX_TEXTURE_BIND_INPUT,
		.format = GRAPHICS_FORMAT_RGBA_U8_SRGB,
		.size = v2i(NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT),
		.sampler = GRAPHICS_SAMPLER_POINT,
		.label = "NES video",
	});
	app.chr_texture = gfx_create_texture(app.renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_PER_FRAME,
		.bind_flags = GFX_TEXTURE_BIND_INPUT,
		.format = GRAPHICS_FORMAT_RGBA_U8_SRGB,
		.size = v2i(NES_TARGET_CHR_WIDTH, NES_TARGET_CHR_HEIGHT),
		.sampler = GRAPHICS_SAMPLER_POINT,
		.label = "NES CHR map",
	});

	app.debugger = debugger_create(&app.arena, &app.emulator);
	app.library_overlay_on = true;
	return true;
}

static void app_shutdown(void)
{
	app_save_state();
	// app_write_active_orb();
	// app_save_catalog();
	// catalog_destroy(&app.catalog);
	orb_store_destroy(&app.orb_store);
	os_window_destroy(app.os_window);
	os_audio_shutdown();
	os_graphical_shutdown();
	os_shutdown();
	if (debugger_log_file)
	{
		logger_remove_sink(app_file_log_sink, debugger_log_file);
		fclose(debugger_log_file);
		debugger_log_file = 0;
	}
}

static void app_pace_frame(void)
{
	Seconds now = seconds_now();
	f64 elapsed = now.seconds - app.frame_begin.seconds;
	platform_sleep((u64)(Max(0.0, 1.0 / 60.0 - elapsed) * 1000.0));
	app.frame_begin = seconds_now();
}

int main(void)
{
	Platform_File_Info font_info;
	if (!platform_get_file_info(app_font_path, &font_info)) {
		os_set_current_directory_to_executable();
	}

	debugger_log_file = fopen(debugger_log_path, "w");
	if (debugger_log_file) {
		Assert(logger_add_sink(app_file_log_sink, debugger_log_file));
	} else {
		LOG_WARN("failed to open session log '%s'", debugger_log_path);
	}

	if (!app_init()) {
		LOG_ERROR("failed to initialize orbiter");
		return 1;
	}

	app.frame_begin = seconds_now();
	while (os_window_is_open(app.os_window))
	{
		os_poll_windows();
		if (os_window_is_open(app.os_window)) {
			app_frame();
		}

		PROF_BLOCK("pacing") app_pace_frame();
	}
	app_shutdown();
	return 0;
}


#if 0
static b32 app_save_state(void)
{
	if (!nes_is_booted(&app.emulator))
	{
		LOG_WARN("cannot save emulator state without a loaded cartridge");
		return false;
	}
	if (!app.active_orb || !app.active_save)
	{
		LOG_WARN("cannot save emulator state without an active ORB save");
		return false;
	}

	ByteSpan state = {};
	SCRATCH_SCOPE(&app.frame_arena)
	{
		ByteSpan captured = orb_nes_state_encode(&app.frame_arena, &app.emulator);
		if (captured.size == app.active_save->state.size)
		{
			memory_copy(app.active_save->state.data, captured.data, captured.size);
			state = app.active_save->state;
		}
		else if (captured.size) state = byte_span(arena_push_copy(&app.orb_store.arena, captured.size, captured.data), captured.size);
	}
	if (!state.size)
	{
		LOG_WARN("failed to capture emulator state");
		return false;
	}

	app.active_save->state = state;
	app.active_save->updated_unix_ms = app_unix_time_ms();
	app.active_save->play_time_ms = app_play_time_ms();
	app.active_orb->last_played_unix_ms = app.active_save->updated_unix_ms;
	app.active_orb->play_time_ms = app.active_save->play_time_ms;
	app.active_orb->dirty = true;
	LOG_INFO("updated active ORB save");
	return true;
}

static NES_SetupParams app_nes_setup_params(NES_CartridgeDesc cartridge)
{
	return (NES_SetupParams) {
		.mapper = cartridge.mapper,
		.vmirror = cartridge.vmirror,
		.four_screen = cartridge.four_screen,
		.has_trainer = cartridge.has_trainer,
		.prg_rom = cartridge.prg_rom,
		.chr_rom = cartridge.chr_rom,
	};
}

static b32 app_load_file(const char *path)
{
	b32 success = false;
	Orb_StoreResult store_result = orb_store_load(&app.orb_store, str_from_cstr(path));
	if (store_result.status != ORB_STORE_STATUS_OK)
	{
		if (store_result.status == ORB_STORE_STATUS_INVALID_ORB) LOG_WARN("failed to parse '%s' at byte %llu: %s", path, store_result.orb_result.offset, orb_status_string(store_result.orb_result.status));
		else LOG_WARN("failed to open '%s': %s", path, orb_store_status_string(store_result.status));
		return false;
	}

	Orb *orb = &app.orb_store.orb;
	Orb_SaveNode *latest = 0;
	for (Orb_SaveNode *save = orb->first_save; save; save = save->next)
	{
		if (!latest || save->updated_unix_ms > latest->updated_unix_ms) latest = save;
	}
	success = nes_setup_emulator(&app.emulator, app_nes_setup_params(orb->cartridge));
	if (success && latest) success = orb_transfer_save_state_no_chunk(&app.emulator, latest->state);
	if (success) debugger_reset(app.debugger);
	if (success && !latest)
	{
		u64 now = app_unix_time_ms();
		u64 arena_position = app.orb_store.arena.position;
		latest = arena_push_zero(&app.orb_store.arena, sizeof(*latest));
		*latest = (Orb_SaveNode) {
			.id = app_new_save_id(now),
			.kind = ORB_SAVE_RESUME,
			.created_unix_ms = now,
			.updated_unix_ms = now,
			.play_time_ms = orb->play_time_ms,
			.state = orb_nes_state_encode(&app.orb_store.arena, &app.emulator),
		};
		if (!latest->state.size)
		{
			app.orb_store.arena.position = arena_position;
			latest = 0;
			success = false;
		}
		else
		{
			orb->first_save = latest;
			orb->last_save = latest;
			orb->save_count = 1;
			orb->dirty = true;
		}
	}
	if (success)
	{
		app.current_save_id = latest->id;
		app.save_created_unix_ms = latest->created_unix_ms;
		app.first_played_unix_ms = orb->first_played_unix_ms;
		app.play_time_seconds = latest->play_time_ms / 1000.0;
		app.last_rom_path = orb->source_path.size ? str_push_copy(&app.arena, orb->source_path) : (Str) {};
		Str title = orb->title.size ? orb->title : app.last_rom_path.size ? app_rom_name(app.last_rom_path) : LIT("NES game");
		app.current_game_title = str_push_copy(&app.arena, title);
		// app_set_game_cartridge(orb->cartridge);
		app.active_orb = orb;
		app.active_save = latest;
	}

	if (success)
	{
		app.mode = APP_MODE_EMULATOR;
		app.library_overlay_on = false;
		LOG_INFO("restored emulator state from '%s'", path);
	}
	else LOG_WARN("failed to restore emulator state from '%s'", path);

	app.active_orb  = 0;
	app.active_save = 0;
	return success;
}

static b32 app_restore_state(void)
{
	// Platform_File_Info info = {};
	// if (platform_get_file_info(orb_save_path, &info)) return app_load_file(orb_save_path);
	// return false;
	return true;
}

static b32 app_open_rom_path(Str path)
{
	b32 success = false;
	SCRATCH_SCOPE(&app.frame_arena)
	{
		Str rom = app_read_file(&app.frame_arena, path.text);
		if (rom.size)
		{
			LOG_INFO("open file: %s", path.text);
			NES_CartridgeDesc cartridge = {};
			if (!nes_cartridge_parse_ines(byte_span((void *)rom.data, rom.size), &cartridge)) LOG_WARN("failed to load ROM: invalid or unsupported iNES image");
			else success = debugger_load_cartridge(app.debugger, cartridge);
			if (success) {
				Hash256 content_hash = orb_cartridge_hash(cartridge);
				b32 same_game = !hash256_is_zero(app.current_content_hash) && hash256_match(app.current_content_hash, content_hash);
				app.mode = APP_MODE_EMULATOR;
				app.library_overlay_on = false;

				app.last_rom_path = str_push_copy(&app.arena, path);
				if (!same_game) app_begin_game_history(app_rom_name(app.last_rom_path));
				app_set_game_cartridge(cartridge);
				app_create_runtime_orb(app.last_rom_path);
				if (!catalog_find_path(&app.catalog, path) && catalog_add_source(&app.catalog, path))
				{
					app_save_catalog();
					app.catalog_refresh_pending = true;
				}
			}
		} else {
			LOG_WARN("failed to read ROM '%s'", path.text);
		}
	}
	return success;
}
static void app_open_rom(void)
{
	SCRATCH_SCOPE(&app.frame_arena)
	{
		Str path = os_dialog_open_file(&app.frame_arena);
		if (path.size) {
			app_open_rom_path(path);
		}
	}
}
#endif
#if 0
static void app_load_catalog(void)
{
	SCRATCH_SCOPE(&app.frame_arena)
	{
		Platform_File_Info info = {};
		if (platform_get_file_info(catalog_config_path, &info))
		{
			Str source = app_read_file(&app.frame_arena, catalog_config_path);
			Catalog_Result result = source.size
				? catalog_from_source(&app.catalog, str_from_data(catalog_config_path, sizeof(catalog_config_path) - 1), source, &app.frame_arena)
				: (Catalog_Result) { .status = CATALOG_STATUS_PARSE_ERROR, .message = LIT("catalog state is empty or unreadable") };
			if (result.status != CATALOG_STATUS_OK)
			{
				app.catalog_write_protected = true;
				app_log_catalog_error(result);
				LOG_WARN("automatic catalog writes are disabled to preserve '%s'", catalog_config_path);
			}
		}
	}
}

static b32 app_save_catalog(void)
{
	if (!app.catalog.dirty) return true;
	if (app.catalog_write_protected) return false;
	b32 success = false;
	SCRATCH_SCOPE(&app.frame_arena)
	{
		Catalog_EncodeResult encoded = catalog_to_source(&app.catalog, &app.frame_arena);
		if (encoded.result.status != CATALOG_STATUS_OK) {
			app_log_catalog_error(encoded.result);
		}
		else
		{
			success = app_write_file_atomic(catalog_config_path, encoded.source.data, encoded.source.size);
			if (success) catalog_mark_saved(&app.catalog);
			else LOG_WARN("failed to save catalog state to '%s'", catalog_config_path);
		}
	}
	return success;
}

static void app_refresh_catalog(void)
{
	Str application_sources[] = { LIT("data") };
	catalog_refresh(&app.catalog, &app.frame_arena, application_sources, ArrayCount(application_sources));
	app.catalog_refresh_pending = false;
	if (app.catalog.scan_error_count) LOG_WARN("catalog refresh completed with %u unreadable entries or sources", app.catalog.scan_error_count);
}

static void app_begin_game_history(Str title)
{
	u64 now = app_unix_time_ms();
	app.current_game_title = str_push_copy(&app.arena, title);
	app.current_save_id = app_new_save_id(now);
	app.current_content_hash = (Hash256) {};
	app.save_created_unix_ms = now;
	app.first_played_unix_ms = now;
	app.play_time_seconds = 0.0;
}

static void app_set_game_cartridge(NES_CartridgeDesc cartridge)
{
	arena_reset(&app.game_arena);
	app.current_cartridge = (NES_CartridgeDesc) {};
	app.current_content_hash = (Hash256) {};
	Hash256 content_hash = orb_cartridge_hash(cartridge);
	if (hash256_is_zero(content_hash)) return;
	ByteSpan prg_rom = byte_span(arena_push_copy(&app.game_arena, cartridge.prg_rom.size, cartridge.prg_rom.data), cartridge.prg_rom.size);
	ByteSpan chr_rom = {};
	if (cartridge.chr_rom.size) chr_rom = byte_span(arena_push_copy(&app.game_arena, cartridge.chr_rom.size, cartridge.chr_rom.data), cartridge.chr_rom.size);
	app.current_cartridge = cartridge;
	app.current_cartridge.prg_rom = prg_rom;
	app.current_cartridge.chr_rom = chr_rom;
	app.current_content_hash = content_hash;
}
#endif
