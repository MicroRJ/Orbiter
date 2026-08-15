#include "app_window.h"
#include "app.h"
#include "app_library_store.h"
#include "nes_process.h"
#include "gif_recorder.h"
#include "panels.h"
#include "text_gfx.h"
#include "ui_box.h"
#include "ui_widgets.h"
#include "views.h"
#include "elf.h"

enum { APP_WINDOW_ACTION_CAPACITY = 64 };

struct App_Window
{
	App *app;
	OS_Window *os;
	Input_State input;
	GFX_Window *gfx;
	Draw_Context *draw;
	UI_Context *ui;
	Panels *panels;
	GFX_Renderer *renderer;
	Text_GFX *text_gfx;
	App_Action output_actions[APP_WINDOW_ACTION_CAPACITY];
	App_Action pending_actions[APP_WINDOW_ACTION_CAPACITY];
	u32 output_action_count;
	u32 pending_action_count;
	b32 library_overlay_on;
	b32 exclusive_ppu;
	b32 crt_enabled;
	b32 ui_debug_bounds;
	b32 ui_frame_active;
	f32 volume_animation;
	f32 frames_per_second;
	Seconds previous_draw_time;
	GifRecorder capture;
	b32 screenshot_requested;
};

static void app_window_route_action(App_Window *window, App_Action action);

GFX_Texture *app_thumbnail_texture(App *app, const App_LibrarySave *save)
{
	Assert(app && save);
	App_ThumbnailCacheEntry *entry = 0;
	App_ThumbnailCacheEntry *available = 0;
	for (u32 index = 0; index < ArrayCount(app->thumbnail_cache.entries); index ++)
	{
		App_ThumbnailCacheEntry *candidate = &app->thumbnail_cache.entries[index];
		if (candidate->save == save)
		{
			entry = candidate;
			break;
		}
		if (!candidate->save)
		{
			if (!available) available = candidate;
			continue;
		}
		if (candidate->last_used_frame == app->frame_index) continue;
		if (!available || candidate->last_used_frame < available->last_used_frame) available = candidate;
	}

	if (entry && entry->updated_unix_ms == save->updated_unix_ms)
	{
		entry->last_used_frame = app->frame_index;
		return entry->texture;
	}
	if (!entry) entry = available;
	if (!entry) return 0;
	if (entry->texture) gfx_destroy_texture(entry->texture);
	*entry = (App_ThumbnailCacheEntry) {
		.save = save,
		.updated_unix_ms = save->updated_unix_ms,
		.last_used_frame = app->frame_index,
	};

	u64 arena_position = app->frame_arena.position;
	App_Save *data = arena_push_zero(&app->frame_arena, sizeof(*data));
	if (app_library_store_read_save(app->library_store, &app->frame_arena, save, data))
	{
		Color_RGBA8 *pixels = arena_push(&app->frame_arena, sizeof(*pixels) * NES_VIDEO_WIDTH * NES_VIDEO_HEIGHT);
		nes_target_colorize_pixels(pixels, &data->state.video[0][0], NES_VIDEO_WIDTH * NES_VIDEO_HEIGHT);
		entry->texture = gfx_create_texture_from_image(app->renderer, (Image_rgba_u8) {
			.reso = v2i(NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT),
			.elem_stride = NES_VIDEO_WIDTH,
			.data = (vec4_u8 *)pixels,
		}, GRAPHICS_SAMPLER_POINT);
	}
	app->frame_arena.position = arena_position;
	return entry->texture;
}

App_Window *app_window_create(Arena *owner, App *app, App_WindowDesc desc)
{
	Assert(owner);
	Assert(app);
	Assert(desc.title);
	Assert(app->renderer);
	Assert(app->text);
	Assert(app->text_gfx);

	App_Window *window = arena_push_zero(owner, sizeof(*window));
	window->app = app;
	window->renderer = app->renderer;
	window->text_gfx = app->text_gfx;
	window->os = os_window_create((OS_WindowDesc) {
		.title = desc.title,
		.title_bar = {
			.enabled = true,
			.dark = true,
			.background_rgb = 0x050A0C,
			.text_rgb = 0x718783,
			.border_rgb = 0x718783,
		},
	});
	Assert(window->os);
	window->gfx = gfx_create_window(owner, window->renderer, window->os);
	window->draw = draw_create(owner, window->renderer);
	window->ui = ui_create(owner, window->os, &window->input, app->text, window->draw, desc.theme);
	window->panels = panels_create(owner);
	window->library_overlay_on = true;
	window->crt_enabled = true;
	return window;
}

void app_window_destroy(App_Window *window)
{
	Assert(window);
	if (window->capture.recording) gif_recorder_end(&window->capture);
	for (u32 index = 0; index < ArrayCount(window->app->thumbnail_cache.entries); index ++) gfx_destroy_texture(window->app->thumbnail_cache.entries[index].texture);
	os_window_destroy(window->os);
}

b32 app_window_is_open(const App_Window *window)
{
	Assert(window);
	return os_window_is_open(window->os);
}

void app_window_set_library_visible(App_Window *window, b32 visible)
{
	Assert(window);
	window->library_overlay_on = visible;
}

void app_window_state_push(elf_State *state, const App_Window *window)
{
	Assert(state);
	Assert(window);
	i32 top = elf_get_top(state);
	elf_new_table(state);
	i32 table = elf_abs_index(state, -1);
	elf_push_int(state, 1);
	Assert(elf_set_field(state, table, "version"));
	panels_layout_push(state, window->panels);
	Assert(elf_set_field(state, table, "panels"));
	Assert(elf_get_top(state) == top + 1);
}

static b32 app_window_state_read_impl(elf_State *state, i32 index, App_Window *window)
{
	if (elf_type(state, index) != ELF_VALUE_TYPE_TABLE) return false;
	i32 table = elf_abs_index(state, index);
	elf_Integer version;
	if (!elf_get_field(state, table, "version")) return false;
	b32 valid = elf_to_int(state, -1, &version) && version == 1;
	Assert(elf_pop(state, 1));
	if (!valid || !elf_get_field(state, table, "panels")) return false;
	valid = panels_layout_read(state, -1, window->panels);
	Assert(elf_pop(state, 1));
	return valid;
}

b32 app_window_state_read(elf_State *state, i32 index, App_Window *window)
{
	Assert(state);
	Assert(window);
	i32 top = elf_get_top(state);
	b32 result = app_window_state_read_impl(state, index, window);
	Assert(elf_get_top(state) == top);
	return result;
}

void app_window_emit_action(App_Window *window, App_Action action)
{
	Assert(window);
	Assert(action.kind != APP_ACTION_NONE);
	Assert(window->pending_action_count < ArrayCount(window->pending_actions));
	window->pending_actions[window->pending_action_count++] = action;
}

static u32 app_window_modifiers(const App_Window *window)
{
	u32 modifiers = 0;
	if (window->input.keys[OS_Key_LeftShift] & INPUT_KEY_DOWN || window->input.keys[OS_Key_RightShift] & INPUT_KEY_DOWN) modifiers |= OS_MODIFIER_SHIFT;
	if (window->input.keys[OS_Key_LeftControl] & INPUT_KEY_DOWN || window->input.keys[OS_Key_RightControl] & INPUT_KEY_DOWN) modifiers |= OS_MODIFIER_CONTROL;
	if (window->input.keys[OS_Key_LeftAlt] & INPUT_KEY_DOWN || window->input.keys[OS_Key_RightAlt] & INPUT_KEY_DOWN) modifiers |= OS_MODIFIER_ALT;
	return modifiers;
}

static b32 app_window_handle_local_action(App_Window *window, App_Action action)
{
	switch (action.kind)
	{
		case APP_ACTION_TOGGLE_LIBRARY_OVERLAY:
		{
			window->library_overlay_on = !window->library_overlay_on;
		} break;
		case APP_ACTION_SPLIT_PANEL:
		{
			Assert(window->panels->focused);
			panel_split(window->panels, window->panels->focused, action.split_panel.axis, 0.5f);
		} break;
		case APP_ACTION_CLOSE_PANEL:
		{
			Assert(window->panels->focused);
			panel_close(window->panels, window->panels->focused);
		} break;
		case APP_ACTION_OPEN_VIEW:
		{
			if (action.open_view.index < view_desc_count) panel_open_view(window->panels, window->panels->focused, &view_descs[action.open_view.index]);
		} break;
		case APP_ACTION_TOGGLE_FULLSCREEN:
		{
			os_window_set_fullscreen(window->os, !os_window_is_fullscreen(window->os));
		} break;
		case APP_ACTION_TOGGLE_PPU_FULLSCREEN:
		{
			window->exclusive_ppu = !window->exclusive_ppu;
			os_window_set_fullscreen(window->os, window->exclusive_ppu);
		} break;
		case APP_ACTION_EXIT_PPU_FULLSCREEN:
		{
			if (window->exclusive_ppu)
			{
				window->exclusive_ppu = false;
				os_window_set_fullscreen(window->os, false);
			}
		} break;
		case APP_ACTION_TAKE_APP_SCREENSHOT:
		{
			window->screenshot_requested = true;
		} break;
		case APP_ACTION_TOGGLE_APP_CAPTURE:
		{
			if (window->capture.recording) gif_recorder_end(&window->capture);
			else if (!gif_recorder_begin(&window->capture, window->os->size, "orbiter_capture")) LOG_ERROR("failed to begin application GIF capture");
		} break;
		case APP_ACTION_TOGGLE_CRT:
		{
			window->crt_enabled = !window->crt_enabled;
		} break;
		case APP_ACTION_TOGGLE_UI_DEBUG_BOUNDS:
		{
			window->ui_debug_bounds = !window->ui_debug_bounds;
			LOG_INFO("UI debug bounds %s", window->ui_debug_bounds ? "enabled" : "disabled");
		} break;
		case APP_ACTION_ADJUST_UI_FONT_SIZE:
		{
			i32 font_size = CLAMP(window->ui->theme.code.size + action.ui_font.pixels, UI_CODE_FONT_SIZE_MIN, UI_CODE_FONT_SIZE_MAX);
			if (font_size != window->ui->theme.code.size)
			{
				window->ui->theme.code.size = font_size;
				LOG_INFO("UI font size %d px", font_size);
			}
		} break;
		case APP_ACTION_RESET_UI_FONT_SIZE:
		{
			window->ui->theme.code.size = UI_CODE_FONT_SIZE_DEFAULT;
			LOG_INFO("UI font size %d px", UI_CODE_FONT_SIZE_DEFAULT);
		} break;
		default: return false;
	}
	return true;
}

static void app_window_route_action(App_Window *window, App_Action action)
{
	if (action.kind == APP_ACTION_NONE) return;
	if (action.kind == APP_ACTION_ADJUST_VOLUME || action.kind == APP_ACTION_MUTE) window->volume_animation = 1.f;
	if (app_window_handle_local_action(window, action)) return;
	Assert(window->output_action_count < ArrayCount(window->output_actions));
	window->output_actions[window->output_action_count++] = action;
}

static App_GameInput app_window_keyboard_input(const App_Window *window, u32 player)
{
	if (player != 0) return 0;
	App_GameInput input = 0;
	if (window->input.keys[OS_Key_Up] & INPUT_KEY_DOWN) input |= APP_GAME_INPUT_UP;
	if (window->input.keys[OS_Key_Down] & INPUT_KEY_DOWN) input |= APP_GAME_INPUT_DOWN;
	if (window->input.keys[OS_Key_Left] & INPUT_KEY_DOWN) input |= APP_GAME_INPUT_LEFT;
	if (window->input.keys[OS_Key_Right] & INPUT_KEY_DOWN) input |= APP_GAME_INPUT_RIGHT;
	if (window->input.keys[OS_Key_W] & INPUT_KEY_DOWN) input |= APP_GAME_INPUT_UP;
	if (window->input.keys[OS_Key_S] & INPUT_KEY_DOWN) input |= APP_GAME_INPUT_DOWN;
	if (window->input.keys[OS_Key_A] & INPUT_KEY_DOWN) input |= APP_GAME_INPUT_LEFT;
	if (window->input.keys[OS_Key_D] & INPUT_KEY_DOWN) input |= APP_GAME_INPUT_RIGHT;
	if (window->input.keys[OS_Key_Z] & INPUT_KEY_DOWN) input |= APP_GAME_INPUT_A;
	if (window->input.keys[OS_Key_X] & INPUT_KEY_DOWN) input |= APP_GAME_INPUT_B;
	if (window->input.keys[OS_Key_C] & INPUT_KEY_DOWN) input |= APP_GAME_INPUT_START;
	if (window->input.keys[OS_Key_V] & INPUT_KEY_DOWN) input |= APP_GAME_INPUT_SELECT;
	return input;
}

App_WindowOutput app_window_begin_frame(App_Window *window, App_KeyMap key_map)
{
	Assert(window);
	Assert(!key_map.count || key_map.bindings);
	Assert(!window->ui_frame_active);

	App_WindowOutput result = { .feedback = ui_feedback_take(window->ui) };
	input_state_update(&window->input, window->os);
	if (!(window->os->status & OS_WINDOW_MINIMIZED) && window->os->size.x > 1 && window->os->size.y > 1)
	{
		ui_begin_frame(window->ui);
		window->ui_frame_active = true;
	}
	window->output_action_count = 0;
	for (u32 index = 0; index < window->pending_action_count; index++) app_window_route_action(window, window->pending_actions[index]);
	window->pending_action_count = 0;

	for (u32 event_index = 0; event_index < os_window_event_count(window->os); event_index++)
	{
		const OS_Event *event = os_window_event(window->os, event_index);
		App_KeyChordActivation activation;
		if (event->type == OS_EVENT_KEY_PRESS) activation = APP_KEY_CHORD_ON_PRESS;
		else if (event->type == OS_EVENT_KEY_RELEASE) activation = APP_KEY_CHORD_ON_RELEASE;
		else continue;

		for (u32 bind_index = 0; bind_index < key_map.count; bind_index++)
		{
			const App_KeyBinding *binding = &key_map.bindings[bind_index];
			if (binding->key_chord.activation != activation || binding->key_chord.key != event->key || binding->key_chord.modifiers != event->modifiers) continue;
			if (event->repeat && !binding->allow_repeat) continue;
			app_window_route_action(window, binding->action);
		}
	}

	u32 modifiers = app_window_modifiers(window);
	result.modifiers = modifiers;
	for (u32 bind_index = 0; bind_index < key_map.count; bind_index++)
	{
		const App_KeyBinding *binding = &key_map.bindings[bind_index];
		if (binding->key_chord.activation != APP_KEY_CHORD_WHILE_DOWN || binding->key_chord.modifiers != modifiers) continue;
		if (window->input.keys[binding->key_chord.key] & INPUT_KEY_DOWN) app_window_route_action(window, binding->action);
	}

	result.actions = window->output_actions;
	result.action_count = window->output_action_count;
	result.keyboard_captured = !!(modifiers & (OS_MODIFIER_CONTROL | OS_MODIFIER_ALT));
	if (!result.keyboard_captured)
	{
		for (u32 player = 0; player < ArrayCount(result.keyboard_input); player++) result.keyboard_input[player] = app_window_keyboard_input(window, player);
	}
	return result;
}

static void app_resize_graphics_outputs(App_Window *window, vec2i size)
{
	Assert(size.x > 1 && size.y > 1);
	gfx_resize_window(window->gfx, size);
}

// Render passes

// TODO(RJ) we need to free intermediate textures!
static GFX_Texture *app_acquire_pass_output(App_Window *window, vec2i size, GFX_Sampler sampler, const char *label)
{
	return gfx_acquire_transient_texture(window->renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_RARE_UPDATES,
		.bind_flags = GFX_TEXTURE_BIND_INPUT | GFX_TEXTURE_BIND_OUTPUT,
		.format = GRAPHICS_FORMAT_RGBA_F32,
		.size = size,
		.sampler = sampler,
		.label = label,
	});
}

static GFX_Texture *app_acquire_hdr_pass_output(App_Window *window, GFX_Texture *input, const char *label)
{
	return app_acquire_pass_output(window, gfx_texture_size(input), GRAPHICS_SAMPLER_POINT, label);
}

static GFX_Texture *app_crt_barrel_pass(App_Window *window, GFX_Texture *input)
{
	GFX_Texture *output = app_acquire_hdr_pass_output(window, input, "barrel pass output");
	draw_begin_pass(window->draw, (GFX_PassDesc) { .output = output, .clear = true, .clear_color = COLOR_BLACK });
	draw_barrel(window->draw, (Draw_BarrelParams) { .texture = input, .strength = 1.f });
	draw_end_pass(window->draw);
	return output;
}

static GFX_Texture *app_rewind_pass(App_Window *window, GFX_Texture *input)
{
	GFX_Texture *output = app_acquire_hdr_pass_output(window, input, "rewind pass output");
	draw_begin_pass(window->draw, (GFX_PassDesc) { .output = output, .clear = true, .clear_color = COLOR_BLACK });
	draw_rewind(window->draw, (Draw_RewindParams) { .texture = input, .time = (f32)fmod(seconds_now().seconds, 1024.0), .strength = 1.f });
	draw_end_pass(window->draw);
	return output;
}

static GFX_Texture *app_crt_scanlines_pass(App_Window *window, GFX_Texture *input)
{
	GFX_Texture *output = app_acquire_hdr_pass_output(window, input, "scanlines pass output");
	draw_begin_pass(window->draw, (GFX_PassDesc) { .output = output, .clear = true, .clear_color = COLOR_BLACK });
	draw_crt_scanlines(window->draw, input);
	draw_end_pass(window->draw);
	return output;
}

static void app_draw_exclusive_ppu(App_Window *window, GFX_Texture *frame_texture, rect_f32 window_rect)
{
	vec2 presentation_size = v2(4.f, 3.f);
	f32 scale = Min(window_rect.w / presentation_size.x, window_rect.h / presentation_size.y);
	rect_f32 video_rect = rect_f32_align(window_rect, v2(presentation_size.x * scale, presentation_size.y * scale), v2(0.5f, 0.5f));
	video_rect = rect_f32_round_out(video_rect);

	GFX_Texture *video_texture = window->crt_enabled ? app_crt_scanlines_pass(window, window->app->video_texture) : window->app->video_texture;
	draw_begin_pass(window->draw, (GFX_PassDesc) {
		.output = frame_texture,
		.clear = true,
		.clear_color = COLOR_BLACK,
	});
	draw_rect(window->draw, (Draw_RectParams) {
		.rect = video_rect,
		.texture = video_texture,
		.texture_region = { 0, 0, NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT },
		.color = COLOR_WHITE,
		.sampler = GRAPHICS_SAMPLER_POINT,
	});
	draw_end_pass(window->draw);
}

static void app_draw_box_tree(UI_Box *box)
{
	ui_box_paint(box);
	for (UI_Box *child = box->first; child; child = child->next) {
		app_draw_box_tree(child);
	}
}

static void app_draw_ui_debug_bounds(App_Window *window, UI_Box *root, rect_f32 window_rect)
{
	if (!window->ui_debug_bounds) return;
	UI_Context *ui = window->ui;
	UI_Box *box = ui_box_find_deepest(root, ui->mouse);
	if (!box) return;

	rect_f32 hit_rect = box->state ? box->state->hit_rect : rect_f32_intersect(box->rect, box->clip_rect);
	Color_SRGBA box_color = ui->theme.palette.violet;
	Color_SRGBA hit_color = ui->theme.palette.cyan;
	UI_TextStyle label_style = ui->theme.code;
	label_style.size = 14;
	label_style.color = COLOR_WHITE;
	label_style.align = v2(0.f, 0.5f);
	Str label = str_push_copy_f(&ui->frame_arena,
		"%.*s  rect %.0f,%.0f %.0fx%.0f  hit %.0f,%.0f %.0fx%.0f  z %d%s%s",
		(i32)box->name.size, box->name.data,
		box->rect.x, box->rect.y, box->rect.w, box->rect.h,
		hit_rect.x, hit_rect.y, hit_rect.w, hit_rect.h,
		box->paint.z,
		box->hit_intercept ? "  interactive" : "",
		box->hit_passthrough ? "  passthrough" : "");
	vec2 label_size = ui_measure_text(ui, label_style, label);
	rect_f32 label_rect = {
		.x = ui->mouse.x + 12.f,
		.y = ui->mouse.y + 16.f,
		.w = label_size.x + 12.f,
		.h = label_size.y + 8.f,
	};
	if (label_rect.x + label_rect.w > window_rect.x + window_rect.w) label_rect.x = window_rect.x + window_rect.w - label_rect.w - 4.f;
	if (label_rect.y + label_rect.h > window_rect.y + window_rect.h) label_rect.y = ui->mouse.y - label_rect.h - 12.f;
	label_rect.x = Max(label_rect.x, window_rect.x + 4.f);
	label_rect.y = Max(label_rect.y, window_rect.y + 4.f);

	ui_push_z(ui, UI_Z_OVERLAY + 1000);
	ui_push_unclipped(ui);
	ui_draw_rect(ui, (Draw_RectParams) { .rect = hit_rect, .color = color_with_alpha(hit_color, 0.12f) });
	ui_draw_rect_outline(ui, box->rect, 2.f, box_color);
	ui_draw_rect_outline(ui, hit_rect, 1.f, hit_color);
	ui_draw_rect(ui, (Draw_RectParams) { .rect = label_rect, .color = color_with_alpha(COLOR_BLACK, 0.92f), .corner_radii = draw_corner_radii_all(3.f) });
	ui_draw_rect_outline(ui, label_rect, 1.f, hit_color);
	ui_draw_text(ui, (rect_f32) { label_rect.x + 6.f, label_rect.y + 4.f, label_size.x, label_size.y }, label_style, label);
	ui_pop_unclipped(ui);
	ui_pop_z(ui);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct
{
	Str title;
	u32 mapper;
	u32 prg_size;
	u32 chr_size;
	u64 play_time_ms;
	u32 save_count;
	GFX_Texture *thumbnail;
}
LibraryCardModel;

typedef struct
{
	b32 activated;
}
LibraryCardOutput;

static LibraryCardOutput library_card_component(UI_Context *ui, UI_Key key, const LibraryCardModel *model)
{
	LibraryCardOutput output = {};
	UI_Palette *palette = &ui->theme.palette;

	ui_clean(ui);
	ui_axis(ui, AXIS_X);
	ui_size(ui, AXIS_X, ui_fill());
	ui_size(ui, AXIS_Y, ui_fixed(132.f));
	ui_padd(ui, AXIS_X, 12.f, 18.f);
	ui_padd(ui, AXIS_Y, 12.f, 12.f);
	ui_gap(ui, 18.f);
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_CLIP);
	ui_background(ui, palette->raised);
	ui_border(ui, palette->divider, 1.f);
	ui_roundness(ui, 8.f);
	UI_Box *card = ui_begin_horz(ui, key);
	UI_Response response = ui_signal_from_box(card);
	if (response.hovered)
	{
		card->paint.background = color_srgba_mix(palette->raised, palette->cyan, 0.12f);
		card->paint.border = palette->cyan;
	}
	if (response.held) card->paint.background = color_srgba_mix(palette->raised, palette->teal, 0.25f);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_fixed(160.f));
	ui_size(ui, AXIS_Y, ui_fill());
	ui_background(ui, palette->panel);
	ui_roundness(ui, 5.f);
	ui_image_box(ui, 1, (UI_ImageStyle) { .fit = UI_IMAGE_FIT_COVER, .align = v2(0.5f, 0.5f), .tint = model->thumbnail ? COLOR_WHITE : color_srgba_mix(palette->panel, palette->violet, 0.35f) }, model->thumbnail);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_fill());
	ui_size(ui, AXIS_Y, ui_fill());
	ui_gap(ui, 5.f);
	ui_begin_vert(ui, 2);
	{
		UI_TextStyle title_style = ui->theme.code;
		title_style.size = 28;
		title_style.align = v2(0.f, 0.5f);
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fill());
		ui_size(ui, AXIS_Y, ui_wrap());
		ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
		ui_text(ui, 1, title_style, model->title);

		UI_TextStyle detail_style = ui->theme.code;
		detail_style.size = 17;
		detail_style.align = v2(0.f, 0.5f);
		detail_style.color = palette->text_muted;
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fill());
		ui_size(ui, AXIS_Y, ui_wrap());
		ui_text_box(ui, 2, detail_style, "MAPPER %u  |  PRG %u KiB  |  CHR %u KiB", model->mapper, model->prg_size / 1024, model->chr_size / 1024);

		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fill());
		ui_size(ui, AXIS_Y, ui_wrap());
		ui_text_box(ui, 3, detail_style, "%lluh %02llum played  |  %u save%s", model->play_time_ms / (60 * 60 * 1000), model->play_time_ms / (60 * 1000) % 60, model->save_count, model->save_count == 1 ? "" : "s");
	}
	ui_box_end(ui);
	ui_box_end(ui);

	if (response.pressed)
	{
		ui_feedback_emit(ui, UI_FEEDBACK_PRESS);
		output.activated = true;
	}
	return output;
}

static void app_build_library_item(UI_Context *ui, u32 index, void *user)
{
	App_Window *window = user;
	App_Library *library = &window->app->library_store->library;
	Assert(index < library->game_count);
	const App_LibraryGame *game = &library->games[index];
	LibraryCardModel model = {
		.title = game->title,
		.mapper = game->cartridge.metadata.mapper,
		.prg_size = game->cartridge.metadata.prg_rom_size,
		.chr_size = game->cartridge.metadata.chr_rom_size,
		.play_time_ms = game->play_time_ms,
		.save_count = game->save_count,
	};
	for (u32 save_index = 0; save_index < game->save_count; save_index ++)
	{
		const App_LibrarySave *save = &game->saves[save_index];
		if (save->kind == APP_LIBRARY_SAVE_RESUME)
		{
			model.thumbnail = app_thumbnail_texture(window->app, save);
			break;
		}
	}
	LibraryCardOutput output = library_card_component(ui, UI_KEY("library card"), &model);
	if (output.activated) app_window_emit_action(window, (App_Action) { .kind = APP_ACTION_OPEN_LIBRARY_GAME, .open_library_game = { index } });
}

static void library_build_ui(App_Window *window)
{
	UI_Context *ui = window->ui;
	App_Library *library = &window->app->library_store->library;
	if (!library->game_count)
	{
		ui_clean(ui);
		ui_axis(ui, AXIS_Y);
		ui_size(ui, AXIS_X, ui_fill());
		ui_size(ui, AXIS_Y, ui_fill());
		ui_padd(ui, AXIS_X, 48.f, 48.f);
		ui_padd(ui, AXIS_Y, 48.f, 48.f);
		ui_gap(ui, 12.f);
		ui_begin_vert(ui, UI_KEY("empty library"));

		UI_TextStyle title_style = ui->theme.code;
		title_style.size = 28;
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_wrap());
		ui_size(ui, AXIS_Y, ui_wrap());
		ui_text(ui, UI_KEY("empty library title"), title_style, LIT("YOUR LIBRARY IS EMPTY"));

		UI_TextStyle detail_style = ui->theme.code;
		detail_style.size = 17;
		detail_style.color = ui->theme.palette.text_muted;
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_wrap());
		ui_size(ui, AXIS_Y, ui_wrap());
		ui_text(ui, UI_KEY("empty library detail"), detail_style, LIT("Import an iNES ROM to get started."));

		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_wrap());
		ui_size(ui, AXIS_Y, ui_wrap());
		if (ui_button(ui, UI_KEY("empty library import"), LIT("IMPORT ROM  CTRL+O")).pressed) app_window_emit_action(window, (App_Action) { .kind = APP_ACTION_OPEN_ROM });
		ui_box_end(ui);
		return;
	}

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_fill());
	ui_size(ui, AXIS_Y, ui_fill());
	UI_ScrollBox *scroll = ui_scroll_box_begin(ui, UI_KEY("library vertical scroll"), AXIS_Y);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_padd(ui, AXIS_X, 32.f, 32.f);
	ui_padd(ui, AXIS_Y, 32.f, 32.f);
	ui_begin_vert(ui, 1);

	UI_TextStyle style = ui->theme.code;
	style.size = 48;
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_wrap());
	ui_size(ui, AXIS_Y, ui_wrap());
	ui_text(ui, UI_KEY("your_library"), style, LIT("Your Library"));

	ui_clean(ui);
	ui_axis(ui, AXIS_Y);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_wrap());
	ui_gap(ui, 12.f);
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_virtual_list(ui, UI_KEY("library games"), LIT("library games"), (UI_VirtualListDesc) {
		.item_count = library->game_count,
		.user = window,
		.build_item = app_build_library_item,
	});
	ui_box_end(ui);

	ui_scroll_box_end(scroll);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

static UI_Box *app_status_text(UI_Context *ui, u64 key, Str text, UI_TextStyle style, f32 emission)
{
	ui_emission(ui, emission);
	UI_Box *box = ui_text(ui, key, style, text);
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

static UI_Box *build_main_ui(App_Window *window, rect_f32 window_rect, ViewFrameData *view_frame)
{
	App *app = window->app;
	UI_Context *ui = window->ui;
	Assert(app);
	Assert(view_frame);

	UI_BoxDesc root_desc = ui_defaults();
	root_desc.axis = AXIS_Y;
	root_desc.size[AXIS_X] = ui_grow(1.f);
	root_desc.size[AXIS_Y] = ui_grow(1.f);
	UI_Box *root = ui_build_begin(ui, UI_KEY("application shell"), LIT("application shell"), root_desc);

	f32 status_height = ui->theme.code.size + 8.f;
	UI_TextStyle style = ui->theme.code;
	style.align.y = 0.5f;

	app_status_bar_begin(ui, 1, LIT("top status"), status_height);
	app_status_row_begin(ui, 1, LIT("top status row"));

	style.color = ui->theme.palette.cyan;
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_wrap());
	ui_size(ui, AXIS_Y, ui_wrap());
	app_status_text(ui, 1, LIT("ORBITER"), style, ui->theme.palette.emission_medium);

	style.color = ui->theme.text_subtle;
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_flex(0.f, 1.f));
	ui_size(ui, AXIS_Y, ui_wrap());
	app_status_text(ui, 2, LIT("  |  github.com/MicroRJ  |  "), style, 0.f);

	f32 pulse = 0.5f + 0.5f * sinf((f32)seconds_now().seconds * 3.f * 4);

	if (!nes_emulator_ready_to_run(&app->debugger->emulator))
	{
		style.color = ui->theme.palette.amber;
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_flex(0.f, 1.f));
		ui_size(ui, AXIS_Y, ui_wrap());
		app_status_text(ui, 3, LIT("* INSERT CARTRIDGE *"), style, ui->theme.palette.emission_high * pulse);
	}
	else if (app->transport.state == APP_TRANSPORT_SCRUBBING && app->transport.direction == -1)
	{
		style.color = ui->theme.palette.error;
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_flex(0.f, 1.f));
		ui_size(ui, AXIS_Y, ui_wrap());
		u64 rewind_marker;
		u64 replay_marker;
		u64 rewind_cursor;
		debugger_get_rewind_markers(window->app->debugger, &rewind_marker, &rewind_cursor, &replay_marker);

		Str str = str_push_copy_f(&ui->frame_arena, "<< REWINDING [%llu, %llu, %llu]", rewind_marker, rewind_cursor, replay_marker);
		app_status_text(ui, 3, str, style, ui->theme.palette.emission_high * pulse);
	}
	else if (app->transport.state == APP_TRANSPORT_SCRUBBING && app->transport.direction == +1)
	{
		style.color = ui->theme.palette.amber;
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_flex(0.f, 1.f));
		ui_size(ui, AXIS_Y, ui_wrap());
		app_status_text(ui, 3, LIT("REPLAYING >>"), style, ui->theme.palette.emission_high * pulse);
	}
	else if (app->transport.state == APP_TRANSPORT_SCRUBBING)
	{
		style.color = ui->theme.palette.error;
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_flex(0.f, 1.f));
		ui_size(ui, AXIS_Y, ui_wrap());
		app_status_text(ui, 3, LIT("<< REWINDING >>"), style, ui->theme.palette.emission_high);
	}
	else
	{
		if (app->transport.state == APP_TRANSPORT_RUNNING)
		{
			style.color = ui->theme.palette.amber;
			ui_clean(ui);
			ui_size(ui, AXIS_X, ui_flex(0.f, 1.f));
			ui_size(ui, AXIS_Y, ui_wrap());
			app_status_text(ui, 3, LIT("RUNNING"), style, ui->theme.palette.emission_medium);
		}
		else
		{
			style.color = ui->theme.palette.error;
			ui_clean(ui);
			ui_size(ui, AXIS_X, ui_flex(0.f, 1.f));
			ui_size(ui, AXIS_Y, ui_wrap());
			app_status_text(ui, 3, LIT("PAUSED"), style, 0.06f + pulse * 0.16f);
		}
	}

	style.color = ui->theme.text_subtle;
	if (window->capture.recording)
	{
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_flex(0.f, 1.f));
		ui_size(ui, AXIS_Y, ui_wrap());
		app_status_text(ui, 4, LIT("   REC APP"), style, 0.f);
	}
	if (app->ppu_gif.recording)
	{
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_flex(0.f, 1.f));
		ui_size(ui, AXIS_Y, ui_wrap());
		app_status_text(ui, 4, LIT("   REC PPU"), style, 0.f);
	}

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_wrap());
	ui_box_make(ui, 6, LIT("top spacer"));

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_flex(0.f, 1.f));
	ui_size(ui, AXIS_Y, ui_wrap());
	style.align.x = 0.f;

	Color_SRGBA ppu_volume_base_color = ui->theme.text_subtle;
	if (app->ppu_volume <= 0.01f) {
		ppu_volume_base_color = ui->theme.palette.error;
	}
	style.color = color_srgba_mix(ppu_volume_base_color, ui->theme.palette.amber, window->volume_animation);
	ui_emission(ui, ui->theme.palette.emission_high * window->volume_animation);
	UI_Box *volume_box = ui_text_sized_f(ui, UI_KEY("volume"), style, LIT("VOL 100%"), "VOL %i%%", (i32) roundf(app->ppu_volume * 100.f));
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
		ui_text(ui, UI_KEY("1"), style, str_push_copy_f(&ui->frame_arena, "Ctrl+Up Raise Volume"));
		ui_clean(ui);
		ui_text(ui, UI_KEY("2"), style, str_push_copy_f(&ui->frame_arena, "Ctrl+Down Lower Volume"));
		ui_box_end(ui);

		ui_tooltip_end(ui);
	}
	window->volume_animation *= 0.95f;

	style.color = ui->theme.text_subtle;
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_flex(0.f, 1.f));
	ui_size(ui, AXIS_Y, ui_wrap());
	ui_text_sized_f(ui, UI_KEY("fps"), style, LIT("FPS 999.9"), "FPS %02.2f", window->frames_per_second);

	style.color = ui->theme.text_subtle;
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_flex(0.f, 1.f));
	ui_size(ui, AXIS_Y, ui_wrap());
	ui_text_sized_f(ui, UI_KEY("frame"), style, LIT("FRAME 999999999"), " FRAME %llu", app->published.generation);

	ui_box_end(ui);
	app_status_divider(ui, 2, false);
	ui_box_end(ui);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_layout(ui, &UI_FlatLayoutHooks);
	ui_box_begin(ui, 2, LIT("belly"));
	{
		rect_f32 panel_rect = window_rect;
		panel_rect.y += status_height;
		panel_rect.h = Max(0.f, panel_rect.h - status_height * 2.f);
		panels_build_ui(window->panels, window->os, view_frame, panel_rect);

		if (window->library_overlay_on)
		{
			ui_push_box_z(ui, UI_Z_OVERLAY);

			ui_clean(ui);
			ui_size(ui, AXIS_X, ui_fixed(window_rect.w * 0.45f));
			ui_size(ui, AXIS_Y, ui_grow(1.f));
			ui_backdrop(ui, 0.f);
			ui_background(ui, (Color_SRGBA){0,0,0,0.2});
			UI_Box *overlay_root = ui_box_begin(ui, UI_KEY("overlay"), LIT(""));
			overlay_root->hit_intercept = true;

			library_build_ui(window);

			ui_box_end(ui);

			ui_pop_box_z(ui);
		}

	}
	ui_box_end(ui);

	app_status_bar_begin(ui, 3, LIT("bottom status"), status_height);
	app_status_divider(ui, 1, true);
	app_status_row_begin(ui, 2, LIT("bottom status row"));

	Str bottom_left = app->session.game ? app->session.game->title : LIT("NO CARTRIDGE");
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_flex(0.f, 1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	style.align.x = 0.f;
	app_status_text(ui, 1, bottom_left, style, 0.f);
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_box_make(ui, 2, LIT("bottom spacer"));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_flex(0.f, 3.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	style.align.x = 1.f;
	app_status_text(ui, 3, LIT("F PPU   F5 RUN   F7 CRT   F8 PPU PNG / SHIFT GIF   F9 APP PNG / SHIFT GIF   F10 STEP   F11 FULLSCREEN"), style, 0.f);
	ui_box_end(ui);
	ui_box_end(ui);

	ui_build_end(ui);
	PROF_BLOCK("ui measure") ui_box_measure(root, (UI_BoxConstraints) { .min = window_rect.size, .max = window_rect.size });
	PROF_BLOCK("ui layout") ui_box_layout(root, window_rect);
	return root;
}


static void app_window_update_fps(App_Window *window)
{
	Seconds now = seconds_now();
	if (window->previous_draw_time.seconds > 0.0)
	{
		f64 elapsed = now.seconds - window->previous_draw_time.seconds;
		if (elapsed > 0.0)
		{
			f32 measured = (f32)(1.0 / elapsed);
			window->frames_per_second = window->frames_per_second > 0.f ? window->frames_per_second * 0.9f + measured * 0.1f : measured;
		}
	}
	window->previous_draw_time = now;
}

static u8 app_window_linear_to_srgb_u8(f32 value)
{
	value = CLAMP(value, 0.f, 1.f);
	f32 srgb = value <= 0.0031308f ? value * 12.92f : 1.055f * powf(value, 1.f / 2.4f) - 0.055f;
	return (u8)(srgb * 255.f + 0.5f);
}

static void app_window_capture_frame(App_Window *window, GFX_Texture *texture)
{
	enum { GIF_CAPTURE_MAX_FRAMES = 60 * 30 };
	b32 screenshot_requested = window->screenshot_requested;
	window->screenshot_requested = false;
	if (!screenshot_requested && !window->capture.recording) return;

	vec2i size = gfx_texture_size(texture);
	if (window->capture.recording && (size.x != window->capture.size.x || size.y != window->capture.size.y))
	{
		LOG_WARN("ending application GIF because the window size changed");
		gif_recorder_end(&window->capture);
	}
	if (!screenshot_requested && !window->capture.recording) return;
	u32 pixel_count = size.x * size.y;
	vec4 *linear = arena_push(&window->ui->frame_arena, pixel_count * sizeof(*linear));
	Color_RGBA8 *pixels = arena_push(&window->ui->frame_arena, pixel_count * sizeof(*pixels));
	if (!gfx_read_texture(texture, linear, size.x * sizeof(*linear)))
	{
		LOG_ERROR("application capture texture readback failed");
		if (window->capture.recording) gif_recorder_end(&window->capture);
		return;
	}
	for (u32 index = 0; index < pixel_count; index++)
	{
		pixels[index] = (Color_RGBA8) {
			app_window_linear_to_srgb_u8(linear[index].x),
			app_window_linear_to_srgb_u8(linear[index].y),
			app_window_linear_to_srgb_u8(linear[index].z),
			(u8)(CLAMP(linear[index].w, 0.f, 1.f) * 255.f + 0.5f),
		};
	}
	if (screenshot_requested && !screenshot_write_png(pixels, size, size.x * sizeof(*pixels), "orbiter_screenshot")) LOG_ERROR("failed to save application screenshot");
	if (window->capture.recording && !gif_recorder_frame(&window->capture, pixels, size.x * sizeof(*pixels)))
	{
		LOG_ERROR("application GIF capture failed");
		gif_recorder_end(&window->capture);
		return;
	}
	if (window->capture.frame_count >= GIF_CAPTURE_MAX_FRAMES) gif_recorder_end(&window->capture);
}

void app_window_render(App_Window *window)
{
	Assert(window);
	if (!window->ui_frame_active)
	{
		window->previous_draw_time = seconds_now();
		return;
	}
	App *app = window->app;
	Assert(app);
	Assert(app->debugger);
	Assert(app->video_texture);
	Assert(app->chr_texture);

	rect_f32 window_rect = rect_f32_from_size(v2_from_v2i(window->os->size));
	app_window_update_fps(window);
	app_resize_graphics_outputs(window, window->os->size);
	draw_begin_frame(window->draw);
	os_window_set_cursor(window->os, OS_CURSOR_POINTER);

	GFX_Texture *frame_texture = app_acquire_pass_output(window, window->os->size, GRAPHICS_SAMPLER_POINT, "application frame");

	if (window->exclusive_ppu) {
		app_draw_exclusive_ppu(window, frame_texture, window_rect);
	}
	else {
		ViewFrameData view_frame = {
			.window = window,
			.emulator = &app->debugger->emulator,
			.debugger = app->debugger,
			.program = &app->program,
			.execution_graph = debugger_execution_graph(app->debugger),
			.execution_activity = &app->execution_activity,
			.publication = &app->published,
			.video_texture = app->video_texture,
			.chr_texture = app->chr_texture,
			.ui = window->ui,
			.scratch = &window->ui->frame_arena,
		};
		UI_Box *shell = build_main_ui(window, window_rect, &view_frame);
		draw_begin_pass(window->draw, (GFX_PassDesc) {
			.output = frame_texture,
			.clear = true,
			.clear_color = COLOR_BLACK,
		});
		draw_rect(window->draw, (Draw_RectParams) {
			.rect = window_rect,
			.color = window->ui->theme.background,
		});
		app_draw_box_tree(shell);
		app_draw_ui_debug_bounds(window, shell, window_rect);
		draw_end_pass(window->draw);
		draw_compose(window->draw, window->text_gfx, frame_texture, window_rect);
	}


	GFX_Texture *present_texture = frame_texture;
	if (app->transport.state == APP_TRANSPORT_SCRUBBING) present_texture = app_rewind_pass(window, present_texture);
	if (window->crt_enabled) present_texture = app_crt_barrel_pass(window, present_texture);

	draw_begin_pass(window->draw, (GFX_PassDesc) {
		.output = gfx_window_texture(window->gfx),
	});
	draw_blit(window->draw, present_texture);
	draw_end_pass(window->draw);

	text_gfx_sync(window->text_gfx);

	draw_end_frame(window->draw);
	PROF_BLOCK("app capture") app_window_capture_frame(window, present_texture);

	PROF_BLOCK("present wait") gfx_present_window(window->gfx);

	ui_end_frame(window->ui);
	window->ui_frame_active = false;
}
