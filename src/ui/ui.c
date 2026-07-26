#include "base.h"
#include "graphics.h"
#include "ttf_api.h"
#include "text.h"
#include "os_graphical.h"
#include "ui.h"

static UI_DrawCommand *ui__push_command(UI_Context *ui, UI_LayerKind layer,
	UI_DrawCommandKind kind, b32 inherit_clip)
{
	Assert(ui);
	Assert(layer >= 0 && layer < UI_LAYER_COUNT);
	UI_DrawCommand *command = arena_push_zero(&ui->frame_arena,
		sizeof(*command));
	command->kind = kind;
	command->emission = ui->emission;
	if (inherit_clip && ui->clip_stack_count && !ui->unclipped_scope_count)
	{
		command->has_clip = true;
		command->clip = ui->clip_stack[ui->clip_stack_count - 1];
	}
	UI_Layer *command_layer = &ui->frame.layers[layer];
	command_layer->has_emission |= command->emission > 0.f;
	if (command_layer->last) {
		command_layer->last->next = command;
	} else {
		command_layer->first = command;
	}
	command_layer->last = command;
	command_layer->command_count += 1;
	return command;
}

UI_DrawCommand *ui_draw_rect(UI_Context *ui, rect_f32 rect, Color_SRGBA color)
{
	UI_DrawCommand *command = ui__push_command(ui, ui->layer, UI_DRAW_COMMAND_RECT, true);
	command->rect.rect = rect;
	command->rect.color = color;
	return command;
}

void ui_draw_rect_outline(UI_Context *ui, rect_f32 rect, f32 thickness, Color_SRGBA color)
{
	ui_draw_rect(ui, rect_f32_from_slice(rect, AXIS_X, thickness), color);
	ui_draw_rect(ui, rect_f32_from_slice(rect, AXIS_X, -thickness), color);
	ui_draw_rect(ui, rect_f32_from_slice(rect, AXIS_Y, thickness), color);
	ui_draw_rect(ui, rect_f32_from_slice(rect, AXIS_Y, -thickness), color);
}

void ui_draw_inset_shadow(UI_Context *ui, rect_f32 rect, f32 strength)
{
	UI_DrawCommand *command = ui__push_command(ui, ui->layer,
		UI_DRAW_COMMAND_INSET_SHADOW, true);
	command->inset_shadow.rect = rect;
	command->inset_shadow.strength = strength;
}

static void ui__draw_backdrop(UI_Context *ui, UI_LayerKind layer, rect_f32 rect)
{
	UI_DrawCommand *command = ui__push_command(ui, layer, UI_DRAW_COMMAND_BACKDROP, false);
	command->backdrop.rect = rect;
	command->backdrop.corner_radius = 5.f;
	command->backdrop.distortion = 10.f;
	command->backdrop.distortion_width = 15.f;
	command->backdrop.saturation = 1.12f;
	command->backdrop.tint = color_with_alpha(
		ui->theme.palette.overlay, 0.20f);
	command->backdrop.grain = 0.002f;
	command->backdrop.highlight = 0.055f;
	command->backdrop.shadow = 0.005f;
	ui->frame.layers[layer].has_backdrops = true;
}

void ui_draw_backdrop(UI_Context *ui, rect_f32 rect)
{
	ui__draw_backdrop(ui, ui->layer, rect);
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
	UI_Theme theme)
{
	Assert(owner);
	Assert(window);
	Assert(text);
	UI_Context *ui = arena_push_zero(owner, sizeof(*ui));
	ui->window = window;
	ui->text = text;
	ui->frame_arena = arena_create(0, "UI frame arena");
	ui->theme = theme;
	return ui;
}

void ui_begin_frame(UI_Context *ui)
{
	Assert(ui);
	arena_reset(&ui->frame_arena);
	ui->frame = (UI_Frame) { 0 };
	ui->layer = UI_LAYER_CONTENT;
	ui->layer_stack_count = 0;
	ui->clip_stack_count = 0;
	ui->unclipped_scope_count = 0;
	ui->emission = 0.f;
	ui->emission_stack_count = 0;
	ui->mouse = v2_from_v2i(ui->window->mouse_position);
	ui->hot = UI_ID_NONE;
}

void ui_end_frame(UI_Context *ui)
{
	Assert(ui);
	Assert(ui->layer_stack_count == 0);
	Assert(ui->clip_stack_count == 0);
	Assert(ui->unclipped_scope_count == 0);
	Assert(ui->emission_stack_count == 0);
	if (!(ui->window->keys[OS_Key_MouseLeft] & OS_KEY_DOWN))
	{
		ui->active = UI_ID_NONE;
	}
}

const UI_Frame *ui_frame(const UI_Context *ui)
{
	Assert(ui);
	return &ui->frame;
}

void ui_push_layer(UI_Context *ui, UI_LayerKind layer)
{
	Assert(ui);
	Assert(layer >= 0 && layer < UI_LAYER_COUNT);
	Assert(ui->layer_stack_count < ArrayCount(ui->layer_stack));
	ui->layer_stack[ui->layer_stack_count++] = ui->layer;
	ui->layer = layer;
}

void ui_pop_layer(UI_Context *ui)
{
	Assert(ui);
	Assert(ui->layer_stack_count > 0);
	ui->layer = ui->layer_stack[--ui->layer_stack_count];
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

UI_Id ui_id_child(UI_Id parent, u64 child)
{
	u64 value = parent.value ^ (child + 0x9e3779b97f4a7c15ull +
		(parent.value << 6) + (parent.value >> 2));
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

UI_Response ui_interact(UI_Context *ui, UI_Id id, rect_f32 rect)
{
	Assert(ui);
	Assert(id.value);
	OS_KeyState mouse = ui->window->keys[OS_Key_MouseLeft];
	UI_Response response = {};
	response.hovered = rect_f32_contains(rect, ui->mouse);
	if (response.hovered)
	{
		ui->hot = id;
	}
	if (response.hovered && (mouse & OS_KEY_PRESSED))
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

void ui_push_clip(UI_Context *ui, rect_f32 rect)
{
	Assert(ui);
	Assert(ui->clip_stack_count < ArrayCount(ui->clip_stack));
	if (ui->clip_stack_count)
	{
		rect_i32 parent = rect_i32_from_f32(
			ui->clip_stack[ui->clip_stack_count - 1]);
		rect_i32 child = rect_i32_from_f32(rect);
		rect = rect_f32_from_i32(rect_i32_intersect(parent, child));
	}
	ui->clip_stack[ui->clip_stack_count++] = rect;
}

void ui_pop_clip(UI_Context *ui)
{
	Assert(ui);
	Assert(ui->clip_stack_count > 0);
	ui->clip_stack_count -= 1;
}

void ui_push_unclipped(UI_Context *ui)
{
	Assert(ui);
	ui->unclipped_scope_count += 1;
}

void ui_pop_unclipped(UI_Context *ui)
{
	Assert(ui);
	Assert(ui->unclipped_scope_count > 0);
	ui->unclipped_scope_count -= 1;
}

void ui_push_emission(UI_Context *ui, f32 emission)
{
	Assert(ui);
	Assert(ui->emission_stack_count < ArrayCount(ui->emission_stack));
	ui->emission_stack[ui->emission_stack_count++] = ui->emission;
	ui->emission = emission;
}

void ui_pop_emission(UI_Context *ui)
{
	Assert(ui);
	Assert(ui->emission_stack_count > 0);
	ui->emission = ui->emission_stack[--ui->emission_stack_count];
}

void ui_draw_panel(UI_Context *ui, rect_f32 rect, b32 focused)
{
	(void)focused;
	ui_draw_rect(ui, rect, ui->theme.panel_background);
	ui_draw_inset_shadow(ui, rect, 0.035f);
}

void ui_draw_splitter(UI_Context *ui, rect_f32 rect, UI_Id id)
{
	(void)id;
	ui_draw_rect(ui, rect, ui->theme.slider_track);
}

void ui_draw_image(UI_Context *ui, UI_ImageParams params)
{
	UI_DrawCommand *command = ui__push_command(ui, ui->layer,
		UI_DRAW_COMMAND_IMAGE, true);
	command->image.params = params;
}

vec2 ui_measure_text(UI_Context *ui, UI_TextStyle style, String text)
{
	vec2 result;
	ARENA_SCOPE(&ui->frame_arena) {
		result = text_layout(&ui->frame_arena, ui->text, style.font, style.size, text).metrics.dim;
	}
	return result;
}

vec2 ui_draw_text(UI_Context *ui, rect_f32 rect, UI_TextStyle style, String text)
{
	vec2 size = {};
	if (!text.size) return size;

	PROF_BLOCK("ui text")
	{
		Text_Layout layout = text_layout(&ui->frame_arena, ui->text,
			style.font, style.size, text);
		UI_DrawCommand *command = ui__push_command(ui, ui->layer,
			UI_DRAW_COMMAND_TEXT, true);
		command->text.run = text_make_draw_run(&ui->frame_arena, &layout);
		command->text.position = v2(rect.x, rect.y);
		command->text.color = style.color;
		size = command->text.run.dim;
	}
	return size;
}

UI_Response ui_scrollbar(UI_Context *ui, UI_Id id, rect_f32 track, f32 viewport_height, f32 *position, f32 content_height)
{
	Assert(ui);
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

	UI_DrawCommand *cmd = ui_draw_rect(ui, track, ui->theme.slider_track);
	cmd->rect.roundness = track.w * 0.10f;
	cmd->rect.edge_softness = 0.5f;
	cmd = ui_draw_rect(ui, thumb, ui->theme.slider_thumb);
	cmd->rect.roundness = thumb.w * 0.10f;
	cmd->rect.edge_softness = 0.5f;
	return response;
}

UI_TableColumnSpec ui_table_column_content(void)
{
	return (UI_TableColumnSpec) { UI_TABLE_COLUMN_CONTENT, 0.f };
}

UI_TableColumnSpec ui_table_column_fixed(f32 width)
{
	return (UI_TableColumnSpec) { UI_TABLE_COLUMN_FIXED, Max(width, 0.f) };
}

UI_TableColumnSpec ui_table_column_flex(f32 weight)
{
	return (UI_TableColumnSpec) { UI_TABLE_COLUMN_FLEX, Max(weight, 0.f) };
}

UI_Table ui_table_begin(UI_Context *ui, Arena *arena, rect_f32 rect, u32 row_count, u32 column_count, f32 row_height)
{
	Assert(ui);
	Assert(arena);
	Assert(row_count);
	Assert(column_count);

	UI_Table table = {
		.ui = ui,
		.rect = rect,
		.row_height = row_height,
		.column_gap = 8.f,
		.cell_padding = v2(2.f, 2.f),
		.row_count = row_count,
		.column_count = column_count,
	};
	table.columns = arena_push_zero(arena, column_count * sizeof(*table.columns));
	table.cells = arena_push_zero(arena, row_count * column_count * sizeof(*table.cells));
	table.resolved_widths = arena_push_zero(arena, column_count * sizeof(*table.resolved_widths));
	for (u32 column = 0; column < column_count; ++column) {
		table.columns[column] = ui_table_column_flex(1.f);
	}
	return table;
}

void ui_table_set_column(UI_Table *table, u32 column,
	UI_TableColumnSpec spec)
{
	Assert(table);
	Assert(column < table->column_count);
	table->columns[column] = spec;
	table->is_laid_out = false;
}

void ui_table_set_text(UI_Table *table, u32 row, u32 column,
	UI_TextStyle style, String text)
{
	Assert(table);
	Assert(row < table->row_count);
	Assert(column < table->column_count);
	table->cells[row * table->column_count + column] =
		(UI_TableCell) { text, style };
	table->is_laid_out = false;
}

void ui_table_layout(UI_Table *table)
{
	Assert(table);
	f32 committed_width = table->column_gap *
		Max((i32)table->column_count - 1, 0);
	f32 flex_weight = 0.f;

	for (u32 column = 0; column < table->column_count; ++column)
	{
		UI_TableColumnSpec spec = table->columns[column];
		f32 width = 0.f;
		if (spec.kind == UI_TABLE_COLUMN_CONTENT)
		{
			for (u32 row = 0; row < table->row_count; ++row)
			{
				UI_TableCell *cell = &table->cells[
					row * table->column_count + column];
				vec2 text_size = ui_measure_text(table->ui,
					cell->style, cell->text);
				width = Max(width, text_size.x + table->cell_padding.x * 2.f);
			}
		}
		else if (spec.kind == UI_TABLE_COLUMN_FIXED)
		{
			width = spec.value;
		}
		else
		{
			flex_weight += spec.value;
		}

		table->resolved_widths[column] = width;
		committed_width += width;
	}

	f32 flex_space = Max(table->rect.w - committed_width, 0.f);
	for (u32 column = 0; column < table->column_count; ++column)
	{
		UI_TableColumnSpec spec = table->columns[column];
		if (spec.kind == UI_TABLE_COLUMN_FLEX)
		{
			table->resolved_widths[column] = flex_weight > 0.f
				? flex_space * spec.value / flex_weight : 0.f;
		}
	}
	table->is_laid_out = true;
}

rect_f32 ui_table_cell_rect(const UI_Table *table, u32 row, u32 column)
{
	Assert(table);
	Assert(table->is_laid_out);
	Assert(row < table->row_count);
	Assert(column < table->column_count);

	rect_f32 result = {
		.x = table->rect.x,
		.y = table->rect.y + row * table->row_height,
		.w = table->resolved_widths[column],
		.h = table->row_height,
	};
	for (u32 previous = 0; previous < column; ++previous)
	{
		result.x += table->resolved_widths[previous] + table->column_gap;
	}
	return result;
}

void ui_table_draw(UI_Table *table)
{
	Assert(table);
	ui_table_layout(table);
	for (u32 row = 0; row < table->row_count; ++row)
	{
		for (u32 column = 0; column < table->column_count; ++column)
		{
			UI_TableCell *cell = &table->cells[
				row * table->column_count + column];
			if (!cell->text.size) continue;

			rect_f32 cell_rect = ui_table_cell_rect(table, row, column);
			if (cell_rect.w <= 0.f || cell_rect.h <= 0.f) continue;
			vec2 text_size = ui_measure_text(table->ui, cell->style, cell->text);
			rect_f32 text_rect = cell_rect;
			text_rect.x += table->cell_padding.x;
			text_rect.y += Max((cell_rect.h - text_size.y) * 0.5f, 0.f);

			// Clip each cell independently so constrained tables hide overflow
			// instead of allowing one value to collide with the next column.
			rect_i32 clip = rect_i32_intersect(rect_i32_from_f32(cell_rect),
				rect_i32_from_f32(table->rect));
			ui_push_clip(table->ui, rect_f32_from_i32(clip));
			ui_draw_text(table->ui, text_rect, cell->style, cell->text);
			ui_pop_clip(table->ui);
		}
	}
}
