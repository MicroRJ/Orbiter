#include "debugger.h"
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
#include "os.h"

global const char debugger_state_path[]   = "data/save.orbiter";
global const char debugger_config_path[]  = "data/debugger.cfg";
global const char debugger_log_path[]     = "data/debugger.log";
global const char debugger_program_path[] = "data/program.dump";

#define NES_PALETTE_COLORS(_) \
_(0x80,0x80,0x80) _(0x00,0x00,0xBB) _(0x37,0x00,0xBF) _(0x84,0x00,0xA6) \
_(0xBB,0x00,0x6A) _(0xB7,0x00,0x1E) _(0xB3,0x00,0x00) _(0x91,0x26,0x00) \
_(0x7B,0x2B,0x00) _(0x00,0x3E,0x00) _(0x00,0x48,0x0D) _(0x00,0x3C,0x22) \
_(0x00,0x2F,0x66) _(0x00,0x00,0x00) _(0x05,0x05,0x05) _(0x05,0x05,0x05) \
_(0xC8,0xC8,0xC8) _(0x00,0x59,0xFF) _(0x44,0x3C,0xFF) _(0xB7,0x33,0xCC) \
_(0xFF,0x33,0xAA) _(0xFF,0x37,0x5E) _(0xFF,0x37,0x1A) _(0xD5,0x4B,0x00) \
_(0xC4,0x62,0x00) _(0x3C,0x7B,0x00) _(0x1E,0x84,0x15) _(0x00,0x95,0x66) \
_(0x00,0x84,0xC4) _(0x11,0x11,0x11) _(0x09,0x09,0x09) _(0x09,0x09,0x09) \
_(0xFF,0xFF,0xFF) _(0x00,0x95,0xFF) _(0x6F,0x84,0xFF) _(0xD5,0x6F,0xFF) \
_(0xFF,0x77,0xCC) _(0xFF,0x6F,0x99) _(0xFF,0x7B,0x59) _(0xFF,0x91,0x5F) \
_(0xFF,0xA2,0x33) _(0xA6,0xBF,0x00) _(0x51,0xD9,0x6A) _(0x4D,0xD5,0xAE) \
_(0x00,0xD9,0xFF) _(0x66,0x66,0x66) _(0x0D,0x0D,0x0D) _(0x0D,0x0D,0x0D) \
_(0xFF,0xFF,0xFF) _(0x84,0xBF,0xFF) _(0xBB,0xBB,0xFF) _(0xD0,0xBB,0xFF) \
_(0xFF,0xBF,0xEA) _(0xFF,0xBF,0xCC) _(0xFF,0xC4,0xB7) _(0xFF,0xCC,0xAE) \
_(0xFF,0xD9,0xA2) _(0xCC,0xE1,0x99) _(0xAE,0xEE,0xB7) _(0xAA,0xF7,0xEE) \
_(0xB3,0xEE,0xFF) _(0xDD,0xDD,0xDD) _(0x11,0x11,0x11) _(0x11,0x11,0x11)

#define NES_PALETTE_COLOR(r, g, b) { r, g, b, 255 },
global const Color_RGBA8 nes_palette[64] = { NES_PALETTE_COLORS(NES_PALETTE_COLOR) };
#undef NES_PALETTE_COLOR
#undef NES_PALETTE_COLORS

typedef enum
{
	APP_ACTION_NONE,

	APP_ACTION_BEGIN_REWINDING_BACKWARDS,
	APP_ACTION_BEGIN_REWINDING_FORWARD,
	APP_ACTION_STOP_REWINDING,

	APP_ACTION_SUPPRESS_EMULATOR_INPUT,
	APP_ACTION_TOGGLE_FULLSCREEN,
	APP_ACTION_TOGGLE_PPU_FULLSCREEN,
	APP_ACTION_EXIT_PPU_FULLSCREEN,
	APP_ACTION_OPEN_ROM,
	APP_ACTION_RESET,
	APP_ACTION_SAVE_STATE,
	APP_ACTION_RESTORE_STATE,
	APP_ACTION_DUMP_PROGRAM,
	APP_ACTION_TOGGLE_RUNNING,
	APP_ACTION_STEP,
	APP_ACTION_TOGGLE_PPU_CAPTURE,
	APP_ACTION_TOGGLE_APP_CAPTURE,
	APP_ACTION_MUTE,
}
AppAction;

typedef struct
{
	AppAction action;
}
AppInput;

typedef enum
{
	APP_MODE_EMULATOR = 0,
	APP_MODE_REWINDING,
}
AppMode;

typedef struct
{
	AppMode                    mode;
	b32            emulator_running;
	b32     resume_emulator_running;
	i32            rewind_direction;

	Arena arena;
	Arena frame_arena;
	Debugger *debugger;
	ExecutionActivity execution_activity;
	FrontendPublication published;
	GFX_Texture *video_texture;
	GFX_Texture *crt_scanline_texture;
	GFX_Texture *crt_bloom_ping_texture;
	GFX_Texture *crt_bloom_pong_texture;
	GFX_Texture *chr_texture;
	OS_Window *os_window;
	GFX_Renderer *renderer;
	GFX_Window *gfx_window;
	Draw_Context *draw;
	Text_Context *text;
	Text_GFX *text_gfx;
	GFX_Texture *frame_texture;
	GFX_Texture *effect_texture;
	GFX_Texture *bloom_texture;
	GFX_Texture *blur_horizontal_texture;
	GFX_Texture *blur_vertical_texture;
	UI_Context *ui;
	Panels *panels;
	Audio_Stream *audio;
	u32 audio_backend_capacity;
	u32 audio_min_queued_frames;
	u64 audio_starved_frames;
	u64 audio_backend_empty_events;
	u64 audio_last_overrun_frames;
	Seconds audio_stats_begin;
	b32 audio_backend_was_empty;
	String last_rom_path;

	b32 exclusive_ppu_mode;
	b32 fullscreen_mode;
	b32 crt_enabled;
	b32 apu_muted;
	GifRecorder ppu_gif;
	GifRecorder app_gif;
	Seconds frame_begin;
	Seconds previous_draw_time;
	f32 frames_per_second;
}
App;

global App app = { APP_MODE_EMULATOR };
global FILE *debugger_log_file;

static String app_read_file(Arena *arena, const char *path)
{
	String result = {};
	Platform_File file = platform_access_file(path, PLATFORM_FILE_OPEN_EXISTING, PLATFORM_FILE_READ | PLATFORM_FILE_SHARE_READ);
	if (!platform_file_is_valid(file)) return result;
	u64 size = 0;
	if (platform_get_file_size(file, &size) && size <= MAX_VALUE_U32)
	{
		u8 *data = arena_push(arena, size + 1);
		u64 bytes_read = 0;
		if (platform_read_file(file, data, size, &bytes_read) && bytes_read == size)
		{
			data[size] = 0;
			result = string_from_data((char *)data, (u32)size);
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

static void app_file_log_sink(const LogRecord *record, void *user_data)
{
	FILE *file = user_data;
	fprintf(file, "%llu %s(%i) %-7s: %*s%s\n", record->sequence, record->source.file, record->source.line, record->tag, record->indent * 2, "", record->message);
	fflush(file);
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

static void app_update_debugger_input(void)
{
	for (u32 player = 0; player < 2; player ++)
	{
		NES_Input input = app_translate_keyboard_input_for_emulator(player) | app_translate_controller_input_for_emulator(player);
		debugger_set_input(app.debugger, input, player);
	}
}

static void app_clear_debugger_input(void)
{
	for (u32 player = 0; player < 2; player++) {
		debugger_set_input(app.debugger, 0, player);
	}
}

static b32 app_save_state(void)
{
	b32 success = false;
	ARENA_SCOPE(&app.frame_arena)
	{
		u8 *data = arena_top(&app.frame_arena);
		u32 offset = (u32)arena_used(&app.frame_arena);
		if (debugger_save_state(app.debugger, &app.frame_arena))
		{
			u32 size = (u32)arena_used(&app.frame_arena) - offset;
			success = size && app_write_file(debugger_state_path, data, size);
		}
	}
	if (success) LOG_INFO("saved emulator state to '%s'", debugger_state_path);
	else LOG_WARN("failed to save emulator state to '%s'", debugger_state_path);
	return success;
}

static b32 app_restore_state(void)
{
	b32 success = false;
	ARENA_SCOPE(&app.frame_arena)
	{
		String state = app_read_file(&app.frame_arena, debugger_state_path);
		if (state.size)
		{
			success = debugger_restore_state(app.debugger,
			byte_span((void *)state.data, state.size));
		}
	}
	if (success) audio_stream_discard(app.audio);
	if (success) LOG_INFO("restored emulator state from '%s'", debugger_state_path);
	else LOG_WARN("failed to restore emulator state from '%s'", debugger_state_path);
	return success;
}

static b32 app_open_rom_path(String path)
{
	b32 success = false;
	ARENA_SCOPE(&app.frame_arena)
	{
		String rom = app_read_file(&app.frame_arena, path.text);
		if (rom.size)
		{
			LOG_INFO("open file: %s", path.text);
			success = debugger_open_rom(app.debugger, byte_span((void *)rom.data, rom.size));
			if (success) {
				audio_stream_discard(app.audio);
				app.last_rom_path = push_string_copy(&app.arena, path);
			}
		} else {
			LOG_WARN("failed to read ROM '%s'", path.text);
		}
	}
	return success;
}

static void app_open_rom(void)
{
	ARENA_SCOPE(&app.frame_arena)
	{
		String path = os_dialog_open_file(&app.frame_arena);
		if (path.size) {
			app_open_rom_path(path);
		}
	}
}

static String app_config_read_line(String *text)
{
	u32 size = 0;
	while (size < text->size && text->text[size] != '\n') {
		++size;
	}
	u32 line_size = size;
	if (line_size && text->text[line_size - 1] == '\r') {
		--line_size;
	}
	String line = string_slice(*text, 0, line_size);
	u32 consumed = size + (size < text->size);
	*text = string_slice(*text, consumed, text->size - consumed);
	return line;
}

static void app_load_config(void)
{
	ARENA_SCOPE(&app.frame_arena)
	{
		String config = app_read_file(&app.frame_arena, debugger_config_path);
		if (config.size)
		{
			String version = app_config_read_line(&config);
			String rom = app_config_read_line(&config);
			String layout = app_config_read_line(&config);
			if (string_match(version, LIT("version 1")) && string_match(layout, LIT("layout")))
			{
				if (rom.size > 4 && memory_match(rom.text, "rom ", 4))
				{
					String path = string_slice(rom, 4, rom.size - 4);
					String terminated_path = push_string_copy(&app.frame_arena, path);
					app_open_rom_path(terminated_path);
				}
				if (!panels_restore_layout(app.panels, config)) {
					LOG_WARN("ignored invalid panel layout in '%s'", debugger_config_path);
				}
			} else {
				LOG_WARN("ignored unsupported debugger config '%s'", debugger_config_path);
			}
		}
	}
}

static void app_save_config(void)
{
	ARENA_SCOPE(&app.frame_arena)
	{
		String layout = panels_save_layout(app.panels, &app.frame_arena);
		String header = push_formatted(&app.frame_arena, "version 1\nrom %.*s\nlayout\n", app.last_rom_path.size, app.last_rom_path.text ? app.last_rom_path.text : "");
		u32 size = header.size + layout.size;
		char *text = arena_push_aligned(&app.frame_arena, size, 1);
		memory_copy(text, header.text, header.size);
		memory_copy(text + header.size, layout.text, layout.size);
		if (!app_write_file(debugger_config_path, text, size)) {
			LOG_WARN("failed to save debugger config to '%s'", debugger_config_path);
		}
	}
}

typedef enum
{
	KEY_CHORD_ON_RELEASE = OS_EVENT_KEY_RELEASE,
	KEY_CHORD_ON_PRESSED = OS_EVENT_KEY_PRESS,
}
KeyChordActivation;

typedef struct {
	KeyChordActivation activation;
	OS_Key             key;
	OS_ModifierFlags   modifiers;
}
KeyChord;


typedef struct {
	AppAction action;
	KeyChord  key_chord;
}
KeyBind;

static const KeyBind app_emulator_mode_key_binds[] =
{
	{APP_ACTION_BEGIN_REWINDING_FORWARD   , {KEY_CHORD_ON_PRESSED, OS_Key_Left , OS_MODIFIER_CONTROL}},
	{APP_ACTION_BEGIN_REWINDING_BACKWARDS , {KEY_CHORD_ON_PRESSED, OS_Key_Right, OS_MODIFIER_CONTROL}},
	{APP_ACTION_OPEN_ROM                  , {KEY_CHORD_ON_RELEASE, OS_Key_O    , OS_MODIFIER_CONTROL}},
	{APP_ACTION_RESET                     , {KEY_CHORD_ON_RELEASE, OS_Key_R    , OS_MODIFIER_CONTROL}},
	{APP_ACTION_SAVE_STATE                , {KEY_CHORD_ON_RELEASE, OS_Key_S    , OS_MODIFIER_CONTROL}},
	{APP_ACTION_RESTORE_STATE             , {KEY_CHORD_ON_RELEASE, OS_Key_L    , OS_MODIFIER_CONTROL}},
	{APP_ACTION_DUMP_PROGRAM              , {KEY_CHORD_ON_RELEASE, OS_Key_K    , OS_MODIFIER_CONTROL}},
	{APP_ACTION_MUTE                      , {KEY_CHORD_ON_RELEASE, OS_Key_M    , }},
	{APP_ACTION_TOGGLE_PPU_FULLSCREEN     , {KEY_CHORD_ON_RELEASE, OS_Key_F    , }},
	{APP_ACTION_EXIT_PPU_FULLSCREEN       , {KEY_CHORD_ON_RELEASE, OS_Key_Esc  , }},
	{APP_ACTION_TOGGLE_FULLSCREEN         , {KEY_CHORD_ON_RELEASE, OS_Key_F11  , }},
	{APP_ACTION_TOGGLE_FULLSCREEN         , {KEY_CHORD_ON_RELEASE, OS_Key_Enter, OS_MODIFIER_ALT }},
	{APP_ACTION_TOGGLE_RUNNING            , {KEY_CHORD_ON_RELEASE, OS_Key_F5   , }},
	{APP_ACTION_TOGGLE_PPU_CAPTURE        , {KEY_CHORD_ON_RELEASE, OS_Key_F8   , }},
	{APP_ACTION_TOGGLE_APP_CAPTURE        , {KEY_CHORD_ON_RELEASE, OS_Key_F9   , }},
	{APP_ACTION_STEP                      , {KEY_CHORD_ON_RELEASE, OS_Key_F10  , }},
};

static AppAction app_find_action_for_keychord(KeyChord chord, KeyBind *binds, u32 nbinds) {
	for (u32 i = 0; i < nbinds; ++ i)
	{
		if (binds[i].key_chord.key == chord.key && binds[i].key_chord.modifiers == chord.modifiers && binds[i].key_chord.activation == chord.activation) {
			return binds[i].action;
		}
	}
	return APP_ACTION_NONE;
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
		input.action = app_find_action_for_keychord((KeyChord){event->type,event->key,event->modifiers}, app_emulator_mode_key_binds, ArrayCount(app_emulator_mode_key_binds));
	}
	return input;
}

static u64 app_handle_input(AppInput input)
{
	u64 step_cycles = 0;
	b32 clear_input = input.action != APP_ACTION_NONE;
	switch (input.action)
	{
		case APP_ACTION_NONE: app_update_debugger_input(); break;
		case APP_ACTION_SUPPRESS_EMULATOR_INPUT:
		{
			clear_input = app.emulator_running;
		} break;
		case APP_ACTION_OPEN_ROM: app_open_rom(); break;
		case APP_ACTION_RESET:
		{
			if (debugger_reset(app.debugger)) audio_stream_discard(app.audio);
		} break;
		case APP_ACTION_SAVE_STATE: app_save_state(); break;
		case APP_ACTION_RESTORE_STATE: app_restore_state(); break;
		case APP_ACTION_DUMP_PROGRAM:
		{
			if (program_dump(app.debugger, debugger_program_path, &app.frame_arena)) {
				LOG_INFO("dumped program model to '%s'", debugger_program_path);
			} else {
				LOG_ERROR("failed to dump program model to '%s'", debugger_program_path);
			}
		} break;
		case APP_ACTION_TOGGLE_RUNNING:
		{
			app.emulator_running = !app.emulator_running;
			LOG_INFO(app.emulator_running ? "running realtime" : "paused");
		} break;
		case APP_ACTION_STEP:
		{
			app.emulator_running = false;
			step_cycles = 3;
		} break;
		case APP_ACTION_TOGGLE_PPU_CAPTURE:
		{
			if (app.ppu_gif.recording) gif_recorder_end(&app.ppu_gif);
			else if (!gif_recorder_begin(&app.ppu_gif, v2i(NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT), "ppu_capture")) LOG_ERROR("failed to begin PPU GIF capture");
		} break;
		case APP_ACTION_TOGGLE_APP_CAPTURE:
		{
			if (app.app_gif.recording) gif_recorder_end(&app.app_gif);
			else if (!gif_recorder_begin(&app.app_gif, app.os_window->size, "orbiter_capture")) LOG_ERROR("failed to begin application GIF capture");
		} break;

		case APP_ACTION_BEGIN_REWINDING_BACKWARDS:
		{
			Assert(app.mode != APP_MODE_REWINDING);
			app.mode = APP_MODE_REWINDING;
			app.rewind_direction = -1;
			app.resume_emulator_running = app.emulator_running;
			app.emulator_running = false;
			clear_input = false;
		} break;
		case APP_ACTION_BEGIN_REWINDING_FORWARD:
		{
			Assert(app.mode != APP_MODE_REWINDING);
			app.mode = APP_MODE_REWINDING;
			app.rewind_direction = +1;
			app.resume_emulator_running = app.emulator_running;
			app.emulator_running = false;
			clear_input = false;
		} break;
		case APP_ACTION_STOP_REWINDING:
		{
			app.mode = APP_MODE_EMULATOR;
			app.emulator_running = app.resume_emulator_running;
			clear_input = app.emulator_running;
		} break;

		case APP_ACTION_MUTE:
		{
			app.apu_muted = !app.apu_muted;
		}
		break;
		case APP_ACTION_TOGGLE_PPU_FULLSCREEN:
		{
			app.exclusive_ppu_mode = !app.exclusive_ppu_mode;
			app.fullscreen_mode = app.exclusive_ppu_mode;
			os_window_set_fullscreen(app.os_window, app.fullscreen_mode);
		}
		break;
		case APP_ACTION_EXIT_PPU_FULLSCREEN:
		{
			if (app.exclusive_ppu_mode)
			{
				app.exclusive_ppu_mode = false;
				app.fullscreen_mode = false;
				os_window_set_fullscreen(app.os_window, false);
			}
		} break;
		case APP_ACTION_TOGGLE_FULLSCREEN:
		{
			app.fullscreen_mode = !app.fullscreen_mode;
			os_window_set_fullscreen(app.os_window, app.fullscreen_mode);
		} break;
	}
	if (clear_input) app_clear_debugger_input();
	return step_cycles;
}

static void app_fill_audio(void)
{
	// Todo, we need to run at least one freaking frame!
	u32 queued = audio_stream_available_frames(app.audio);
	u32 target = Min(app.audio_backend_capacity * 2, audio_stream_capacity_frames(app.audio));
	if (queued >= target) return;

	u32 minimum = target - queued;
	u32 capacity = audio_stream_capacity_frames(app.audio);

	f32 *samples = arena_push(&app.frame_arena, sizeof(*samples) * capacity);
	u64 nsamples = debugger_run_samples(app.debugger, minimum, samples, capacity);

	// Todo ... dude?
	if (app.apu_muted) memory_zero(samples, sizeof(* samples) * nsamples);

	if (debugger_breakpoint_hit(app.debugger))
	{
		app.emulator_running = false;
		audio_stream_discard(app.audio);
		return;
	}

	prof_add_metric(PROF_METRIC_AUDIO_SAMPLES_GENERATED, nsamples);

	PROF_BLOCK("audio stream write") audio_stream_write(app.audio, samples, (u32)nsamples);
}

static void app_drain_audio(void)
{
	u32 writable = os_audio_writable_frames();
	u32 queued = audio_stream_available_frames(app.audio);
	app.audio_min_queued_frames = Min(app.audio_min_queued_frames, queued);
	if (app.emulator_running && writable == app.audio_backend_capacity)
	{
		if (!app.audio_backend_was_empty) app.audio_backend_empty_events += 1;
		app.audio_backend_was_empty = true;
	}
	else
	{
		app.audio_backend_was_empty = false;
	}
	if (app.emulator_running && writable > queued) app.audio_starved_frames += writable - queued;

	while (writable)
	{
		Audio_ReadSpan span = audio_stream_acquire(app.audio);
		u32 count = Min(writable, span.frame_count);
		if (!count) {
			audio_stream_consume(app.audio, 0);
			break;
		}
		u32 written = os_audio_write_mono(span.samples, count);
		if (written > count)
		{
			LOG_ERROR("audio backend reported writing %u frames from a %u-frame span", written, count);
			written = count;
		}
		audio_stream_consume(app.audio, written);
		if (!written) break;
		writable -= written;
	}

	u64 overruns = audio_stream_overrun_frames(app.audio);
	u64 overrun_frames = overruns - app.audio_last_overrun_frames;
	Seconds now = seconds_now();
	if (now.seconds - app.audio_stats_begin.seconds >= 1.0)
	{
		if (app.audio_backend_empty_events || app.audio_starved_frames || overrun_frames)
		{
			LOG_WARN("audio continuity: queued minimum %u/%u frames, backend empty %llu times, producer short %llu frames, overruns %llu frames",
			app.audio_min_queued_frames, audio_stream_capacity_frames(app.audio), app.audio_backend_empty_events,
			app.audio_starved_frames, overrun_frames);
		}
		else
		{
			//	LOG_DEBUG("audio continuity: queued minimum %u/%u frames, no starvation or overruns",
			//		app.audio_min_queued_frames, audio_stream_capacity_frames(app.audio));
		}
		app.audio_min_queued_frames = MAX_VALUE_U32;
		app.audio_starved_frames = 0;
		app.audio_backend_empty_events = 0;
		app.audio_last_overrun_frames = overruns;
		app.audio_stats_begin = now;
	}
}

static void app_pace_frame(void)
{
	Seconds now = seconds_now();
	f64 elapsed = now.seconds - app.frame_begin.seconds;
	platform_sleep((u64)(Max(0.0, 1.0 / 60.0 - elapsed) * 1000.0));
	app.frame_begin = seconds_now();
}

static u32 app_chr_map_sprite_tile_index(const NES_PPUState *ppu, const NES_PPUSprite *sprite, u32 row)
{
	if (ppu->PPUCTRL & 0x20) {
		return (sprite->index & 1) * NES_PATTERN_TABLE_TILE_COUNT + (sprite->index & 0xFE) + row / NES_PATTERN_TILE_SIZE;
	}
	return !!(ppu->PPUCTRL & 0x08) * NES_PATTERN_TABLE_TILE_COUNT + sprite->index;
}

static void app_publish_sprites(void)
{
	const NES_PPUState *ppu = &app.published.state.ppu;
	u32 sprite_height = ppu->PPUCTRL & 0x20 ? 16 : 8;
	for (u32 index = 0; index < ArrayCount(app.published.sprites); ++index)
	{
		const NES_PPUSprite *source = &ppu->OAM[index];
		u32 tile_index = app_chr_map_sprite_tile_index(ppu, source, 0);
		i32 cell_x = (i32)(index % 16 * 16);
		i32 cell_y = (i32)(CHR_MAP_PATTERN_HEIGHT + index / 16 * 16);
		app.published.sprites[index] = (FrontendSprite) {
			.texture_region = {
				.x = cell_x + 4,
				.y = cell_y + (16 - (i32)sprite_height) / 2,
				.w = NES_PATTERN_TILE_SIZE,
				.h = (i32)sprite_height,
			},
			.selection_region = { cell_x, cell_y, 16, 16 },
			.pattern_mapping = app.published.chr_map.mappings[tile_index],
			.ppu_address = (u16)(tile_index * NES_PATTERN_TILE_SIZE * 2),
			.oam_index = (u8)index,
			.x = source->xpos,
			.y = (u8)(source->ypos + 1),
			.tile = source->index,
			.palette = source->attrs & 3,
			.behind_background = !!(source->attrs & 0x20),
			.flip_horizontal = !!(source->attrs & 0x40),
			.flip_vertical = !!(source->attrs & 0x80),
		};
	}
}

static void app_publish_palettes(void)
{
	for (u32 index = 0; index < ArrayCount(app.published.palettes); ++index)
	{
		FrontendPalette *palette = &app.published.palettes[index];
		palette->index = (u8)(index & 3);
		palette->is_sprite = index >= 4;
		for (u32 slot = 0; slot < ArrayCount(palette->colors); ++slot)
		{
			u8 address = (u8)((index >= 4 ? 0x10 : 0) + (index & 3) * 4 + slot);
			u8 color_index = app.published.chr_map.palette[address];
			palette->colors[slot] = (FrontendPaletteColor) {
				.color = app.published.palette[color_index & 63],
				.palette_address = address,
				.color_index = color_index,
			};
		}
	}
}

static void app_update_video_texture(void)
{
	Image_rgba_u8 image = push_image_rgba_u8(&app.frame_arena, v2i(NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT));
	for (u32 index = 0; index < NES_VIDEO_WIDTH * NES_VIDEO_HEIGHT; ++index) {
		image.data[index] = app.published.palette[app.published.video[0][index] & 63];
	}
	graphics_texture_update(app.video_texture, (GFX_TextureUpdateParams) {
		.dest = v2i(0, 0),
		.size = image.reso,
		.stride = image.elem_stride * sizeof(*image.data),
		.data = image.data,
	});
}

static void app_update_chr_texture(void)
{
	const NES_CHRMap *map = &app.published.chr_map;
	const NES_PPUState *ppu = &app.published.state.ppu;
	Image_rgba_u8 image = push_image_rgba_u8(&app.frame_arena, v2i(CHR_MAP_TEXTURE_WIDTH, CHR_MAP_TEXTURE_HEIGHT));
	memory_zero(image.data, image.elem_stride * image.reso.y * sizeof(*image.data));
	for (u32 tile_index = 0; tile_index < NES_PATTERN_TILE_COUNT; ++tile_index)
	{
		u32 table = tile_index / NES_PATTERN_TABLE_TILE_COUNT;
		u32 local_index = tile_index % NES_PATTERN_TABLE_TILE_COUNT;
		u32 tile_x = table * 128 + local_index % 16 * NES_PATTERN_TILE_SIZE;
		u32 tile_y = local_index / 16 * NES_PATTERN_TILE_SIZE;
		for (u32 y = 0; y < NES_PATTERN_TILE_SIZE; ++y) {
			for (u32 x = 0; x < NES_PATTERN_TILE_SIZE; ++x) {
				u8 color = map->palette[map->tiles[tile_index].pixels[y][x]];
				image.data[(tile_y + y) * CHR_MAP_TEXTURE_WIDTH + tile_x + x] = app.published.palette[color & 63];
			}
		}
	}
	u32 sprite_height = ppu->PPUCTRL & 0x20 ? 16 : 8;
	for (u32 sprite_index = 0; sprite_index < ArrayCount(ppu->OAM); ++sprite_index)
	{
		const NES_PPUSprite *sprite = &ppu->OAM[sprite_index];
		rect_i32 texture_region = app.published.sprites[sprite_index].texture_region;
		for (u32 y = 0; y < sprite_height; ++y)
		{
			u32 source_y = sprite->attrs & 0x80 ? sprite_height - 1 - y : y;
			u32 tile_index = app_chr_map_sprite_tile_index(ppu, sprite, source_y);
			for (u32 x = 0; x < NES_PATTERN_TILE_SIZE; ++x)
			{
				u32 source_x = sprite->attrs & 0x40 ? NES_PATTERN_TILE_SIZE - 1 - x : x;
				u8 palette_slot = map->tiles[tile_index].pixels[source_y % NES_PATTERN_TILE_SIZE][source_x];
				if (palette_slot) {
					u8 color = map->palette[0x10 + (sprite->attrs & 3) * 4 + palette_slot];
					image.data[(texture_region.y + y) * CHR_MAP_TEXTURE_WIDTH + texture_region.x + x] = app.published.palette[color & 63];
				}
			}
		}
	}
	graphics_texture_update(app.chr_texture, (GFX_TextureUpdateParams) {
		.dest = v2i(0, 0),
		.size = image.reso,
		.stride = image.elem_stride * sizeof(*image.data),
		.data = image.data,
	});
}

static void app_publish(void)
{
	app.published.state = debugger_capture_state(app.debugger);
	PROF_BLOCK("capture video") debugger_capture_video(app.debugger, &app.published.video[0][0], NES_VIDEO_WIDTH);
	PROF_BLOCK("capture chr map") debugger_capture_chr_map(app.debugger, &app.published.chr_map);
	app.published.prg_rom_size = debugger_prg_rom_size(app.debugger);
	memory_copy(app.published.palette, nes_palette, sizeof(nes_palette));
	PROF_BLOCK("publish sprites") app_publish_sprites();
	PROF_BLOCK("publish palettes") app_publish_palettes();
	++ app.published.generation;
	app.published.valid = true;
	PROF_BLOCK("capture video texture") app_update_video_texture();
	PROF_BLOCK("capture char texture") app_update_chr_texture();
}

static String app_rom_name(String path)
{
	u32 first = 0;
	for (u32 index = 0; index < path.size; ++index) {
		if (path.text[index] == '/' || path.text[index] == '\\') {
			first = index + 1;
		}
	}
	return string_slice(path, first, path.size - first);
}

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

// Todo, remove this entirely!
static void app_handle_window_commands(void)
{
	for (u32 index = 0; index < os_window_event_count(app.os_window); ++index)
	{
		const OS_Event *event = os_window_event(app.os_window, index);
		if (event->type != OS_EVENT_KEY_RELEASE) continue;
		if (event->modifiers & OS_MODIFIER_CONTROL)
		{
			i32 font_size = app.ui->theme.code.size;
			if (event->key == OS_Key_Equal || event->key == OS_Key_NumPadPlus) font_size = Min(font_size + 1, UI_CODE_FONT_SIZE_MAX);
			else if (event->key == OS_Key_Minus || event->key == OS_Key_NumPadMinus) font_size = Max(font_size - 1, UI_CODE_FONT_SIZE_MIN);
			else if (event->key == OS_Key_0) font_size = UI_CODE_FONT_SIZE_DEFAULT;
			if (font_size != app.ui->theme.code.size)
			{
				app.ui->theme.code.size = font_size;
				LOG_INFO("UI font size %d px", font_size);
			}
		}
		if (event->key == OS_Key_F7) app.crt_enabled = !app.crt_enabled;
	}
}

typedef struct
{
	f32 emission;
}
AppBoxPaintData;

typedef struct
{
	UI_Box *root;
	UI_Box *top;
	UI_Box *panel_host;
	UI_Box *bottom;
}
AppShell;

static UI_Box *app_status_text(UI_BoxBuilder *builder, u64 key, String text, UI_TextStyle style, f32 emission)
{
	UI_Box *box = ui_text_box_string(builder, key, style, text);
	if (emission > 0.f)
	{
		AppBoxPaintData *paint = arena_push_zero(builder->arena, sizeof(*paint));
		paint->emission = emission;
		box->user = paint;
	}
	return box;
}

static void app_draw_box_tree(UI_Box *box)
{
	AppBoxPaintData *paint = box->user;
	if (paint) ui_push_emission(app.ui, paint->emission);
	if (box->ops && box->ops->paint) {
		box->ops->paint(box);
	}
	if (paint) ui_pop_emission(app.ui);
	for (u32 child_index = 0; child_index < box->child_count; child_index ++) {
		app_draw_box_tree(box->children[child_index]);
	}
}

static AppShell app_build_shell(rect_f32 window_rect)
{
	AppShell shell = {};
	UI_BoxBuilder b;
	UI_BoxDesc root_desc = ui_box_desc();
	root_desc.axis = AXIS_Y;
	root_desc.size[AXIS_X] = ui_box_fill(1.f);
	root_desc.size[AXIS_Y] = ui_box_fill(1.f);
	shell.root = ui_box_builder_begin(&b, &app.ui->frame_arena, app.ui, 1, LIT("application shell"), root_desc);

	f32 status_height = app.ui->theme.code.size + 8.f;
	UI_TextStyle style = app.ui->theme.code;
	style.align.y = 0.5f;

	ui_push(&b);
	ui_axis(&b, AXIS_X);
	ui_size(&b, AXIS_X, ui_box_fill(1.f));
	ui_size(&b, AXIS_Y, ui_box_pixels(status_height));
	ui_padd(&b, AXIS_X, 6.f, 6.f);
	shell.top = ui_box_begin(&b, 1, LIT("top status"));
	ui_pop(&b);

	ui_push(&b);
	ui_size(&b, AXIS_Y, ui_box_fill(1.f));
	style.color = app.ui->theme.palette.cyan;
	app_status_text(&b, 1, LIT("ORBITER"), style, app.ui->theme.palette.emission_medium);

	ui_push(&b);
	ui_size(&b, AXIS_X, ui_box_flex(0.f, 1.f));
	style.color = app.ui->theme.text_subtle;
	app_status_text(&b, 2, LIT("  |  github.com/MicroRJ  |  "), style, 0.f);
	ui_pop(&b);

	if (app.mode == APP_MODE_REWINDING && app.rewind_direction == -1)
	{
		style.color = app.ui->theme.palette.error;
		f32 pulse = 0.5f + 0.5f * sinf((f32)seconds_now().seconds * 3.f * 4);
		app_status_text(&b, 3, LIT("<< REWINDING"), style, app.ui->theme.palette.emission_high * pulse);
	}
	else if (app.mode == APP_MODE_REWINDING && app.rewind_direction == +1)
	{
		style.color = app.ui->theme.palette.amber;
		f32 pulse = 0.5f + 0.5f * sinf((f32)seconds_now().seconds * 3.f * 4);
		app_status_text(&b, 3, LIT("REPLAYING >>"), style, app.ui->theme.palette.emission_high * pulse);
	}
	else if (app.mode == APP_MODE_REWINDING)
	{
		style.color = app.ui->theme.palette.error;
		app_status_text(&b, 3, LIT("<< REWINDING >>"), style, app.ui->theme.palette.emission_high);
	}
	else if (app.mode == APP_MODE_EMULATOR)
	{
		if (app.emulator_running)
		{
			style.color = app.ui->theme.palette.amber;
			app_status_text(&b, 3, LIT("RUNNING"), style, app.ui->theme.palette.emission_medium);
		}
		else
		{
			f32 pulse = 0.5f + 0.5f * sinf((f32)seconds_now().seconds * 3.f);
			style.color = app.ui->theme.palette.error;
			app_status_text(&b, 3, LIT("PAUSED"), style, 0.06f + pulse * 0.16f);
		}
	}

	style.color = app.ui->theme.text_subtle;
	if (app.app_gif.recording) app_status_text(&b, 4, LIT("   REC APP"), style, 0.f);
	else if (app.ppu_gif.recording) app_status_text(&b, 4, LIT("   REC PPU"), style, 0.f);
	if (app.apu_muted) app_status_text(&b, 5, LIT("   MUTED"), style, 0.f);

	ui_push(&b);
	ui_size(&b, AXIS_X, ui_box_fill(1.f));
	ui_box_make(&b, 6, LIT("top spacer"));
	ui_pop(&b);

	ui_push(&b);
	ui_size(&b, AXIS_X, ui_box_flex(0.f, 1.f));
	style.align.x = 0.f;
	ui_text_box_sized(&b, 7, style, LIT("FPS 999.9"), "FPS %02.2f", app.frames_per_second);
	ui_pop(&b);
	style.align.x = 0.f;
	ui_text_box_sized(&b, 8, style, LIT("FRAME 999999999"), " FRAME %llu", app.published.generation);
	ui_pop(&b);

	ui_box_end(&b);

	ui_push(&b);
	ui_size(&b, AXIS_X, ui_box_fill(1.f));
	ui_size(&b, AXIS_Y, ui_box_fill(1.f));
	shell.panel_host = ui_box_make(&b, 2, LIT("panel host"));
	ui_pop(&b);

	ui_push(&b);
	ui_axis(&b, AXIS_X);
	ui_size(&b, AXIS_X, ui_box_fill(1.f));
	ui_size(&b, AXIS_Y, ui_box_pixels(status_height));
	ui_padd(&b, AXIS_X, 6.f, 6.f);
	shell.bottom = ui_box_begin(&b, 3, LIT("bottom status"));
	ui_pop(&b);

	ui_push(&b);
	ui_size(&b, AXIS_Y, ui_box_fill(1.f));
	String rom_name = app_rom_name(app.last_rom_path);
	String bottom_left = rom_name.size ? push_formatted(&app.ui->frame_arena, "ROM   %.*s", rom_name.size, rom_name.text) : LIT("NO CARTRIDGE");
	ui_push(&b);
	ui_size(&b, AXIS_X, ui_box_flex(0.f, 1.f));
	style.align.x = 0.f;
	app_status_text(&b, 1, bottom_left, style, 0.f);
	ui_pop(&b);
	ui_push(&b);
	ui_size(&b, AXIS_X, ui_box_fill(1.f));
	ui_box_make(&b, 2, LIT("bottom spacer"));
	ui_pop(&b);
	ui_push(&b);
	ui_size(&b, AXIS_X, ui_box_flex(0.f, 3.f));
	style.align.x = 1.f;
	app_status_text(&b, 3, LIT("F PPU   F5 RUN   F7 CRT   F8 PPU GIF   F9 APP GIF   F10 STEP   F11 FULLSCREEN"), style, 0.f);
	ui_pop(&b);
	ui_pop(&b);
	ui_box_end(&b);

	ui_box_builder_end(&b);
	ui_box_measure(shell.root, (UI_BoxConstraints) { .min = window_rect.size, .max = window_rect.size });
	ui_box_layout(shell.root, window_rect);
	return shell;
}

static void app_draw_shell(AppShell shell)
{
	ui_draw_rect(app.ui, shell.top->rect, app.ui->theme.slider_track);
	ui_draw_rect(app.ui, shell.bottom->rect, app.ui->theme.slider_track);
	ui_draw_rect(app.ui, rect_f32_from_slice(shell.top->rect, AXIS_Y, -1.f), app.ui->theme.panel_outline);
	ui_draw_rect(app.ui, rect_f32_from_slice(shell.bottom->rect, AXIS_Y, 1.f), app.ui->theme.panel_outline);
	app_draw_box_tree(shell.root);
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
	if (app.ppu_gif.recording)
	{
		Image_rgba_u8 image = push_image_rgba_u8(&app.frame_arena, v2i(NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT));
		for (u32 index = 0; index < NES_VIDEO_WIDTH * NES_VIDEO_HEIGHT; ++index) {
			image.data[index] = app.published.palette[app.published.video[0][index] & 63];
		}
		if (!gif_recorder_frame(&app.ppu_gif, image.data, image.elem_stride * sizeof(*image.data))) {
			LOG_ERROR("PPU GIF capture failed");
		}
		if (app.ppu_gif.frame_count >= GIF_CAPTURE_MAX_FRAMES) {
			gif_recorder_end(&app.ppu_gif);
		}
	}
	if (app.app_gif.recording)
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
		if (!graphics_texture_read(frame_texture, linear, size.x * sizeof(*linear)))
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
	gfx_window_resize(app.gfx_window, size);
}

// Todo, remove this!
static void app_acquire_graphics_outputs(vec2i size)
{
	vec2i blur_size = v2i(Max(2, (size.x + 1) / 2), Max(2, (size.y + 1) / 2));
	app.frame_texture = gfx_acquire_transient_texture(app.renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_RARE_UPDATES,
		.bind_flags = GFX_TEXTURE_BIND_INPUT | GFX_TEXTURE_BIND_OUTPUT,
		.format = GRAPHICS_FORMAT_RGBA_F32,
		.size = size,
		.sampler = GRAPHICS_SAMPLER_POINT,
		.label = "application frame",
	});
	app.effect_texture = gfx_acquire_transient_texture(app.renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_RARE_UPDATES,
		.bind_flags = GFX_TEXTURE_BIND_INPUT | GFX_TEXTURE_BIND_OUTPUT,
		.format = GRAPHICS_FORMAT_RGBA_F32,
		.size = size,
		.sampler = GRAPHICS_SAMPLER_POINT,
		.label = "application effect",
	});
	app.bloom_texture = gfx_acquire_transient_texture(app.renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_RARE_UPDATES,
		.bind_flags = GFX_TEXTURE_BIND_INPUT | GFX_TEXTURE_BIND_OUTPUT,
		.format = GRAPHICS_FORMAT_RGBA_F32,
		.size = size,
		.sampler = GRAPHICS_SAMPLER_LINEAR,
		.label = "UI emission",
	});
	app.blur_horizontal_texture = gfx_acquire_transient_texture(app.renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_RARE_UPDATES,
		.bind_flags = GFX_TEXTURE_BIND_INPUT | GFX_TEXTURE_BIND_OUTPUT,
		.format = GRAPHICS_FORMAT_RGBA_F32,
		.size = blur_size,
		.sampler = GRAPHICS_SAMPLER_LINEAR,
		.label = "blur ping",
	});
	app.blur_vertical_texture = gfx_acquire_transient_texture(app.renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_RARE_UPDATES,
		.bind_flags = GFX_TEXTURE_BIND_INPUT | GFX_TEXTURE_BIND_OUTPUT,
		.format = GRAPHICS_FORMAT_RGBA_F32,
		.size = blur_size,
		.sampler = GRAPHICS_SAMPLER_LINEAR,
		.label = "blur pong",
	});
}


GFX_Texture *create_hdr_pass_output_texture(GFX_Texture *input, const char *label)
{
	GFX_Texture *output = gfx_acquire_transient_texture(app.renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_RARE_UPDATES,
		.bind_flags = GFX_TEXTURE_BIND_INPUT | GFX_TEXTURE_BIND_OUTPUT,
		.format = GRAPHICS_FORMAT_RGBA_F32,
		.size = gfx_texture_size(input),
		.sampler = GRAPHICS_SAMPLER_POINT,
		.label = label,
	});
	return output;
}

GFX_Texture *app_crt_barrel_pass(GFX_Texture *input)
{
	GFX_Texture *output = create_hdr_pass_output_texture(input, "barrel pass output");
	gfx_begin_pass(app.draw, (GFX_PassDesc) { .output = output, .clear = true, .clear_color = COLOR_BLACK });
	draw_barrel(app.draw, (Draw_BarrelParams) { .texture = input, .strength = 1.f });
	gfx_end_pass(app.draw);
	return output;
}

GFX_Texture *app_rewind_pass(GFX_Texture *input)
{
	GFX_Texture *output = create_hdr_pass_output_texture(input, "rewind pass output");
	gfx_begin_pass(app.draw, (GFX_PassDesc) { .output = output, .clear = true, .clear_color = COLOR_BLACK });
	draw_rewind(app.draw, (Draw_RewindParams) { .texture = input, .time = (f32)fmod(seconds_now().seconds, 1024.0), .strength = 1.f });
	gfx_end_pass(app.draw);
	return output;
}

GFX_Texture *app_crt_scanlines_pass(GFX_Texture *input)
{
	GFX_Texture *output = create_hdr_pass_output_texture(input, "scanlines pass output");
	gfx_begin_pass(app.draw, (GFX_PassDesc) { .output = output, .clear = true, .clear_color = COLOR_BLACK });
	draw_crt_scanlines(app.draw, input);
	gfx_end_pass(app.draw);
	return output;
}



typedef struct
{
	GFX_Texture *video;
	GFX_Texture *bloom;
}
App_PPUFrame;

static App_PPUFrame app_prepare_exclusive_ppu_frame(void)
{
	App_PPUFrame result = {
		.video = app.video_texture,
	};
	if (!app.crt_enabled) {
		return result;
	}

	gfx_begin_pass(app.draw, (GFX_PassDesc) {
		.output = app.crt_scanline_texture,
		.clear = true,
		.clear_color = COLOR_BLACK,
	});
	draw_crt_scanlines(app.draw, app.video_texture);
	gfx_end_pass(app.draw);

	gfx_begin_pass(app.draw, (GFX_PassDesc) {
		.output = app.crt_bloom_ping_texture,
		.clear = true,
		.clear_color = COLOR_BLACK,
	});
	draw_luminance(app.draw, (Draw_LuminanceParams) {
		.texture = app.crt_scanline_texture,
		.threshold = 0.35f,
		.gain = 0.35f,
	});
	gfx_end_pass(app.draw);

	gfx_begin_pass(app.draw, (GFX_PassDesc) {
		.output = app.crt_bloom_pong_texture,
		.clear = true,
		.clear_color = COLOR_BLACK,
	});
	draw_gaussian_blur(app.draw, (Draw_GaussianBlurParams) {
		.texture = app.crt_bloom_ping_texture,
		.direction = v2(1.f, 0.f),
		.sigma = 2.f,
	});
	gfx_end_pass(app.draw);

	gfx_begin_pass(app.draw, (GFX_PassDesc) {
		.output = app.crt_bloom_ping_texture,
		.clear = true,
		.clear_color = COLOR_BLACK,
	});
	draw_gaussian_blur(app.draw, (Draw_GaussianBlurParams) {
		.texture = app.crt_bloom_pong_texture,
		.direction = v2(0.f, 1.f),
		.sigma = 2.f,
	});
	gfx_end_pass(app.draw);

	result.video = app.crt_scanline_texture;
	// app.crt_bloom_ping_texture;
	result.bloom = 0;
	return result;
}

static void app_draw_exclusive_ppu(GFX_Texture *frame_texture, rect_f32 window_rect)
{
	vec2 presentation_size = v2(4.f, 3.f);
	f32 scale = Min(window_rect.w / presentation_size.x, window_rect.h / presentation_size.y);
	rect_f32 video_rect = rect_f32_align(window_rect, v2(presentation_size.x * scale, presentation_size.y * scale), v2(0.5f, 0.5f));
	video_rect = rect_f32_round_out(video_rect);

	App_PPUFrame ppu = app_prepare_exclusive_ppu_frame();
	gfx_begin_pass(app.draw, (GFX_PassDesc) {
		.output = frame_texture,
		.clear = true,
		.clear_color = COLOR_BLACK,
	});
	draw_image(app.draw, (Draw_TextureParams) {
		.rect = video_rect,
		.texture = ppu.video,
		.region = { 0, 0, NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT },
		.tint = COLOR_WHITE,
		.sampler = GRAPHICS_SAMPLER_POINT,
	});
	if (ppu.bloom)
	{
		draw_image(app.draw, (Draw_TextureParams) {
			.rect = video_rect,
			.texture = ppu.bloom,
			.region = { 0, 0, NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT },
			.tint = COLOR_WHITE,
			.sampler = GRAPHICS_SAMPLER_LINEAR,
			.blender = GFX_BLENDER_ADDITIVE,
		});
	}
	gfx_end_pass(app.draw);
}

static Color_SRGBA app_emission_color(Color_SRGBA color, f32 emission)
{
	color.r = color_encode_srgb_channel(color_decode_srgb_channel(color.r) * emission);
	color.g = color_encode_srgb_channel(color_decode_srgb_channel(color.g) * emission);
	color.b = color_encode_srgb_channel(color_decode_srgb_channel(color.b) * emission);
	return color;
}

static void app_draw_ui_layer(UI_LayerKind layer_kind, GFX_Texture *backdrop_texture, b32 emission_only)
{
	const UI_Layer *layer = &ui_frame(app.ui)->layers[layer_kind];
	u32 replay_count = 0;
	PROF_BLOCK("ui layer playback") for (UI_DrawCommand *command = layer->first; command; command = command->next)
	{
		replay_count++;
		if (emission_only && command->emission <= 0.f) {
			continue;
		}
		if (command->has_clip) {
			draw_push_clip(app.draw, command->clip);
		}

		switch (command->kind)
		{
			case UI_DRAW_COMMAND_RECT:
			{
				draw_rect(app.draw, (Draw_RectParams) {
					.rect = command->rect.rect,
					.color = emission_only ? app_emission_color(command->rect.color, command->emission) : command->rect.color,
					.corner_radii.top_left = command->rect.roundness,
					.corner_radii.top_right = command->rect.roundness,
					.corner_radii.bot_left = command->rect.roundness,
					.corner_radii.bot_right = command->rect.roundness,
					.edge_softness = command->rect.edge_softness,
				});
			} break;
			case UI_DRAW_COMMAND_IMAGE:
			{
				const UI_ImageParams *params = &command->image.params;
				draw_image(app.draw, (Draw_TextureParams) {
					.rect = params->rect,
					.texture = params->texture,
					.region = params->region,
					.tint = emission_only ? app_emission_color(COLOR_WHITE, command->emission) : COLOR_WHITE,
					.sampler = params->sampler,
					.blender = params->blender,
					.shader = params->shader,
				});
			} break;
			case UI_DRAW_COMMAND_TEXT:
			{
				text_gfx_draw_run(app.text_gfx, app.draw, command->text.run,
				command->text.position, emission_only ? app_emission_color(command->text.color, command->emission) : command->text.color);
			} break;
			case UI_DRAW_COMMAND_INSET_SHADOW:
			{
				if (!emission_only) {
					draw_inset_shadow(app.draw, command->inset_shadow.rect, command->inset_shadow.strength);
				}
			} break;
			case UI_DRAW_COMMAND_BACKDROP:
			{
				if (emission_only) {
					break;
				}
				Assert(backdrop_texture);
				draw_glass(app.draw, (Draw_GlassParams) {
					.texture = backdrop_texture,
					.rect = command->backdrop.rect,
					.corner_radius = command->backdrop.corner_radius,
					.distortion = command->backdrop.distortion,
					.distortion_width = command->backdrop.distortion_width,
					.saturation = command->backdrop.saturation,
					.tint = command->backdrop.tint,
					.grain = command->backdrop.grain,
					.highlight = command->backdrop.highlight,
					.shadow = command->backdrop.shadow,
				});
			} break;
			default: Assert(!"invalid UI draw command");
		}

		if (command->has_clip) {
			draw_pop_clip(app.draw);
		}
	}
	prof_add_metric(PROF_METRIC_UI_COMMAND_REPLAYS, replay_count);
}

static GFX_Texture *app_blur_backdrop(GFX_Texture *frame_texture)
{
	prof_add_metric(PROF_METRIC_UI_BACKDROP_BLURS, 1);
	gfx_begin_pass(app.draw, (GFX_PassDesc) {
		.output = app.blur_horizontal_texture,
	});
	draw_texture_copy(app.draw, frame_texture);
	gfx_end_pass(app.draw);

	for (u32 round = 0; round < 2; ++round)
	{
		gfx_begin_pass(app.draw, (GFX_PassDesc) {
			.output = app.blur_vertical_texture,
		});
		draw_gaussian_blur(app.draw, (Draw_GaussianBlurParams) {
			.texture = app.blur_horizontal_texture,
			.direction = v2(1.f, 0.f),
			.sigma = 3.f,
		});
		gfx_end_pass(app.draw);

		gfx_begin_pass(app.draw, (GFX_PassDesc) {
			.output = app.blur_horizontal_texture,
		});
		draw_gaussian_blur(app.draw, (Draw_GaussianBlurParams) {
			.texture = app.blur_vertical_texture,
			.direction = v2(0.f, 1.f),
			.sigma = 3.f,
		});
		gfx_end_pass(app.draw);
	}
	return app.blur_horizontal_texture;
}

static void app_draw_ui_bloom(UI_LayerKind layer_kind, GFX_Texture *frame_texture, rect_f32 window_rect)
{
	if (!ui_frame(app.ui)->layers[layer_kind].has_emission) {
		return;
	}
	prof_add_metric(PROF_METRIC_UI_BLOOM_LAYERS, 1);

	gfx_begin_pass(app.draw, (GFX_PassDesc) { .output = app.bloom_texture, .clear = true, .clear_color = COLOR_TRANSPARENT });
	app_draw_ui_layer(layer_kind, 0, true);
	gfx_end_pass(app.draw);

	gfx_begin_pass(app.draw, (GFX_PassDesc) { .output = app.blur_horizontal_texture, .clear = true, .clear_color = COLOR_TRANSPARENT });
	draw_texture_copy(app.draw, app.bloom_texture);
	gfx_end_pass(app.draw);

	// Todo, instead of clearing we could instead just disable blending
	gfx_begin_pass(app.draw, (GFX_PassDesc) { .output = app.blur_vertical_texture, .clear = true, .clear_color = COLOR_BLACK });
	draw_gaussian_blur(app.draw, (Draw_GaussianBlurParams) { .texture = app.blur_horizontal_texture, .direction = v2(1.f, 0.f), .sigma = 5.f });
	gfx_end_pass(app.draw);

	gfx_begin_pass(app.draw, (GFX_PassDesc) { .output = app.blur_horizontal_texture, .clear = true, .clear_color = COLOR_BLACK });
	draw_gaussian_blur(app.draw, (Draw_GaussianBlurParams) { .texture = app.blur_vertical_texture, .direction = v2(0.f, 1.f), .sigma = 5.f });
	gfx_end_pass(app.draw);

	gfx_begin_pass(app.draw, (GFX_PassDesc) { .output = frame_texture });
	draw_image(app.draw, (Draw_TextureParams) {
		.rect = window_rect,
		.texture = app.blur_horizontal_texture,
		.region = rect_i32_from_size(gfx_texture_size(app.blur_horizontal_texture)),
		.tint = color_srgba(0xB8B8B8),
		.sampler = GRAPHICS_SAMPLER_LINEAR,
		.blender = GFX_BLENDER_ADDITIVE,
	});
	gfx_end_pass(app.draw);
}

static void app_compose_ui_layers(GFX_Texture *frame_texture, rect_f32 window_rect)
{
	const UI_Frame *ui = ui_frame(app.ui);
	for (u32 layer_index = 0; layer_index < UI_LAYER_COUNT; ++layer_index)
	{
		UI_LayerKind layer_kind = (UI_LayerKind)layer_index;
		const UI_Layer *layer = &ui->layers[layer_index];
		GFX_Texture *backdrop = layer->has_backdrops ? app_blur_backdrop(frame_texture) : 0;
		gfx_begin_pass(app.draw, (GFX_PassDesc) { .output = frame_texture });
		app_draw_ui_layer(layer_kind, backdrop, false);
		gfx_end_pass(app.draw);
		app_draw_ui_bloom(layer_kind, frame_texture, window_rect);
	}
}

static void app_draw_debugger(GFX_Texture *frame_texture, rect_f32 window_rect)
{
	AppShell shell = app_build_shell(window_rect);
	ViewFrameData frame = {
		.debugger = app.debugger,
		.execution_graph = debugger_execution_graph(app.debugger),
		.execution_activity = &app.execution_activity,
		.publication = &app.published,
		.video_texture = app.video_texture,
		.chr_texture = app.chr_texture,
		.ui = app.ui,
		.scratch = &app.ui->frame_arena,
		.draw_box_tree = app_draw_box_tree,
	};

	gfx_begin_pass(app.draw, (GFX_PassDesc) {
		.output = frame_texture,
		.clear = true,
		.clear_color = COLOR_BLACK,
	});
	draw_rect(app.draw, (Draw_RectParams) {
		.rect = window_rect,
		.color = app.ui->theme.background,
	});
	panels_update_and_draw(app.panels, app.os_window, &frame, shell.panel_host->viewport);
	app_draw_shell(shell);
	gfx_end_pass(app.draw);

	PROF_BLOCK("ui composition") app_compose_ui_layers(frame_texture, window_rect);
}

static void app_draw(void)
{
	rect_f32 window_rect = rect_f32_from_size(v2_from_v2i(app.os_window->size));
	app_update_fps();
	ui_begin_frame(app.ui);
	os_window_set_cursor(app.os_window, OS_CURSOR_POINTER);
	app_handle_window_commands();

	if (app.mode == APP_MODE_REWINDING && app.rewind_direction == -1) if(debugger_undo_frame(app.debugger)) audio_stream_discard(app.audio);
	if (app.mode == APP_MODE_REWINDING && app.rewind_direction == +1) if(debugger_redo_frame(app.debugger)) audio_stream_discard(app.audio);

	app_resize_graphics_outputs(app.os_window->size);
	gfx_begin_frame(app.draw);
	app_acquire_graphics_outputs(app.os_window->size);
	GFX_Texture *frame_texture = app.frame_texture;

	if (app.exclusive_ppu_mode) {
		app_draw_exclusive_ppu(frame_texture, window_rect);
	}
	else {
		app_draw_debugger(frame_texture, window_rect);
	}

	GFX_Texture *present_texture = frame_texture;
	GFX_Texture *effect_output = app.effect_texture;
	if (app.mode == APP_MODE_REWINDING) {

		gfx_begin_pass(app.draw, (GFX_PassDesc) { .output = effect_output, .clear = true, .clear_color = COLOR_BLACK });
		draw_rewind(app.draw, (Draw_RewindParams) { .texture = present_texture, .time = (f32)fmod(seconds_now().seconds, 1024.0), .strength = 1.f });
		gfx_end_pass(app.draw);

		present_texture = effect_output;
		effect_output = frame_texture;
	}
	if (app.crt_enabled) {
		gfx_begin_pass(app.draw, (GFX_PassDesc) {
			.output = effect_output,
			.clear = true,
			.clear_color = COLOR_BLACK,
		});
		draw_barrel(app.draw, (Draw_BarrelParams) {
			.texture = present_texture,
			.strength = 1.f,
		});
		gfx_end_pass(app.draw);
		present_texture = effect_output;
	}

	gfx_begin_pass(app.draw, (GFX_PassDesc) {
		.output = gfx_window_texture(app.gfx_window),
	});
	draw_blit(app.draw, present_texture);
	gfx_end_pass(app.draw);

	text_gfx_sync(app.text_gfx);

	gfx_end_frame(app.draw);

	app_capture_gifs(present_texture);

	PROF_BLOCK("present wait") gfx_window_present(app.gfx_window);

	ui_end_frame(app.ui);
}

static void app_frame(void)
{
	ARENA_SCOPE(&app.frame_arena)
	{
		prof_begin_frame();
		PROF_BLOCK("main frame incl wait")
		{
			debugger_update_cpu_mapping(app.debugger);
			AppInput input = app_translate_input_events_based_on_mode();
			u64 ppu_cycles = app_handle_input(input);

			const Program *program = debugger_program(app.debugger);
			u32 refinement_budget = program->refinement_pass_count < 2 ? 2048 : 128;

			PROF_BLOCK("debug stepping")         debugger_run(app.debugger, ppu_cycles);

			if (app.mode == APP_MODE_EMULATOR && app.emulator_running) {
				PROF_BLOCK("emulation") app_fill_audio();
			}
			PROF_BLOCK("snapshot")               debugger_capture_frame(app.debugger);
			PROF_BLOCK("program refinement")     debugger_refine(app.debugger, refinement_budget);
			PROF_BLOCK("drain audio")            app_drain_audio();
			PROF_BLOCK("execution activity")     execution_activity_update(&app.execution_activity, debugger_execution_graph(app.debugger), seconds_now().seconds);

			PROF_BLOCK("application")            app_publish();
			PROF_BLOCK("application draw")       app_draw();
			PROF_BLOCK("frame pacing")           app_pace_frame();
		}
		prof_close_frame();
	}
}

static void app_init(void)
{
	debugger_log_file = fopen(debugger_log_path, "w");
	if (debugger_log_file) {
		Assert(logger_add_sink(app_file_log_sink, debugger_log_file));
	} else {
		LOG_WARN("failed to open session log '%s'", debugger_log_path);
	}
	Assert(os_init());
	Assert(os_graphical_init());
	app.arena = arena_create(0, "app arena");
	app.frame_arena = arena_create(0, "app frame arena");

	OS_AudioInfo audio_info;
	Assert(os_audio_init(&audio_info));
	u32 audio_capacity = Max(audio_info.buffer_frame_count * 4,
	Max(audio_info.sample_rate / 10, 1));
	app.audio = audio_stream_create(&app.arena, (Audio_StreamDesc) {
		.sample_rate = audio_info.sample_rate,
		.channels = 1,
		.frame_capacity = audio_capacity,
	});
	app.audio_backend_capacity = audio_info.buffer_frame_count;
	app.audio_min_queued_frames = MAX_VALUE_U32;
	app.audio_stats_begin = seconds_now();

	// Font_Handle code_font = ttf_load(app_read_file(&app.arena, "data/fonts/IBMPlexMono-Medium.ttf"));
	ttf_init_api();
	Font_Handle code_font = ttf_load(app_read_file(&app.arena, "data/fonts/Saira/static/Saira-Medium.ttf"));
	UI_Theme theme = ui_default_theme(code_font);


	app.os_window = os_window_create((OS_WindowDesc) {
		.title = "Orbiter",
		.title_bar = {
			.enabled = true,
			.dark = true,
			.background_rgb = 0x050A0C,
			.text_rgb = 0x718783,
			.border_rgb = 0x718783,
		},
	});
	Assert(app.os_window);
	app.renderer = gfx_renderer_create(&app.arena);
	app.gfx_window = gfx_window_create(&app.arena, app.renderer, app.os_window);
	app.draw = draw_create(&app.arena, app.renderer);
	app.text = text_create(&app.arena);
	app.text_gfx = text_gfx_create(&app.arena, app.renderer, app.text);
	app.ui = ui_create(&app.arena, app.os_window, app.text, theme);
	app.panels = panels_create(&app.arena);
	app.crt_enabled = true;
	app.debugger = debugger_create(&app.arena, audio_info.sample_rate);
	// Todo, get rid of these, we already have a transient texture system
	app.video_texture = gfx_create_texture(app.renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_PER_FRAME,
		.bind_flags = GFX_TEXTURE_BIND_INPUT,
		.format = GRAPHICS_FORMAT_RGBA_U8_SRGB,
		.size = v2i(NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT),
		.sampler = GRAPHICS_SAMPLER_POINT,
		.label = "NES video",
	});
	app.crt_scanline_texture = gfx_create_texture(app.renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_RARE_UPDATES,
		.bind_flags = GFX_TEXTURE_BIND_INPUT | GFX_TEXTURE_BIND_OUTPUT,
		.format = GRAPHICS_FORMAT_RGBA_F32,
		.size = v2i(NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT),
		.sampler = GRAPHICS_SAMPLER_POINT,
		.label = "CRT scanlines",
	});
	app.crt_bloom_ping_texture = gfx_create_texture(app.renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_RARE_UPDATES,
		.bind_flags = GFX_TEXTURE_BIND_INPUT | GFX_TEXTURE_BIND_OUTPUT,
		.format = GRAPHICS_FORMAT_RGBA_F32,
		.size = v2i(NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT),
		.sampler = GRAPHICS_SAMPLER_LINEAR,
		.label = "CRT bloom ping",
	});
	app.crt_bloom_pong_texture = gfx_create_texture(app.renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_RARE_UPDATES,
		.bind_flags = GFX_TEXTURE_BIND_INPUT | GFX_TEXTURE_BIND_OUTPUT,
		.format = GRAPHICS_FORMAT_RGBA_F32,
		.size = v2i(NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT),
		.sampler = GRAPHICS_SAMPLER_LINEAR,
		.label = "CRT bloom pong",
	});
	app.chr_texture = gfx_create_texture(app.renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_PER_FRAME,
		.bind_flags = GFX_TEXTURE_BIND_INPUT,
		.format = GRAPHICS_FORMAT_RGBA_U8_SRGB,
		.size = v2i(256, 192),
		.sampler = GRAPHICS_SAMPLER_POINT,
		.label = "NES CHR map",
	});
	app_load_config();
	if (debugger_has_cartridge(app.debugger)) {
		app_restore_state();
	}
	app_publish();
	app.frame_begin = seconds_now();
}

static void app_shutdown(void)
{
	if (debugger_has_cartridge(app.debugger)) {
		app_save_state();
	}
	app_save_config();
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

int main(void)
{
	app_init();
	while (os_window_is_open(app.os_window))
	{
		os_graphical_poll();
		if (os_window_is_open(app.os_window)) app_frame();
	}
	app_shutdown();
	return 0;
}
