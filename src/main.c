#include "debugger.h"
#include "audio_mixer.h"
#include "audio_stream.h"
#include "graphics.h"
#include "text.h"
#include "ttf_api.h"
#include "text_gfx.h"
#include "execution_activity.h"
#include "gif_recorder.h"
#include "nes_target.h"
#include "orb.h"
#include "nes_serialize.h"
#include "os.h"
#include "actions.h"
#include "app.h"
#include "app_window.h"
#include "elf.h"

global const char app_user_config_path[]  = "data/user.tab";
global const char debugger_log_path[]     = "data/debugger.log";
global const char debugger_program_path[] = "data/program.dump";
global const char app_font_path[]         = "data/fonts/Saira/static/Saira-Medium.ttf";

global App app = { };
global FILE *debugger_log_file;

// TODO(RJ) why doesn't this return a bytespan instead!
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
static b32 app_write_file_atomic(Str path, ByteSpan data)
{
	char temporary_path[1024];
	i32 length = snprintf(temporary_path, sizeof(temporary_path), "%.*s.tmp", path.size, path.data);
	if (length <= 0 || (u32) length >= sizeof(temporary_path)) return false;
	if (!app_write_file(temporary_path, data.data, data.size)) return false;
	if (platform_move_file(temporary_path, path.data, true)) return true;
	platform_remove_file(temporary_path);
	return false;
}

static void app_log_elf_diagnostic(elf_State *state)
{
	elf_Diagnostic diagnostic;
	if (!elf_get_diagnostic(state, &diagnostic)) LOG_ERROR("failed to parse user config '%s'", app_user_config_path);
	else LOG_ERROR("%s:%u:%llu: %.*s", app_user_config_path, diagnostic.line, diagnostic.column, (int)diagnostic.message.size, diagnostic.message.data);
}

static void app_load_user_config(void)
{
	Platform_File_Info info;
	if (!platform_get_file_info(app_user_config_path, &info)) return;

	Str source = app_read_file(&app.frame_arena, app_user_config_path);
	if (!source.data)
	{
		LOG_ERROR("failed to read user config '%s'", app_user_config_path);
		app.user_config_save_suppressed = true;
		return;
	}

	elf_State *state = elf_create_state();
	Assert(state);
	b32 restored = false;
	b32 parsed = elf_push_constant_expr(state, app_user_config_path, (elf_StrSlice) { source.data, source.size });
	if (!parsed) app_log_elf_diagnostic(state);
	else if (elf_type(state, -1) == ELF_VALUE_TYPE_TABLE)
	{
		i32 table = elf_abs_index(state, -1);
		elf_Integer version;
		if (elf_get_field(state, table, "version"))
		{
			b32 valid_version = elf_to_int(state, -1, &version) && version == 1;
			Assert(elf_pop(state, 1));
			if (valid_version && elf_get_field(state, table, "window"))
			{
				restored = app_window_state_read(state, -1, app.window);
				Assert(elf_pop(state, 1));
			}
		}
	}
	if (parsed && !restored) LOG_ERROR("invalid user config '%s'", app_user_config_path);
	if (!restored) app.user_config_save_suppressed = true;
	else LOG_INFO("restored user config from '%s'", app_user_config_path);
	elf_destroy_state(state);
}

static void app_save_user_config(void)
{
	if (app.user_config_save_suppressed)
	{
		LOG_WARN("not overwriting user config '%s' after a load failure", app_user_config_path);
		return;
	}

	elf_State *state = elf_create_state();
	Assert(state);
	elf_new_table(state);
	i32 table = elf_abs_index(state, -1);
	elf_push_int(state, 1);
	Assert(elf_set_field(state, table, "version"));
	app_window_state_push(state, app.window);
	Assert(elf_set_field(state, table, "window"));

	b32 saved = false;
	if (elf_push_value_source(state, table))
	{
		elf_StrSlice source;
		if (elf_to_str(state, -1, &source) && source.size <= MAX_VALUE_U32) saved = app_write_file_atomic(str_from_cstr(app_user_config_path), byte_span(source.data, (u32)source.size));
	}
	if (!saved) LOG_ERROR("failed to save user config '%s'", app_user_config_path);
	else LOG_INFO("saved user config to '%s'", app_user_config_path);
	elf_destroy_state(state);
}

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

static b32 app_save_state();

#define APP_BIND(kind_, activation_, key_, modifiers_) { .action = { .kind = kind_ }, .key_chord = { activation_, key_, modifiers_ } }
#define APP_BIND_SPLIT(axis_, key_) { .action = { .kind = APP_ACTION_SPLIT_PANEL, .split_panel = { axis_ } }, .key_chord = { APP_KEY_CHORD_ON_RELEASE, key_, OS_MODIFIER_CONTROL } }
#define APP_BIND_VIEW(index_, key_, modifiers_) { .action = { .kind = APP_ACTION_OPEN_VIEW, .open_view = { index_ } }, .key_chord = { APP_KEY_CHORD_ON_RELEASE, key_, modifiers_ } }
#define APP_BIND_SCRUB(direction_, key_) { .action = { .kind = APP_ACTION_SCRUB, .scrub = { direction_ } }, .key_chord = { APP_KEY_CHORD_WHILE_DOWN, key_, OS_MODIFIER_CONTROL } }
#define APP_BIND_FONT(pixels_, key_) { .action = { .kind = APP_ACTION_ADJUST_UI_FONT_SIZE, .ui_font = { pixels_ } }, .key_chord = { APP_KEY_CHORD_ON_RELEASE, key_, OS_MODIFIER_CONTROL } }
#define APP_BIND_VOLUME(delta_, key_) { .action = { .kind = APP_ACTION_ADJUST_VOLUME, .volume = { delta_ } }, .key_chord = { APP_KEY_CHORD_ON_PRESS, key_, OS_MODIFIER_CONTROL }, .allow_repeat = true }

static const App_KeyBinding app_key_bindings[] =
{
	APP_BIND_VOLUME(-0.1f, OS_Key_Down),
	APP_BIND_VOLUME(+0.1f, OS_Key_Up),
	APP_BIND(APP_ACTION_TOGGLE_LIBRARY_OVERLAY, APP_KEY_CHORD_ON_RELEASE, OS_Key_Tab, 0),

	APP_BIND_SPLIT(AXIS_X, OS_Key_H),
	APP_BIND_SPLIT(AXIS_Y, OS_Key_V),
	APP_BIND(APP_ACTION_CLOSE_PANEL, APP_KEY_CHORD_ON_RELEASE, OS_Key_Q, OS_MODIFIER_CONTROL),

	APP_BIND_VIEW(0,  OS_Key_1, 0),
	APP_BIND_VIEW(1,  OS_Key_2, 0),
	APP_BIND_VIEW(2,  OS_Key_3, 0),
	APP_BIND_VIEW(3,  OS_Key_4, 0),
	APP_BIND_VIEW(4,  OS_Key_5, 0),
	APP_BIND_VIEW(5,  OS_Key_6, 0),
	APP_BIND_VIEW(6,  OS_Key_7, 0),
	APP_BIND_VIEW(7,  OS_Key_8, 0),
	APP_BIND_VIEW(8,  OS_Key_9, 0),
	APP_BIND_VIEW(9,  OS_Key_0, 0),
	APP_BIND_VIEW(10, OS_Key_1, OS_MODIFIER_CONTROL),
	APP_BIND_VIEW(11, OS_Key_2, OS_MODIFIER_CONTROL),
	APP_BIND_VIEW(12, OS_Key_3, OS_MODIFIER_CONTROL),
	APP_BIND_VIEW(13, OS_Key_4, OS_MODIFIER_CONTROL),
	APP_BIND_VIEW(14, OS_Key_5, OS_MODIFIER_CONTROL),
	APP_BIND_VIEW(15, OS_Key_6, OS_MODIFIER_CONTROL),

	APP_BIND_SCRUB(-1, OS_Key_Left),
	APP_BIND_SCRUB(+1, OS_Key_Right),
	APP_BIND(APP_ACTION_OPEN_ROM, APP_KEY_CHORD_ON_RELEASE, OS_Key_O, OS_MODIFIER_CONTROL),
	APP_BIND(APP_ACTION_RESET, APP_KEY_CHORD_ON_RELEASE, OS_Key_R, OS_MODIFIER_CONTROL),
	APP_BIND(APP_ACTION_SAVE_STATE, APP_KEY_CHORD_ON_RELEASE, OS_Key_S, OS_MODIFIER_CONTROL),
	APP_BIND(APP_ACTION_RESTORE_STATE, APP_KEY_CHORD_ON_RELEASE, OS_Key_L, OS_MODIFIER_CONTROL),
	APP_BIND(APP_ACTION_DUMP_PROGRAM, APP_KEY_CHORD_ON_RELEASE, OS_Key_K, OS_MODIFIER_CONTROL),
	APP_BIND_FONT(+1, OS_Key_Equal),
	APP_BIND_FONT(+1, OS_Key_NumPadPlus),
	APP_BIND_FONT(-1, OS_Key_Minus),
	APP_BIND_FONT(-1, OS_Key_NumPadMinus),
	APP_BIND(APP_ACTION_RESET_UI_FONT_SIZE, APP_KEY_CHORD_ON_RELEASE, OS_Key_0, OS_MODIFIER_CONTROL),
	APP_BIND(APP_ACTION_MUTE, APP_KEY_CHORD_ON_RELEASE, OS_Key_M, 0),
	APP_BIND(APP_ACTION_TOGGLE_PPU_FULLSCREEN, APP_KEY_CHORD_ON_RELEASE, OS_Key_F, 0),
	APP_BIND(APP_ACTION_EXIT_PPU_FULLSCREEN, APP_KEY_CHORD_ON_RELEASE, OS_Key_Esc, 0),
	APP_BIND(APP_ACTION_TOGGLE_FULLSCREEN, APP_KEY_CHORD_ON_RELEASE, OS_Key_F11, 0),
	APP_BIND(APP_ACTION_TOGGLE_FULLSCREEN, APP_KEY_CHORD_ON_RELEASE, OS_Key_Enter, OS_MODIFIER_ALT),
	APP_BIND(APP_ACTION_TOGGLE_RUNNING, APP_KEY_CHORD_ON_RELEASE, OS_Key_F5, 0),
	APP_BIND(APP_ACTION_TOGGLE_UI_DEBUG_BOUNDS, APP_KEY_CHORD_ON_RELEASE, OS_Key_F6, 0),
	APP_BIND(APP_ACTION_TOGGLE_CRT, APP_KEY_CHORD_ON_RELEASE, OS_Key_F7, 0),
	APP_BIND(APP_ACTION_TAKE_PPU_SCREENSHOT, APP_KEY_CHORD_ON_RELEASE, OS_Key_F8, 0),
	APP_BIND(APP_ACTION_TOGGLE_PPU_CAPTURE, APP_KEY_CHORD_ON_RELEASE, OS_Key_F8, OS_MODIFIER_SHIFT),
	APP_BIND(APP_ACTION_TAKE_APP_SCREENSHOT, APP_KEY_CHORD_ON_RELEASE, OS_Key_F9, 0),
	APP_BIND(APP_ACTION_TOGGLE_APP_CAPTURE, APP_KEY_CHORD_ON_RELEASE, OS_Key_F9, OS_MODIFIER_SHIFT),
	APP_BIND(APP_ACTION_STEP, APP_KEY_CHORD_ON_RELEASE, OS_Key_F10, 0),
};

#undef APP_BIND
#undef APP_BIND_SPLIT
#undef APP_BIND_VIEW
#undef APP_BIND_SCRUB
#undef APP_BIND_FONT
#undef APP_BIND_VOLUME

static NES_Input app_translate_keyboard_input_for_emulator(App_GameInput source)
{
	NES_Input input = 0;
	if (source & APP_GAME_INPUT_UP)     input |= NES_INPUT_UP;
	if (source & APP_GAME_INPUT_DOWN)   input |= NES_INPUT_DOWN;
	if (source & APP_GAME_INPUT_LEFT)   input |= NES_INPUT_LEFT;
	if (source & APP_GAME_INPUT_RIGHT)  input |= NES_INPUT_RIGHT;
	if (source & APP_GAME_INPUT_A)      input |= NES_INPUT_A;
	if (source & APP_GAME_INPUT_B)      input |= NES_INPUT_B;
	if (source & APP_GAME_INPUT_START)  input |= NES_INPUT_START;
	if (source & APP_GAME_INPUT_SELECT) input |= NES_INPUT_SELECT;
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

static void app_update_emulator_input(const App_WindowOutput *window_output)
{
	for (u32 player = 0; player < 2; player ++)
	{
		NES_Input input = app_translate_keyboard_input_for_emulator(window_output->keyboard_input[player]) | app_translate_controller_input_for_emulator(player);
		nes_emulator_set_input(&app.emulator, player, input);
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

static void app_play_ui_feedback(UI_Feedback feedback)
{
	if (feedback & UI_FEEDBACK_PRESS) audio_mixer_play(app.audio_mixer, app.ui_click, (Audio_PlayDesc) { .gain = 0.35f });
}

static void app_transfer_save(Orb_SaveState *save, NES_Emulator *emulator, b32 write);

static b32 app_open_orb(Str path)
{
	Assert(path.data && path.size);
	app_save_state();

	Orb_Store next_store;
	orb_store_init(&next_store);
	Orb *orb = orb_from_file(&next_store, path);
	if (!orb)
	{
		LOG_ERROR("could not load orb '%.*s'", path.size, path.data);
		orb_store_destroy(&next_store);
		return false;
	}

	Orb_Game game = orb->game;
	Orb_GameMetadata metadata = game.metadata;
	NES_SetupParams setup_params = {
		.mapper = metadata.mapper,
		.vmirror = metadata.vmirror,
		.four_screen = metadata.four_screen,
		.has_trainer = metadata.has_trainer,
		.prg_rom = byte_span(game.prg_rom_data, metadata.prg_rom_size),
		.chr_rom = byte_span(game.chr_rom_data, metadata.chr_rom_size),
	};
	if (!nes_supports_setup_params(setup_params))
	{
		LOG_ERROR("unsupported cartridge in '%.*s'", path.size, path.data);
		orb_store_destroy(&next_store);
		return false;
	}

	Assert(nes_setup_emulator(&app.emulator, setup_params));
	orb_store_destroy(&app.orb_store);
	app.orb_store = next_store;
	app.active_save = 0;

	u64 now = app_unix_time_ms();
	Orb_SaveNode *save = orb->first_save;
	if (save) app_transfer_save(&save->state, &app.emulator, true);
	else
	{
		save = arena_push_zero(&app.orb_store.arena, sizeof(*save));
		save->orb = orb;
		save->metadata.kind = ORB_SAVE_RESUME;
		save->metadata.created_unix_ms = now;
		save->metadata.updated_unix_ms = now;
		orb->first_save = save;
		orb->last_save = save;
		orb->save_count = 1;
	}
	app.active_save = save;
	debugger_reset(app.debugger);
	app.transport = (App_Transport) { .state = APP_TRANSPORT_PAUSED };
	// TODO(RJ) we can't just do this, it has to be driven based off of intent!
	app_window_set_library_visible(app.window, false);
	LOG_INFO("opened '%.*s'", path.size, path.data);
	return true;
}

static b32 app_handle_actions(App_WindowOutput input)
{
	b32 step_requested = false;
	i32 scrub_direction = 0;
	b32 scrub_input_active = false;
	b32 wants_open_file = 0;
	for (u32 index = 0; index < input.action_count; index++)
	{
		App_Action action = input.actions[index];
		switch (action.kind)
		{
			case APP_ACTION_OPEN_ROM:
			{
				wants_open_file ++;
			} break;
			case APP_ACTION_OPEN_LIBRARY_ORB:
			{
				u32 index = action.open_library_orb.index;
				if (index < app.orb_library_count) app_open_orb(app.orb_library[index].path);
				else LOG_ERROR("invalid library orb index %u", index);
			} break;
			case APP_ACTION_RESET:
			{
				// TODO(RJ) reset the active game through the runtime model.
			} break;
			case APP_ACTION_SAVE_STATE: app_save_state(); break;
			case APP_ACTION_RESTORE_STATE: app_restore_state(); break;
			case APP_ACTION_DUMP_PROGRAM:
			{
				if (program_dump(app.debugger, debugger_program_path)) LOG_INFO("dumped program model to '%s'", debugger_program_path);
				else LOG_ERROR("failed to dump program model to '%s'", debugger_program_path);
			} break;
			case APP_ACTION_TOGGLE_RUNNING:
			{
				if (nes_emulator_ready_to_run(&app.emulator))
				{
					App_TransportState *state = app.transport.state == APP_TRANSPORT_SCRUBBING ? &app.transport.return_state : &app.transport.state;
					Assert(*state == APP_TRANSPORT_PAUSED || *state == APP_TRANSPORT_RUNNING);
					*state = *state == APP_TRANSPORT_RUNNING ? APP_TRANSPORT_PAUSED : APP_TRANSPORT_RUNNING;
					LOG_INFO(*state == APP_TRANSPORT_RUNNING ? "running realtime" : "paused");
				}
			} break;
			case APP_ACTION_STEP:
			{
				step_requested = true;
			} break;
			case APP_ACTION_SCRUB:
			{
				scrub_input_active = true;
				scrub_direction += action.scrub.direction;
			} break;
			case APP_ACTION_TAKE_PPU_SCREENSHOT:
			{
				app.ppu_screenshot_requested = true;
			} break;
			case APP_ACTION_TOGGLE_PPU_CAPTURE:
			{
				if (app.ppu_gif.recording) gif_recorder_end(&app.ppu_gif);
				else if (!gif_recorder_begin(&app.ppu_gif, v2i(NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT), "ppu_capture")) LOG_ERROR("failed to begin PPU GIF capture");
			} break;
			case APP_ACTION_ADJUST_VOLUME:
			{
				app.ppu_volume_target = CLAMP(app.ppu_volume_target + action.volume.delta, 0.f, 1.f);
			} break;
			case APP_ACTION_MUTE:
			{
				app.ppu_volume_target = 0.f;
			} break;
			case APP_ACTION_NONE: break;
			default: Assert(false);
		}
	}

	scrub_direction = CLAMP(scrub_direction, -1, 1);
	b32 scrub_modifier_down = !!(input.modifiers & OS_MODIFIER_CONTROL);
	if (nes_emulator_ready_to_run(&app.emulator) && scrub_input_active)
	{
		if (app.transport.state != APP_TRANSPORT_SCRUBBING)
		{
			Assert(app.transport.state == APP_TRANSPORT_PAUSED || app.transport.state == APP_TRANSPORT_RUNNING);
			app.transport.return_state = app.transport.state;
			app.transport.state = APP_TRANSPORT_SCRUBBING;
		}
		app.transport.direction = scrub_direction;
	}
	else if (app.transport.state == APP_TRANSPORT_SCRUBBING)
	{
		if (scrub_modifier_down) {
			app.transport.direction = 0;
		}
		else
		{
			Assert(app.transport.return_state == APP_TRANSPORT_PAUSED || app.transport.return_state == APP_TRANSPORT_RUNNING);
			app.transport.state = app.transport.return_state;
			app.transport.direction = 0;
		}
	}

	if (wants_open_file)
	{
		Str path = os_dialog_open_file(&app.frame_arena);
		if (path.size) app_open_orb(path);
	}

	return step_requested;
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
			app.transport.state = APP_TRANSPORT_PAUSED;
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
			app.transport.state = APP_TRANSPORT_PAUSED;
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

static void app_capture_ppu(void)
{
	enum { GIF_CAPTURE_MAX_FRAMES = 60 * 30 };
	PROF_BLOCK("ppu capture")
	{
		if (app.ppu_screenshot_requested)
		{
			app.ppu_screenshot_requested = false;
			if (!screenshot_write_png(app.published.video, v2i(NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT), NES_VIDEO_WIDTH * sizeof(*app.published.video), "ppu_screenshot")) LOG_ERROR("failed to save PPU screenshot");
		}
		if (app.ppu_gif.recording)
		{
			if (!gif_recorder_frame(&app.ppu_gif, app.published.video, NES_VIDEO_WIDTH * sizeof(*app.published.video))) LOG_ERROR("PPU GIF capture failed");
			if (app.ppu_gif.frame_count >= GIF_CAPTURE_MAX_FRAMES) gif_recorder_end(&app.ppu_gif);
		}
	}
}


static void app_tick(App_WindowOutput input)
{
	b32 step_requested = app_handle_actions(input);

	if (nes_emulator_ready_to_run(&app.emulator))
	{
		if (app.transport.state != APP_TRANSPORT_SCRUBBING)
		{
			app_update_emulator_input(&input);

			if (step_requested) {
				app.transport.state = APP_TRANSPORT_PAUSED;
				PROF_BLOCK("emulation step") debugger_step(app.debugger);
			}
			else if (app.transport.state == APP_TRANSPORT_RUNNING) {
				PROF_BLOCK("emulation") app_run_frame();
			}
		}
		else if (app.transport.direction == -1)
		{
			// TODO(RJ) this needs to be configurable, and also, we should just be able
			// to skip backwards arbitrarily instead of doing one step at a time
			for(u32 i=0;i<4;++i) {
				if(!debugger_undo_snapshot(app.debugger)) break;
			}
		}
		else if (app.transport.direction == +1)
		{
			for(u32 i=0;i<4;++i) {
				if(!debugger_redo_snapshot(app.debugger)) break;
			}
		}

		PROF_BLOCK("update cpu mapping")   debugger_update_cpu_mapping(app.debugger);
		if (app.transport.state != APP_TRANSPORT_RUNNING) program_update(app.debugger);
		PROF_BLOCK("execution activity")   execution_activity_update(&app.execution_activity, debugger_execution_graph(app.debugger), seconds_now().seconds);
		PROF_BLOCK("publish NES target")   nes_target_publish(&app.published, &app.emulator);
		PROF_BLOCK("upload video texture") app_upload_video_texture();
		PROF_BLOCK("upload CHR texture")   app_upload_chr_texture();

	}
	app_capture_ppu();
	if (app.transport.state != APP_TRANSPORT_RUNNING) audio_stream_discard(app.audio);
	app.ppu_volume += (app.ppu_volume_target - app.ppu_volume) * 0.35f;
	PROF_BLOCK("UI audio feedback") app_play_ui_feedback(input.feedback);

	PROF_BLOCK("mix and output audio") app_mix_and_output_audio();

}

static void app_frame(void)
{
	arena_reset(&app.frame_arena);
	prof_begin_frame();
	gfx_renderer_begin_frame(app.renderer);
	App_WindowOutput window_output = app_window_begin_frame(app.window, (App_KeyMap) { app_key_bindings, ArrayCount(app_key_bindings) });
	PROF_BLOCK("app tick") app_tick(window_output);
	PROF_BLOCK("app window render") app_window_render(app.window);
	prof_close_frame();
}

static b32 app_init(void)
{
	if (!os_init()) {
		LOG_ERROR("failed to initialize os");
		return false;
	}
	if (!platform_create_directories("data\\orbs")) {
		LOG_ERROR("failed to create orb storage directory 'data\\orbs'");
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

	app.renderer = gfx_create_renderer(&app.arena);
	app.text = text_create(&app.arena);
	app.text_gfx = text_gfx_create(&app.arena, app.renderer, app.text);
	app.window = app_window_create(&app.arena, &app, (App_WindowDesc) {
		.title = "Orbiter v0.1.0",
		.theme = theme,
	});
	app_load_user_config();

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
	// app.dummy_texture = gfx_create_texture_from_image(app.renderer, push_image_rgba_u8_from_file(&app.frame_arena, LIT("data\\ppu_screenshot_001.png")), GRAPHICS_SAMPLER_POINT);

	app.debugger = debugger_create(&app.arena, &app.emulator);
	return true;
}

static void app_pace_frame(void)
{
	Seconds now = seconds_now();
	f64 elapsed = now.seconds - app.frame_begin.seconds;
	platform_sleep((u64)(Max(0.0, 1.0 / 60.0 - elapsed) * 1000.0));
	app.frame_begin = seconds_now();
}

static void app_transfer_memory(void *dst, void *src, u64 size, u32 write) {
	if (write) {
		void *tmp = dst;
		dst = src;
		src = tmp;
	}
	memory_copy(dst, src, size);
}

static void app_transfer_save(Orb_SaveState *save, NES_Emulator *emulator, b32 write)
{
	app_transfer_memory(&save->scheduler_clock, &emulator->scheduler_clock, sizeof(save->scheduler_clock), write);
	app_transfer_memory(&save->sample_phase, &emulator->sample_phase, sizeof(save->sample_phase), write);
	app_transfer_memory(&save->values, &emulator->values, sizeof(save->values), write);
	app_transfer_memory(&save->input_state, &emulator->input_state, sizeof(save->input_state), write);
	app_transfer_memory(&save->cpu_stall_cycles, &emulator->cpu_stall_cycles, sizeof(save->cpu_stall_cycles), write);
	app_transfer_memory(&save->cpu, &emulator->cpu, sizeof(save->cpu), write);
	app_transfer_memory(&save->ppu, &emulator->ppu, sizeof(save->ppu), write);
	app_transfer_memory(&save->apu, &emulator->apu, sizeof(save->apu), write);
	app_transfer_memory(&save->controllers, &emulator->controllers, sizeof(save->controllers), write);
	app_transfer_memory(&save->wram, emulator->_wram, sizeof(save->wram), write);
	app_transfer_memory(&save->vram, emulator->_vram, sizeof(save->vram), write);
	app_transfer_memory(&save->chr_ram, emulator->chr_ram, sizeof(save->chr_ram), write);
	app_transfer_memory(&save->prg_ram, emulator->prg_ram, sizeof(save->prg_ram), write);
	app_transfer_memory(&save->video, emulator->video, sizeof(save->video), write);
}

static void bytes_to_hex(u8 *bytes, u32 nbytes, char *buf)
{
	for (u32 i = 0; i < nbytes; ++ i)
	{
		buf[i*2+0] = "0123456789ABCDEF"[bytes[i] >> 0 & 0xF];
		buf[i*2+1] = "0123456789ABCDEF"[bytes[i] >> 4 & 0xF];
	}
}

static b32 app_save_state()
{
	if (!app.active_save) return true;

	Orb_SaveNode *save = app.active_save;
	Orb *orb = save->orb;

	u64 now = (u64) platform_unix_time_ms();
	save->metadata.updated_unix_ms = now;
	app_transfer_save(&app.active_save->state, &app.emulator, false);

	ByteSpan orb_data = orb_write(&app.frame_arena, orb);
	char hash_str[32*2 + 1] = { };
	bytes_to_hex(orb->game_hash.bytes, 32, hash_str);
	Str path = str_push_copy_f(&app.frame_arena, "data\\orbs\\%s.orb", hash_str);
	b32 orb_saved = app_write_file_atomic(path, orb_data);
	if (!orb_saved) LOG_ERROR("failed to save orb '%s'", path.data);
	else LOG_INFO("saved orb to '%s'", path.data);

	Str resume_path = LIT("data\\orbs\\resume.orb");
	b32 resume_saved = app_write_file_atomic(resume_path, orb_data);
	if (!resume_saved) LOG_ERROR("failed to save resume orb '%s'", resume_path.data);
	return orb_saved && resume_saved;
}

static void app_shutdown(void)
{
	app_save_state();
	app_save_user_config();
	orb_store_destroy(&app.orb_store);
	app_window_destroy(app.window);
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


typedef void App_FileVisitor(Str path, Platform_File_Info info, void *user);

// Paths borrow stack storage and are valid only for the duration of the callback.
static b32 app_for_file(App_FileVisitor *visitor, void *user)
{
	Assert(visitor);
	Platform_Directory_Open_Result opened = platform_open_directory("data\\orbs");
	if (opened.error == PLATFORM_ERROR_NOT_FOUND) return true;
	if (opened.error != PLATFORM_ERROR_NONE) return false;

	b32 success = true;
	for (;;)
	{
		char name[512];
		Platform_Directory_Next_Result next = platform_next_directory(&opened.directory, name, sizeof(name));
		if (next.error != PLATFORM_ERROR_NONE)
		{
			success = false;
			break;
		}
		if (!next.has_entry) break;
		if (next.info.is_directory || next.info.is_symbolic_link) continue;

		Str filename = str_from_data(name, (u32)next.name_size);
		char path[1024];
		i32 size = snprintf(path, sizeof(path), "data\\orbs\\%s", name);
		if (size <= 0 || (u32)size >= sizeof(path))
		{
			success = false;
			break;
		}
		visitor(str_from_data(path, (u32)size), next.info, user);
	}
	platform_close_directory(&opened.directory);
	return success;
}

void app_file_visitor(Str path, Platform_File_Info info, void *user)
{
	if (!str_ends_nocase(path, LIT(".orb")) || str_ends_nocase(path, LIT("\\resume.orb"))) return;
	if (app.orb_library_count >= app.orb_library_capacity)
	{
		LOG_WARN("orb library capacity reached; skipping '%.*s'", path.size, path.data);
		return;
	}

	u64 arena_start = app.arena.position;
	// TODO(RJ)
	//	We keep Orb as a shell, Orbs only point into memory, which we already
	//	do partially.
	//	Then we can use orbs as both library entries and runtime model.
	//	We allocate the Orb shell on a persistent library wide arena, and the actual
	//	file data on a separate arena.
	//	Only one orb needs to be active, so when an orb is loaded, the arena is reset.
	//	We'd have to make sure to zero the pointers of the orb we just evitected.
	Str file_str = app_read_file(&app.arena, path.data);
	Orb *orb = file_str.data ? orb_read(&app.arena, byte_span(file_str.data, file_str.size)) : 0;
	if (!orb)
	{
		app.arena.position = arena_start;
		LOG_ERROR("could not read orb '%.*s'", path.size, path.data);
		return;
	}

	for (u32 index = 0; index < app.orb_library_count; index ++)
	{
		if (!hash256_match(app.orb_library[index].orb->game_hash, orb->game_hash)) continue;
		app.arena.position = arena_start;
		return;
	}

	Str stored_path = str_push_copy(&app.arena, path);
	orb->disk_path = stored_path;
	app.orb_library[app.orb_library_count ++] = (App_OrbLibEntry) { .orb = orb, .path = stored_path };
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

	app.orb_library_count = 0;
	app.orb_library_capacity = 1024;
	app.orb_library = arena_push_zero(&app.arena, app.orb_library_capacity * sizeof(*app.orb_library));
	if (!app_for_file(app_file_visitor, 0)) LOG_ERROR("could not enumerate orb library");

	app.frame_begin = seconds_now();
	while (app_window_is_open(app.window))
	{
		os_poll_windows();
		if (app_window_is_open(app.window)) {
			app_frame();
		}

		PROF_BLOCK("pacing") app_pace_frame();
	}
	app_shutdown();
	return 0;
}
