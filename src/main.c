#include "nes_process.h"
#include "audio_mixer.h"
#include "audio_stream.h"
#include "graphics.h"
#include "text.h"
#include "ttf_api.h"
#include "text_gfx.h"
#include "execution_activity.h"
#include "gif_recorder.h"
#include "nes_target.h"
#include "ines_importer.h"
#include "os.h"
#include "actions.h"
#include "app.h"
#include "app_library_store.h"
#include "app_window.h"
#include "elf.h"

global const char app_user_config_path[]  = "data/user.tab";
global const char debugger_log_path[]     = "data/debugger.log";
global const char debugger_program_path[] = "data/program.dump";
global const char app_font_path[]         = "data/fonts/Saira/static/Saira-Medium.ttf";
global const char app_library_path[]      = "data/library/library.elf";

global App app = {
	.ppu_volume = 0.3f,
	.ppu_volume_target = 0.3f,
	.ppu_volume_restore = 0.3f,
};
global FILE *debugger_log_file;

static Str app_title_from_path(Str path)
{
	u32 begin = 0;
	for (u32 index = 0; index < path.size; index ++) if (path.data[index] == '/' || path.data[index] == '\\') begin = index + 1;
	u32 end = path.size;
	for (u32 index = end; index > begin; index --)
	{
		if (path.data[index - 1] != '.') continue;
		if (index - 1 > begin) end = index - 1;
		break;
	}
	return str_slice(path, begin, end - begin);
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
	if (!app_write_file(temporary_path, data.data, data.size))
	{
		platform_remove_file(temporary_path);
		return false;
	}
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

	ByteSpan source_bytes = push_file(&app.frame_arena, str_from_cstr(app_user_config_path));
	if (!source_bytes.data || source_bytes.size > MAX_VALUE_U32)
	{
		LOG_ERROR("failed to read user config '%s'", app_user_config_path);
		app.user_config_save_suppressed = true;
		return;
	}
	Str source = str_from_data((char *)source_bytes.data, (u32)source_bytes.size);

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
	return (u64)(app.play_time_seconds * 1000.0 + 0.5);
}

// TODO(RJ) why are we using CRT files for this!?
static void app_file_log_sink(const LogRecord *record, void *user_data)
{
	FILE *file = user_data;
	fprintf(file, "%llu %s(%i) %-7s: %*s%s\n", record->sequence, record->source.file, record->source.line, record->tag, record->indent * 2, "", record->message);
	fflush(file);
}

static b32 app_restore_state(void);
static b32 app_reset_emulator(void);
static b32 app_save_state(void);

#define APP_BIND(kind_, activation_, key_, modifiers_) { .action = { .kind = kind_ }, .key_chord = { activation_, key_, modifiers_ } }
#define APP_BIND_REPEAT(kind_, key_, modifiers_) { .action = { .kind = kind_ }, .key_chord = { APP_KEY_CHORD_ON_PRESS, key_, modifiers_ }, .allow_repeat = true }
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
	APP_BIND_REPEAT(APP_ACTION_STEP, OS_Key_F10, 0),
};

#undef APP_BIND
#undef APP_BIND_REPEAT
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
		nes_emulator_set_input(&app.debugger->emulator, player, input);
	}
}

static void app_finish_emulator_state_change(void)
{
	app.transport = (App_Transport) { .state = APP_TRANSPORT_PAUSED };
	audio_stream_discard(app.audio);
	nes_process_reset(app.debugger);
	program_reset(&app.program, app.debugger->program_rom_size, app.debugger->program_ram_size);
}

static void app_rebuild_program_listing(void)
{
	program_rebuild(&app.program, &app.debugger->emulator, app.debugger->program_evidence);
}

static b32 app_restore_state(void)
{
	if (!app.session.library_save)
	{
		LOG_WARN("no active save to restore");
		return false;
	}

	Assert(nes_emulator_ready_to_run(&app.debugger->emulator));
	app.debugger->emulator.state = app.session.save.state;
	Assert(nes_emulator_valid(&app.debugger->emulator));
	app_finish_emulator_state_change();
	LOG_INFO("restored active save");
	return true;
}

static b32 app_reset_emulator(void)
{
	if (!nes_emulator_ready_to_run(&app.debugger->emulator))
	{
		LOG_WARN("no active game to reset");
		return false;
	}

	nes_reset_emulator(&app.debugger->emulator);
	app_finish_emulator_state_change();
	LOG_INFO("reset active game");
	return true;
}

static App_LibrarySave *app_resume_save(App_LibraryGame *game)
{
	for (u32 index = 0; index < game->save_count; index ++) if (game->saves[index].kind == APP_LIBRARY_SAVE_RESUME) return &game->saves[index];
	return 0;
}

static b32 app_open_library_game(App_LibraryGame *game, b32 save_current)
{
	Assert(game);
	if (save_current && !app_save_state())
	{
		LOG_ERROR("could not switch to '%.*s' because the active save could not be written", game->title.size, game->title.data);
		return false;
	}
	App_LibrarySave *save = app_resume_save(game);
	if (!save)
	{
		LOG_ERROR("game '%.*s' has no resume save", game->title.size, game->title.data);
		return false;
	}

	Arena next_game_arena = arena_create(0, "next game arena");
	App_GameSession next_session = {
		.library_game = game,
		.library_save = save,
	};
	if (!app_library_store_read_game(app.library_store, &next_game_arena, game, save, &next_session.game, &next_session.save))
	{
		LOG_ERROR("could not load game '%.*s'", game->title.size, game->title.data);
		arena_destroy(&next_game_arena);
		return false;
	}
	NES_Emulator *next_emulator = arena_push(&app.frame_arena, sizeof(*next_emulator));
	if (!nes_setup_emulator(next_emulator, next_session.game))
	{
		LOG_ERROR("unsupported cartridge in '%.*s'", game->title.size, game->title.data);
		arena_destroy(&next_game_arena);
		return false;
	}
	next_emulator->state = next_session.save.state;
	if (!nes_emulator_valid(next_emulator))
	{
		LOG_ERROR("invalid save state in '%.*s'", game->title.size, game->title.data);
		arena_destroy(&next_game_arena);
		return false;
	}

	arena_destroy(&app.game_arena);
	app.game_arena = next_game_arena;
	app.session = next_session;
	memory_copy(&app.debugger->emulator, next_emulator, sizeof(app.debugger->emulator));
	app.play_time_seconds = 0;
	nes_process_clear_breakpoints(app.debugger);
	app_finish_emulator_state_change();
	// TODO(RJ) we can't just do this, it has to be driven based off of intent!
	app_window_set_library_visible(app.window, false);
	LOG_INFO("opened '%.*s'", game->title.size, game->title.data);
	return true;
}

static b32 app_import_game(Str path)
{
	Assert(path.data && path.size);
	if (!app_save_state())
	{
		LOG_ERROR("could not import '%.*s' because the active save could not be written", path.size, path.data);
		return false;
	}

	ByteSpan source_bytes = push_file(&app.frame_arena, path);
	NES_Game source = {};
	if (!source_bytes.data || !ines_import(source_bytes, &source))
	{
		LOG_ERROR("could not read iNES game '%.*s'", path.size, path.data);
		return false;
	}
	Str title = app_title_from_path(path);
	NES_Emulator *emulator = arena_push(&app.frame_arena, sizeof(*emulator));
	if (!nes_setup_emulator(emulator, source))
	{
		LOG_ERROR("unsupported cartridge in '%.*s'", path.size, path.data);
		return false;
	}
	App_Save save_data = { .state = emulator->state };
	App_LibraryGame *game = 0;
	App_LibrarySave *save = 0;
	b32 imported = app_library_store_import_game(app.library_store, &app.frame_arena, source, title, &save_data, &game, &save);
	if (!imported)
	{
		LOG_ERROR("could not add '%.*s' to the library", path.size, path.data);
		return false;
	}
	return app_open_library_game(game, false);
}

static b32 app_handle_actions(App_WindowOutput input)
{
	b32 step_requested = false;
	b32 emulator_state_changed = false;
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
			case APP_ACTION_OPEN_LIBRARY_GAME:
			{
				u32 index = action.open_library_game.index;
				App_Library *library = &app.library_store->library;
				if (index < library->game_count)
				{
					if (app_open_library_game(&library->games[index], true))
					{
						emulator_state_changed = true;
						step_requested = false;
					}
				}
				else LOG_ERROR("invalid library game index %u", index);
			} break;
			case APP_ACTION_RESET:
			{
				if (app_reset_emulator())
				{
					emulator_state_changed = true;
					step_requested = false;
				}
			} break;
			case APP_ACTION_SAVE_STATE: app_save_state(); break;
			case APP_ACTION_RESTORE_STATE:
			{
				if (app_restore_state())
				{
					emulator_state_changed = true;
					step_requested = false;
				}
			} break;
			case APP_ACTION_DUMP_PROGRAM:
			{
				app_rebuild_program_listing();
				if (program_dump(&app.program, debugger_program_path)) LOG_INFO("dumped program model to '%s'", debugger_program_path);
				else LOG_ERROR("failed to dump program model to '%s'", debugger_program_path);
			} break;
			case APP_ACTION_TOGGLE_RUNNING:
			{
				if (nes_emulator_ready_to_run(&app.debugger->emulator))
				{
					App_TransportState *state = app.transport.state == APP_TRANSPORT_SCRUBBING ? &app.transport.return_state : &app.transport.state;
					Assert(*state == APP_TRANSPORT_PAUSED || *state == APP_TRANSPORT_RUNNING);
					if (*state == APP_TRANSPORT_RUNNING) *state = APP_TRANSPORT_PAUSED;
					else
					{
						nes_process_clear_ram_evidence(app.debugger);
						*state = APP_TRANSPORT_RUNNING;
					}
					LOG_INFO(*state == APP_TRANSPORT_RUNNING ? "running realtime" : "paused");
				}
			} break;
			case APP_ACTION_STEP:
			{
				if (!emulator_state_changed) step_requested = true;
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
				if (app.ppu_volume_target > 0.f) app.ppu_volume_restore = app.ppu_volume_target;
			} break;
			case APP_ACTION_MUTE:
			{
				if (app.ppu_volume_target > 0.f)
				{
					app.ppu_volume_restore = app.ppu_volume_target;
					app.ppu_volume_target = 0.f;
				}
				else app.ppu_volume_target = Max(app.ppu_volume_restore, 0.1f);
			} break;
			case APP_ACTION_NONE: break;
			default: Assert(false);
		}
	}
	if (emulator_state_changed)
	{
		step_requested = false;
		scrub_input_active = false;
		app.transport = (App_Transport) { .state = APP_TRANSPORT_PAUSED };
	}

	scrub_direction = CLAMP(scrub_direction, -1, 1);
	b32 scrub_modifier_down = !!(input.modifiers & OS_MODIFIER_CONTROL);
	if (nes_emulator_ready_to_run(&app.debugger->emulator) && scrub_input_active)
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
		if (path.size && app_import_game(path)) step_requested = false;
	}

	return step_requested;
}

static void app_run_frame(void)
{
	u64 sample_capacity = nes_required_sample_capacity();
	if (!app.audio_backend_available)
	{
		NES_RunFrameResult frame = nes_process_run_frame(app.debugger, 0, 0);
		prof_add_metric(PROF_METRIC_AUDIO_SAMPLES_GENERATED, frame.samples);
		if (nes_process_hit_breakpoint(app.debugger))
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
		NES_RunFrameResult frame = nes_process_run_frame(app.debugger, samples, sample_capacity);
		if (nes_process_hit_breakpoint(app.debugger))
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

	if (nes_emulator_ready_to_run(&app.debugger->emulator))
	{
		if (app.transport.state != APP_TRANSPORT_SCRUBBING)
		{
			app_update_emulator_input(&input);

			if (step_requested) {
				app.transport.state = APP_TRANSPORT_PAUSED;
				program_invalidate(&app.program);
				PROF_BLOCK("emulation step") nes_process_step(app.debugger);
			}
			else if (app.transport.state == APP_TRANSPORT_RUNNING) {
				program_invalidate(&app.program);
				PROF_BLOCK("emulation") app_run_frame();
			}
		}
		else if (app.transport.direction == -1)
		{
			// TODO(RJ) this needs to be configurable, and also, we should just be able
			// to skip backwards arbitrarily instead of doing one step at a time
			for(u32 i=0;i<4;++i) {
				if(!nes_process_rewind(app.debugger)) break;
				program_invalidate(&app.program);
			}
		}
		else if (app.transport.direction == +1)
		{
			for(u32 i=0;i<4;++i) {
				if(!nes_process_replay(app.debugger)) break;
				program_invalidate(&app.program);
			}
		}

		if (app.transport.state != APP_TRANSPORT_RUNNING) app_rebuild_program_listing();
		PROF_BLOCK("execution activity")   execution_activity_update(&app.execution_activity, debugger_execution_graph(app.debugger), seconds_now().seconds);
		PROF_BLOCK("publish NES target")   nes_target_publish(&app.published, &app.debugger->emulator);
		PROF_BLOCK("upload video texture") app_upload_video_texture();
		PROF_BLOCK("upload CHR texture")   app_upload_chr_texture();

	}
	app_capture_ppu();
	if (app.transport.state != APP_TRANSPORT_RUNNING) audio_stream_discard(app.audio);
	app.ppu_volume += (app.ppu_volume_target - app.ppu_volume) * 0.35f;

	PROF_BLOCK("mix and output audio") app_mix_and_output_audio();

}

static void app_frame(void)
{
	arena_reset(&app.frame_arena);
	app.frame_index++;
	Assert(app.frame_index);
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
	if (!platform_create_directories("data\\library")) {
		LOG_ERROR("failed to create library storage directory 'data\\library'");
		return false;
	}

	if (!os_graphical_init()) {
		LOG_ERROR("failed to initialize graphics");
		return false;
	}

	app.arena = arena_create(0, "app arena");
	app.frame_arena = arena_create(0, "app frame arena");
	app.game_arena = arena_create(0, "app game arena");

	app.library_store = app_library_store_open(app_library_path);
	if (!app.library_store)
	{
		LOG_ERROR("failed to open game library '%s'", app_library_path);
		return false;
	}

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

	ttf_init_api();
	ByteSpan code_font_bytes = push_file(&app.arena, str_from_cstr(app_font_path));
	Assert(code_font_bytes.size <= MAX_VALUE_U32);
	Font_Handle code_font = ttf_load(str_from_data((char *)code_font_bytes.data, (u32)code_font_bytes.size));
	UI_Theme theme = ui_default_theme(code_font);

	app.renderer = gfx_create_renderer(&app.arena);
	app.text = text_create(&app.arena);
	app.text_gfx = text_gfx_create(&app.arena, app.renderer, app.text);
	app.debugger = nes_process_create(&app.arena);
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

	return true;
}

static void app_pace_frame(void)
{
	Seconds now = seconds_now();
	f64 elapsed = now.seconds - app.frame_begin.seconds;
	platform_sleep((u64)(Max(0.0, 1.0 / 60.0 - elapsed) * 1000.0));
	app.frame_begin = seconds_now();
}

static b32 app_save_state(void)
{
	if (!app.session.library_save) return true;
	Assert(app.session.library_game);
	Assert(nes_emulator_ready_to_run(&app.debugger->emulator));

	u64 now = Max(app_unix_time_ms(), Max(app.session.library_game->last_played_unix_ms, app.session.library_save->updated_unix_ms));
	u64 elapsed = app_play_time_ms();
	App_LibrarySave previous_save = *app.session.library_save;
	u64 previous_game_updated = app.session.library_game->last_played_unix_ms;
	u64 previous_game_play_time = app.session.library_game->play_time_ms;
	app.session.library_save->updated_unix_ms = now;
	app.session.library_save->play_time_ms += elapsed;
	app.session.library_game->last_played_unix_ms = now;
	app.session.library_game->play_time_ms += elapsed;
	App_Save *next_save_data = arena_push(&app.frame_arena, sizeof(*next_save_data));
	*next_save_data = app.session.save;
	next_save_data->state = app.debugger->emulator.state;
	if (!app_library_store_write_save(app.library_store, &app.frame_arena, app.session.library_save, next_save_data))
	{
		*app.session.library_save = previous_save;
		app.session.library_game->last_played_unix_ms = previous_game_updated;
		app.session.library_game->play_time_ms = previous_game_play_time;
		LOG_ERROR("failed to write active save '%.*s'", app.session.library_save->id.size, app.session.library_save->id.data);
		return false;
	}
	app.session.save = *next_save_data;
	app.play_time_seconds = 0;
	if (!app_library_store_write_manifest(app.library_store, &app.frame_arena))
	{
		LOG_ERROR("saved state but failed to update library metadata; it will be retried on the next save");
		return false;
	}
	LOG_INFO("saved '%.*s'", app.session.library_game->title.size, app.session.library_game->title.data);
	return true;
}

static void app_shutdown(void)
{
	app_save_state();
	app_save_user_config();
	app_library_store_close(app.library_store);
	arena_destroy(&app.game_arena);
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
