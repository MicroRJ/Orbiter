#include "base.h"
#include "graphics.h"
#include "ttf_api.h"
#include "text.h"
#include "os_graphical.h"
#include "ui.h"
#include "ui_box.h"

enum
{
	UI_BOX_STATE_SLOT_COUNT = 4096,
};
STATIC_ASSERT((UI_BOX_STATE_SLOT_COUNT & (UI_BOX_STATE_SLOT_COUNT - 1)) == 0);

// UI construction is structural. Immediate drawing is only valid after the
// box tree has been built and laid out.
static void ui_assert_paint_phase(UI_Context *ui)
{
	Assert(ui);
	Assert(!ui->builder);
	Assert(ui->draw);
}

void ui_draw_rect(UI_Context *ui, Draw_RectParams params)
{
	ui_assert_paint_phase(ui);
	draw_list_rect(ui->draw, params);
}

void ui_draw_rect_outline(UI_Context *ui, rect_f32 rect, f32 thickness, Color_SRGBA color)
{
	ui_draw_rect(ui, (Draw_RectParams) { .rect = rect_f32_from_slice(rect, AXIS_X, thickness), .color = color });
	ui_draw_rect(ui, (Draw_RectParams) { .rect = rect_f32_from_slice(rect, AXIS_X, -thickness), .color = color });
	ui_draw_rect(ui, (Draw_RectParams) { .rect = rect_f32_from_slice(rect, AXIS_Y, thickness), .color = color });
	ui_draw_rect(ui, (Draw_RectParams) { .rect = rect_f32_from_slice(rect, AXIS_Y, -thickness), .color = color });
}

void ui_draw_inset_shadow(UI_Context *ui, rect_f32 rect, f32 strength)
{
	ui_assert_paint_phase(ui);
	draw_list_inset_shadow(ui->draw, (Draw_InsetShadowParams) {
		.rect = rect,
		.strength = strength,
	});
}

void ui_draw_backdrop(UI_Context *ui, rect_f32 rect, f32 roundness)
{
	ui_assert_paint_phase(ui);
	draw_list_backdrop(ui->draw, (Draw_BackdropParams) {
		.rect = rect,
		.corner_radius = roundness,
		.distortion = 10.f,
		.distortion_width = 15.f,
		.saturation = 1.12f,
		.tint = color_with_alpha(ui->theme.palette.overlay, 0.20f),
		.grain = 0.002f,
		.highlight = 0.055f,
		.shadow = 0.005f,
	});
}

UI_Palette ui_default_palette(void)
{
	return (UI_Palette) {
		.void_background = color_srgba(0x050A0C),
		.background = color_srgba(0x091113),
		.panel = color_srgba(0x0D191C),
		.overlay = color_srgba(0x132326),
		.raised = color_srgba(0x132326),
		.divider = color_srgba(0x294044),
		.text = color_srgba(0xC4D2CF),
		.text_muted = color_srgba(0x718783),
		.teal = color_srgba(0x83A598),
		.cyan = color_srgba(0x62C7C1),
		.blue = color_srgba(0x729BB8),
		.violet = color_srgba(0x8B82AA),
		.amber = color_srgba(0xD1A85C),
		.error = color_srgba(0xC96F68),
		.emission_faint  = 0.01f,
		.emission_low    = 0.05f,
		.emission_medium = 0.10f,
		.emission_high   = 0.15f,
	};
}

UI_Theme ui_default_theme(Font_Handle code_font)
{
	UI_Palette palette = ui_default_palette();
	UI_Theme theme = {
		.palette = palette,
		.background = palette.background,
		.panel_background = palette.panel,
		.panel_outline = palette.divider,
		.panel_outline_focused = palette.teal,
		.splitter = palette.divider,
		.splitter_hot = palette.cyan,
		.slider_track = palette.void_background,
		.slider_thumb = palette.raised,
		.text_neutral = palette.text,
		.text_subtle = palette.text_muted,
		.text_vibrant = palette.cyan,
		.program_bridge = palette.teal,
		.program_counter = palette.amber,
		.code = {
			.font = code_font,
			.size = UI_CODE_FONT_SIZE_DEFAULT,
			.color = palette.text,
		},
	};
	return theme;
}

UI_Context *ui_create(Arena *owner, OS_Window *window, Text_Context *text,
	Draw_Context *draw, UI_Theme theme)
{
	Assert(owner);
	Assert(window);
	Assert(text);
	UI_Context *ui = arena_push_zero(owner, sizeof(*ui));
	ui->owner = owner;
	ui->window = window;
	ui->text = text;
	ui->draw = draw;
	ui->frame_arena = arena_create(0, "UI frame arena");
	ui->theme = theme;
	ui->box_state_slot_count = UI_BOX_STATE_SLOT_COUNT;
	ui->box_state_slots = arena_push_zero(owner, ui->box_state_slot_count * sizeof(*ui->box_state_slots));
	ui->layout_generation = 1;
	ui->previous_frame_time = seconds_now();
	ui->previous_window_size = window->size;
	return ui;
}

void ui_invalidate_layout(UI_Context *ui)
{
	Assert(ui);
	ui->layout_generation++;
	ui->hot = UI_ID_NONE;
	ui->active = UI_ID_NONE;
}

void ui_begin_frame(UI_Context *ui)
{
	Assert(ui);
	Assert(!ui->builder);
	UI_Box *previous_root = ui->root;
	Seconds frame_time = seconds_now();
	ui->frame_elapsed = (f32)Max(frame_time.seconds - ui->previous_frame_time.seconds, 0.0);
	ui->previous_frame_time = frame_time;
	ui->frame_index++;
	ui->mouse_wheel_consumed = false;
	ui->feedback = UI_FEEDBACK_NONE;
	ui->mouse = v2_from_v2i(ui->window->mouse_position);
	if (ui->window->size.x != ui->previous_window_size.x || ui->window->size.y != ui->previous_window_size.y)
	{
		ui->previous_window_size = ui->window->size;
		ui_invalidate_layout(ui);
	}
	ui->hot = UI_ID_NONE;
	if (previous_root && previous_root->state && previous_root->state->last_layout_frame + 1 == ui->frame_index && previous_root->state->layout_generation == ui->layout_generation)
	{
		UI_Box *hit = ui_box_find_deepest(previous_root, ui->mouse);
		if (hit) ui->hot = hit->id;
	}
	arena_reset(&ui->frame_arena);
	ui->root = 0;
	ui->content_root = 0;
	ui->overlay_root = 0;
	ui->tooltip_box = 0;
	ui->tooltip_open = false;
}

UI_BoxState *ui_box_state_get(UI_Context *ui, UI_Id id)
{
	Assert(ui);
	Assert(id.value);
	Assert(ui->box_state_slot_count);

	u32 slot = (u32)id.value & (ui->box_state_slot_count - 1);
	for (UI_BoxState *state = ui->box_state_slots[slot]; state; state = state->hash_next)
	{
		if (!ui_id_equal(state->id, id)) continue;
		state->last_touched_frame = ui->frame_index;
		return state;
	}

	UI_BoxState *state = ui->free_box_states;
	if (state) {
		ui->free_box_states = state->hash_next;
	}
	else {
		state = arena_push_zero(ui->owner, sizeof(*state));
	}
	memory_zero(state, sizeof(*state));
	state->id = id;
	state->last_touched_frame = ui->frame_index;
	state->hash_next = ui->box_state_slots[slot];
	ui->box_state_slots[slot] = state;
	return state;
}

void ui_box_state_forget(UI_Context *ui, UI_Id id)
{
	Assert(ui);
	Assert(id.value);

	u32 slot = (u32)id.value & (ui->box_state_slot_count - 1);
	UI_BoxState **link = &ui->box_state_slots[slot];
	while (*link)
	{
		UI_BoxState *state = *link;
		if (!ui_id_equal(state->id, id))
		{
			link = &state->hash_next;
			continue;
		}

		*link = state->hash_next;
		memory_zero(state, sizeof(*state));
		state->hash_next = ui->free_box_states;
		ui->free_box_states = state;
		if (ui_id_equal(ui->hot, id)) ui->hot = UI_ID_NONE;
		if (ui_id_equal(ui->active, id)) ui->active = UI_ID_NONE;
		break;
	}
}

void ui_end_frame(UI_Context *ui)
{
	Assert(ui);
	Assert(!ui->builder);
	if (!(ui->window->keys[OS_Key_MouseLeft] & OS_KEY_DOWN))
	{
		ui->active = UI_ID_NONE;
	}

	for (u32 slot = 0; slot < ui->box_state_slot_count; slot++)
	{
		UI_BoxState **link = &ui->box_state_slots[slot];
		while (*link)
		{
			UI_BoxState *state = *link;
			if (state->last_touched_frame == ui->frame_index)
			{
				link = &state->hash_next;
				continue;
			}

			*link = state->hash_next;
			if (ui_id_equal(ui->hot, state->id)) ui->hot = UI_ID_NONE;
			if (ui_id_equal(ui->active, state->id)) ui->active = UI_ID_NONE;
			memory_zero(state, sizeof(*state));
			state->hash_next = ui->free_box_states;
			ui->free_box_states = state;
		}
	}
}

void ui_push_z(UI_Context *ui, i32 z)
{
	ui_assert_paint_phase(ui);
	draw_list_push_z(ui->draw, z);
}

void ui_pop_z(UI_Context *ui)
{
	ui_assert_paint_phase(ui);
	draw_list_pop_z(ui->draw);
}

UI_Id ui_id_from_ptr(const void *pointer)
{
	u64 value = (u64)(uintptr_t)pointer;
	value ^= value >> 33;
	value *= 0xff51afd7ed558ccdull;
	value ^= value >> 33;
	value *= 0xc4ceb9fe1a85ec53ull;
	value ^= value >> 33;
	return (UI_Id) { value ? value : 1 };
}

UI_Key ui_key_string(Str string)
{
	u64 value = 14695981039346656037ull;
	value ^= 0x53;
	value *= 1099511628211ull;
	for (u32 index = 0; index < string.size; index++)
	{
		value ^= (u8)string.text[index];
		value *= 1099511628211ull;
	}
	return value ? value : 1;
}

UI_Key ui_key_child(UI_Key parent, UI_Key child)
{
	u64 value = parent ^ (child + 0x9e3779b97f4a7c15ull + (parent << 6) + (parent >> 2));
	return value ? value : 1;
}

UI_Id ui_id_child(UI_Id parent, UI_Key child)
{
	u64 value = ui_key_child(parent.value, child);
	return (UI_Id) { value ? value : 1 };
}

b32 ui_id_equal(UI_Id a, UI_Id b)
{
	return a.value == b.value;
}

b32 ui_is_hot(UI_Context *ui, UI_Id id)
{
	return ui_id_equal(ui->hot, id);
}

b32 ui_is_active(UI_Context *ui, UI_Id id)
{
	return ui_id_equal(ui->active, id);
}

void ui_feedback_emit(UI_Context *ui, UI_Feedback feedback)
{
	Assert(ui);
	ui->feedback |= feedback;
}

UI_Feedback ui_feedback_take(UI_Context *ui)
{
	Assert(ui);
	UI_Feedback feedback = (UI_Feedback)ui->feedback;
	ui->feedback = UI_FEEDBACK_NONE;
	return feedback;
}

// TODO
UI_Response ui_interact(UI_Context *ui, UI_Id id, rect_f32 rect)
{
	Assert(ui);
	Assert(id.value);
	OS_KeyState mouse = ui->window->keys[OS_Key_MouseLeft];
	UI_Response response = {};
	response.hovered = rect_f32_contains(rect, ui->mouse);
	if (response.hovered) {
		ui->hot = id;
	}
	if (!ui->active.value && response.hovered && (mouse & OS_KEY_PRESSED))
	{
		ui->active = id;
		ui->active_press_mouse = ui->mouse;
		response.pressed = true;
	}
	if (ui_id_equal(ui->active, id))
	{
		response.held = !!(mouse & OS_KEY_DOWN);
		response.released = !!(mouse & OS_KEY_RELEASED);
		response.drag_delta = v2_sub(ui->mouse, ui->active_press_mouse);
	}
	return response;
}

UI_Response ui_signal_from_box(UI_Box *box)
{
	Assert(box);
	Assert(box->ui);
	box->hit_intercept = true;
	if (!box->state || !box->has_previous) return (UI_Response) {};

	UI_Context *ui = box->ui;
	UI_Response response = {};
	response.hovered = ui_id_equal(ui->hot, box->id);
	OS_KeyState left_mouse_button = ui->window->keys[OS_Key_MouseLeft];
	if (!ui->active.value && response.hovered && (left_mouse_button & OS_KEY_PRESSED))
	{
		ui->active = box->id;
		ui->active_press_mouse = ui->mouse;
		response.pressed = true;
	}
	if (ui_id_equal(ui->active, box->id))
	{
		response.held = !!(left_mouse_button & OS_KEY_DOWN);
		response.released = !!(left_mouse_button & OS_KEY_RELEASED);
		response.drag_delta = v2_sub(ui->mouse, ui->active_press_mouse);
	}
	return response;
}

void ui_push_clip(UI_Context *ui, rect_f32 rect)
{
	ui_assert_paint_phase(ui);
	draw_list_push_clip(ui->draw, rect);
}

void ui_pop_clip(UI_Context *ui)
{
	ui_assert_paint_phase(ui);
	draw_list_pop_clip(ui->draw);
}

void ui_push_unclipped(UI_Context *ui)
{
	ui_assert_paint_phase(ui);
	draw_list_push_unclipped(ui->draw);
}

void ui_pop_unclipped(UI_Context *ui)
{
	ui_assert_paint_phase(ui);
	draw_list_pop_unclipped(ui->draw);
}

void ui_push_emission(UI_Context *ui, f32 emission)
{
	ui_assert_paint_phase(ui);
	draw_list_push_emission(ui->draw, emission);
}

void ui_pop_emission(UI_Context *ui)
{
	ui_assert_paint_phase(ui);
	draw_list_pop_emission(ui->draw);
}

vec2 ui_measure_text(UI_Context *ui, UI_TextStyle style, Str text)
{
	vec2 result;
	SCRATCH_SCOPE(&ui->frame_arena) {
		PROF_BLOCK("ui text measure") result = text_layout(&ui->frame_arena, ui->text, style.font, style.size, text).metrics.dim;
	}
	return result;
}

vec2 ui_draw_text(UI_Context *ui, rect_f32 rect, UI_TextStyle style, Str text)
{
	ui_assert_paint_phase(ui);
	vec2 size = {};
	if (!text.size) return size;

	PROF_BLOCK("ui text")
	{
		Text_Layout layout = text_layout(&ui->frame_arena, ui->text,
			style.font, style.size, text);
		Draw_TextParams params = {
			.run = text_make_draw_run(&ui->frame_arena, &layout),
			.position = v2(rect.x, rect.y),
			.color = style.color,
		};
		draw_list_text(ui->draw, params);
		size = params.run.dim;
	}
	return size;
}

UI_Response ui_scrollbar(UI_Context *ui, UI_Id id, rect_f32 track, f32 viewport_height, f32 *position, f32 content_height)
{
	ui_assert_paint_phase(ui);
	Assert(position);

	UI_Response response = {};
	f32 max_scroll = Max(content_height - viewport_height, 0.f);
	*position = CLAMP(*position, 0.f, max_scroll);
	if (max_scroll <= 0.f) return response;

	rect_f32 interaction_rect = track;
	interaction_rect.x -= 4.f;
	interaction_rect.w += 4.f;
	rect_f32 thumb_track = rect_f32_inset(track, 4.f);

	f32 visible_ratio = CLAMP(viewport_height / content_height, 0.f, 1.f);
	f32 thumb_size = CLAMP(ceilf(thumb_track.h * visible_ratio), 32.f, thumb_track.h);
	f32 travel = Max(thumb_track.h - thumb_size, 0.f);
	f32 position_ratio = max_scroll > 0.f ? *position / max_scroll : 0.f;
	rect_f32 thumb = rect_f32_from_slice(thumb_track, AXIS_Y, thumb_size);
	thumb.y += travel * position_ratio;

	response = ui_interact(ui, id, interaction_rect);
	if (response.pressed)
	{
		if (!rect_f32_contains(thumb, ui->mouse) && travel > 0.f)
		{
			f32 thumb_y = CLAMP(ui->mouse.y - thumb_size * 0.5f, thumb_track.y, thumb_track.y + travel);
			*position = (thumb_y - thumb_track.y) * max_scroll / travel;
			thumb.y = thumb_y;
		}
		ui->active_start_value = *position;
	}
	if (response.held && travel > 0.f)
	{
		*position = CLAMP(ui->active_start_value + response.drag_delta.y * max_scroll / travel, 0.f, max_scroll);
		position_ratio = *position / max_scroll;
		thumb.y = thumb_track.y + travel * position_ratio;
	}

	ui_draw_rect(ui, (Draw_RectParams) {
		.rect = track,
		.color = ui->theme.slider_track,
		.corner_radii = draw_corner_radii_all(track.w * 0.10f),
		.edge_softness = 0.5f,
	});
	ui_draw_rect(ui, (Draw_RectParams) {
		.rect = thumb,
		.color = ui->theme.slider_thumb,
		.corner_radii = draw_corner_radii_all(thumb.w * 0.10f),
		.edge_softness = 0.5f,
	});
	return response;
}
