#include "ui_widgets.h"

typedef struct
{
	String string;
	String sizing_string;
	UI_TextStyle style;
}
UI_TextBoxData;

typedef struct
{
	UI_BoxTableColumn *columns;
	f32 *natural_widths;
	f32 *resolved_widths;
	u32 column_count;
	f32 row_height;
	f32 column_gap;
}
UI_BoxTableData;

static vec2 ui_box__measure_text(UI_Box *box, UI_BoxConstraints constraints)
{
	(void)constraints;
	UI_TextBoxData *text = box->content;
	String measured_string = text->sizing_string.size ? text->sizing_string : text->string;
	return ui_measure_text(box->ui, text->style, measured_string);
}

static void ui_box__paint_text(UI_Box *box)
{
	UI_TextBoxData *text = box->content;
	vec2 text_size = ui_measure_text(box->ui, text->style, text->string);
	vec2 remaining = v2(Max(0.f, box->viewport.w - text_size.x), Max(0.f, box->viewport.h - text_size.y));
	vec2 position = v2_add(box->viewport.pos, v2_mul(remaining, text->style.align));
	rect_f32 text_rect = { .pos = position, .size = text_size };
	b32 clip_to_viewport = text_rect.x < box->viewport.x || text_rect.y < box->viewport.y || text_rect.x + text_rect.w > box->viewport.x + box->viewport.w || text_rect.y + text_rect.h > box->viewport.y + box->viewport.h;
	ui_push_clip(box->ui, box->clip_rect);
	if (clip_to_viewport) ui_push_clip(box->ui, box->viewport);
	ui_draw_text(box->ui, text_rect, text->style, text->string);
	if (clip_to_viewport) ui_pop_clip(box->ui);
	ui_pop_clip(box->ui);
}

static const UI_BoxHooks ui_box__text_ops = {
	.measure = ui_box__measure_text,
	.paint = ui_box__paint_text,
};

void ui_box_paint(UI_Box *box)
{
	Assert(box);
	UI_Context *ui = box->ui;
	UI_BoxPaintDesc *paint = &box->paint;
	b32 has_paint = paint->flags || box->ops && box->ops->paint;
	if (!has_paint) return;

	if (paint->emission > 0.f) ui_push_emission(ui, paint->emission);
	ui_push_clip(ui, box->clip_rect);
	if (paint->flags & UI_BOX_DRAW_BACKGROUND)
	{
		UI_DrawCommand *command = ui_draw_rect(ui, box->rect, paint->background);
		command->rect.roundness = paint->roundness;
		command->rect.edge_softness = paint->edge_softness;
	}
	if (paint->flags & UI_BOX_DRAW_BORDER && paint->border_width > 0.f) {
		ui_draw_rect_outline(ui, box->rect, paint->border_width, paint->border);
	}
	if (box->ops && box->ops->paint) box->ops->paint(box);
	ui_pop_clip(ui);
	if (paint->emission > 0.f) ui_pop_emission(ui);
}

static UI_Box *ui_box__make_text(UI_Context *ui, UI_Key key, UI_BoxDesc *desc, UI_TextStyle style, String sizing_string, String string)
{
	UI_BoxBuilder *builder = ui->builder;
	Assert(builder);
	Assert(ui->text);
	Assert(style.font);
	Assert(style.size > 0);
	UI_TextBoxData *text = arena_push_zero(builder->arena, sizeof(*text));
	text->string = string;
	text->sizing_string = sizing_string;
	text->style = style;
	UI_Box *box = desc ? ui_box_make_desc(ui, key, string, *desc) : ui_box_make(ui, key, string);
	box->ops = &ui_box__text_ops;
	box->content = text;
	return box;
}

UI_Box *ui_text_box(UI_Context *ui, UI_Key key, UI_TextStyle style, const char *format, ...)
{
	Assert(ui);
	Assert(ui->builder);
	va_list arguments;
	va_start(arguments, format);
	String string = push_formatted_v(ui->builder->arena, format, arguments);
	va_end(arguments);
	return ui_box__make_text(ui, key, 0, style, (String) {}, string);
}

UI_Box *ui_text_box_sized(UI_Context *ui, UI_Key key, UI_TextStyle style, String sizing_string, const char *format, ...)
{
	Assert(ui);
	Assert(ui->builder);
	va_list arguments;
	va_start(arguments, format);
	String string = push_formatted_v(ui->builder->arena, format, arguments);
	va_end(arguments);
	return ui_box__make_text(ui, key, 0, style, sizing_string, string);
}

UI_Box *ui_text_box_string(UI_Context *ui, UI_Key key, UI_TextStyle style, String string)
{
	return ui_box__make_text(ui, key, 0, style, (String) {}, string);
}

UI_Box *ui_text_box_sized_string(UI_Context *ui, UI_Key key, UI_TextStyle style, String sizing_string, String string)
{
	return ui_box__make_text(ui, key, 0, style, sizing_string, string);
}

UI_Box *ui_text_box_desc(UI_Context *ui, UI_Key key, UI_BoxDesc desc, UI_TextStyle style, const char *format, ...)
{
	Assert(ui);
	Assert(ui->builder);
	va_list arguments;
	va_start(arguments, format);
	String string = push_formatted_v(ui->builder->arena, format, arguments);
	va_end(arguments);
	return ui_box__make_text(ui, key, &desc, style, (String) {}, string);
}

UI_Box *ui_text_box_sized_desc(UI_Context *ui, UI_Key key, UI_BoxDesc desc, UI_TextStyle style, String sizing_string, const char *format, ...)
{
	Assert(ui);
	Assert(ui->builder);
	va_list arguments;
	va_start(arguments, format);
	String string = push_formatted_v(ui->builder->arena, format, arguments);
	va_end(arguments);
	return ui_box__make_text(ui, key, &desc, style, sizing_string, string);
}

UI_Box *ui_text_box_string_desc(UI_Context *ui, UI_Key key, UI_BoxDesc desc, UI_TextStyle style, String string)
{
	return ui_box__make_text(ui, key, &desc, style, (String) {}, string);
}

UI_Box *ui_text_box_sized_string_desc(UI_Context *ui, UI_Key key, UI_BoxDesc desc, UI_TextStyle style, String sizing_string, String string)
{
	return ui_box__make_text(ui, key, &desc, style, sizing_string, string);
}

UI_Response ui_button(UI_Context *ui, UI_Key key, String text)
{
	Assert(ui);
	Assert(ui->builder);

	UI_TextStyle style = ui->theme.code;
	style.align = v2(0.5f, 0.5f);

	UI_BoxDesc desc = ui_box_desc();
	desc.horz_padd[0] = desc.horz_padd[1] = 10.f;
	desc.vert_padd[0] = desc.vert_padd[1] = 6.f;
	UI_Box *box = ui_text_box_string_desc(ui, key, desc, style, text);
	UI_Response response = ui_signal_from_box(box);

	UI_Palette *palette = &ui->theme.palette;
	box->paint = (UI_BoxPaintDesc) {
		.flags = UI_BOX_DRAW_BACKGROUND | UI_BOX_DRAW_BORDER,
		.background = palette->raised,
		.border = palette->divider,
		.border_width = 1.f,
		.roundness = 3.f,
		.edge_softness = 0.5f,
	};
	if (response.hovered)
	{
		box->paint.background = color_srgba_mix(palette->raised, palette->cyan, 0.20f);
		box->paint.border = palette->cyan;
	}
	if (response.held)
	{
		box->paint.background = color_srgba_mix(palette->raised, palette->teal, 0.45f);
		box->paint.border = palette->teal;
	}
	return response;
}

static const u64 UI_SCROLL_TRACK_KEY = 0x5343524F4C4C4241ull;
static const u64 UI_SCROLL_SPACE_BEFORE_KEY = 1;
static const u64 UI_SCROLL_THUMB_KEY = 2;
static const u64 UI_SCROLL_SPACE_AFTER_KEY = 3;

static f32 ui_scroll__smooth(f32 offset, f32 target, f32 elapsed)
{
	f32 half_life = 0.055f;
	return target + (offset - target) * exp2f(-Max(elapsed, 0.f) / half_life);
}

static void ui_scroll__size_bar(UI_Scroll *scroll, f32 track_extent, f32 viewport_extent, f32 content_extent, f32 scroll_min, f32 scroll_max, f32 scroll_offset)
{
	AXIS axis = scroll->axis;
	f32 max_scroll = scroll_max - scroll_min;
	f32 thumb_extent = track_extent;
	f32 thumb_offset = 0.f;
	if (max_scroll > 0.001f && content_extent > 0.f)
	{
		thumb_extent = Min(Max(24.f, track_extent * viewport_extent / content_extent), track_extent);
		f32 travel = Max(0.f, track_extent - thumb_extent);
		f32 ratio = (scroll_offset - scroll_min) / max_scroll;
		thumb_offset = travel * CLAMP(ratio, 0.f, 1.f);
	}

	scroll->space_before->desc.size[axis] = ui_box_fill(thumb_offset);
	scroll->thumb->desc.size[axis] = ui_box_fill(thumb_extent);
	scroll->space_after->desc.size[axis] = ui_box_fill(Max(track_extent - thumb_offset - thumb_extent, 0.f));
}

static void ui_scroll__prepare_track(UI_Box *box)
{
	UI_Scroll *scroll = box->content;
	Assert(scroll);
	Assert(box == scroll->track);
	UI_Box *viewport = scroll->viewport;
	UI_BoxState *state = viewport->state;
	AXIS axis = scroll->axis;

	scroll->offset = viewport->scroll_offset.xy[axis];
	scroll->target = CLAMP(scroll->target, viewport->scroll_min.xy[axis], viewport->scroll_max.xy[axis]);
	state->view_offset.xy[axis] = scroll->offset;
	state->view_target.xy[axis] = scroll->target;
	ui_scroll__size_bar(scroll, box->viewport.wh[axis], viewport->viewport.wh[axis], Max(viewport->content_size.xy[axis], viewport->viewport.wh[axis]), viewport->scroll_min.xy[axis], viewport->scroll_max.xy[axis], scroll->offset);
}

static const UI_BoxHooks ui_scroll__track_ops = {
	.prepare_layout = ui_scroll__prepare_track,
};

UI_Scroll *ui_scroll_begin(UI_Context *ui, UI_Key key, AXIS axis)
{
	Assert(ui);
	UI_BoxBuilder *builder = ui->builder;
	Assert(builder);
	Assert(axis == AXIS_X || axis == AXIS_Y);

	UI_Scroll *scroll = arena_push_zero(builder->arena, sizeof(*scroll));
	scroll->ui = ui;
	scroll->axis = axis;
	scroll->parent_count = builder->parent_count;
	scroll->desc_count = builder->desc_count;

	UI_BoxDesc root_desc = builder->desc;
	root_desc.axis = !axis;
	root_desc.gap = 0.f;
	root_desc.overflow[AXIS_X] = UI_BOX_OVERFLOW_VISIBLE;
	root_desc.overflow[AXIS_Y] = UI_BOX_OVERFLOW_VISIBLE;
	scroll->root = ui_box_begin_desc(ui, key, LIT("scroll"), root_desc);

	ui_push(ui);
	builder->desc = ui_box_desc();
	return scroll;
}

void ui_scroll_reset(UI_Scroll *scroll)
{
	Assert(scroll);
	scroll->reset = true;
	scroll->has_previous = false;
	scroll->offset = 0.f;
	scroll->target = 0.f;
	scroll->ui->active = UI_ID_NONE;
	if (scroll->viewport)
	{
		UI_BoxState *viewport_state = scroll->viewport->state;
		viewport_state->view_offset.xy[scroll->axis] = 0.f;
		viewport_state->view_target.xy[scroll->axis] = 0.f;
		scroll->viewport->scroll_offset.xy[scroll->axis] = 0.f;
	}
}

void ui_scroll_end(UI_Scroll *scroll)
{
	Assert(scroll);
	UI_Context *ui = scroll->ui;
	UI_BoxBuilder *builder = ui->builder;
	AXIS axis = scroll->axis;
	AXIS perp_axis = !axis;
	Assert(builder->parent == scroll->root);
	Assert(builder->parent_count == scroll->parent_count + 1);
	Assert(builder->desc_count == scroll->desc_count + 1);
	Assert(scroll->root->child_count == 1);

	scroll->viewport = scroll->root->first;
	Assert(scroll->viewport == scroll->root->last);
	scroll->viewport->desc.size[perp_axis] = ui_box_fill(1.f);
	scroll->viewport->desc.min_size.xy[perp_axis] = 0.f;
	scroll->viewport->desc.max_size.xy[perp_axis] = UI_BOX_INFINITY;
	scroll->viewport->desc.overflow[axis] = UI_BOX_OVERFLOW_SCROLL;

	UI_BoxDesc track = ui_box_desc();
	track.axis = axis;
	track.size[axis] = ui_box_fill(1.f);
	track.size[perp_axis] = ui_box_pixels(12.f);
	track.padd[perp_axis][0] = track.padd[perp_axis][1] = 3.f;
	scroll->track = ui_box_begin_desc(ui, UI_SCROLL_TRACK_KEY, LIT(""), track);

	UI_BoxDesc piece = ui_box_desc();
	piece.size[AXIS_X] = ui_box_fill(1.f);
	piece.size[AXIS_Y] = ui_box_fill(1.f);
	piece.size[axis] = ui_box_fill(0.f);
	scroll->space_before = ui_box_make_desc(ui, UI_SCROLL_SPACE_BEFORE_KEY, LIT(""), piece);
	piece.size[axis] = ui_box_fill(1.f);
	scroll->thumb = ui_box_make_desc(ui, UI_SCROLL_THUMB_KEY, LIT(""), piece);
	piece.size[axis] = ui_box_fill(0.f);
	scroll->space_after = ui_box_make_desc(ui, UI_SCROLL_SPACE_AFTER_KEY, LIT(""), piece);
	ui_box_end(ui);

	scroll->track->paint = (UI_BoxPaintDesc) {
		.flags = UI_BOX_DRAW_BACKGROUND,
		.background = ui->theme.slider_track,
		.roundness = 0.5f,
		.edge_softness = 0.5f,
	};
	scroll->space_before->paint = (UI_BoxPaintDesc) {};
	scroll->thumb->paint = (UI_BoxPaintDesc) {
		.flags = UI_BOX_DRAW_BACKGROUND,
		.background = ui->theme.slider_thumb,
		.roundness = 0.5f,
		.edge_softness = 0.5f,
	};
	scroll->space_after->paint = (UI_BoxPaintDesc) {};

	UI_BoxState *viewport_state = scroll->viewport->state;
	UI_BoxState *track_state = scroll->track->state;
	UI_BoxState *thumb_state = scroll->thumb->state;
	Assert(viewport_state);
	Assert(track_state);
	Assert(thumb_state);

	scroll->has_previous = scroll->viewport->has_previous && scroll->track->has_previous && scroll->thumb->has_previous;
	scroll->offset = viewport_state->view_offset.xy[axis];
	scroll->target = viewport_state->view_target.xy[axis];
	if (scroll->reset)
	{
		viewport_state->view_offset.xy[axis] = 0.f;
		viewport_state->view_target.xy[axis] = 0.f;
		scroll->viewport->scroll_offset.xy[axis] = 0.f;
		scroll->has_previous = false;
		scroll->offset = 0.f;
		scroll->target = 0.f;
	}

	if (scroll->has_previous)
	{
		f32 scroll_min = viewport_state->scroll_min.xy[axis];
		f32 scroll_max = viewport_state->scroll_max.xy[axis];
		f32 max_scroll = scroll_max - scroll_min;
		i32 wheel = ui->window->mouse_wheel.xy[axis];
		if (wheel && !ui->mouse_wheel_consumed && rect_f32_contains(viewport_state->hit_rect, ui->mouse))
		{
			scroll->target -= wheel * 48.f;
			ui->mouse_wheel_consumed = true;
		}

		f32 travel = Max(0.f, track_state->viewport.wh[axis] - thumb_state->rect.wh[axis]);
		UI_Response thumb_response = ui_signal_from_box(scroll->thumb);
		if (thumb_response.pressed) {
			ui->active_start_value = scroll->offset;
		}
		if (thumb_response.held && travel > 0.f && max_scroll > 0.f)
		{
			scroll->offset = CLAMP(ui->active_start_value + thumb_response.drag_delta.xy[axis] * max_scroll / travel, scroll_min, scroll_max);
			scroll->target = scroll->offset;
		}

		if (!thumb_response.hovered && !ui_is_active(ui, scroll->thumb->id))
		{
			UI_Response track_response = ui_signal_from_box(scroll->track);
			if (track_response.pressed && max_scroll > 0.f)
			{
				f32 direction = ui->mouse.xy[axis] < thumb_state->rect.xy[axis] ? -1.f : 1.f;
				scroll->target += direction * viewport_state->viewport.wh[axis] * 0.85f;
			}
		}

		scroll->target = CLAMP(scroll->target, scroll_min, scroll_max);
		if (!ui_is_active(ui, scroll->thumb->id)) {
			scroll->offset = ui_scroll__smooth(scroll->offset, scroll->target, ui->frame_elapsed);
		}
		scroll->offset = CLAMP(scroll->offset, scroll_min, scroll_max);
	}
	scroll->viewport->scroll_offset.xy[axis] = scroll->offset;
	viewport_state->view_target.xy[axis] = scroll->target;

	scroll->track->content = scroll;
	scroll->track->ops = &ui_scroll__track_ops;

	ui_box_end(ui);
	ui_pop(ui);
}

UI_BoxTableColumn ui_box_table_content(void)
{
	return (UI_BoxTableColumn) { .kind = UI_BOX_TABLE_COLUMN_CONTENT };
}

UI_BoxTableColumn ui_box_table_fixed(f32 width)
{
	return (UI_BoxTableColumn) { .kind = UI_BOX_TABLE_COLUMN_FIXED, .value = Max(0.f, width) };
}

UI_BoxTableColumn ui_box_table_flex(f32 weight)
{
	return (UI_BoxTableColumn) { .kind = UI_BOX_TABLE_COLUMN_FLEX, .value = Max(0.f, weight) };
}

static vec2 ui_box__measure_table(UI_Box *box, UI_BoxConstraints constraints)
{
	(void)constraints;
	UI_BoxTableData *table = box->content;
	Assert(table);
	memory_zero(table->natural_widths, table->column_count * sizeof(*table->natural_widths));

	for (UI_Box *row = box->first; row; row = row->next)
	{
		Assert(row->child_count == table->column_count);
		u32 column = 0;
		for (UI_Box *cell = row->first; cell; cell = cell->next, column++)
		{
			Assert(column < table->column_count);
			cell->desc.size[AXIS_X] = ui_box_content();
			vec2 measured = ui_box_measure(cell, (UI_BoxConstraints) { .max = v2(UI_BOX_INFINITY, table->row_height) });
			table->natural_widths[column] = Max(table->natural_widths[column], measured.x);
		}
		Assert(column == table->column_count);
	}

	f32 table_width = table->column_gap * Max((i32)table->column_count - 1, 0);
	for (u32 column = 0; column < table->column_count; column++)
	{
		UI_BoxTableColumn spec = table->columns[column];
		table->resolved_widths[column] = spec.kind == UI_BOX_TABLE_COLUMN_FIXED ? spec.value : table->natural_widths[column];
		table_width += table->resolved_widths[column];
	}
	for (UI_Box *row = box->first; row; row = row->next)
	{
		row->measured_size = row->arranged_size = v2(table_width, table->row_height);
		u32 column = 0;
		for (UI_Box *cell = row->first; cell; cell = cell->next, column++)
		{
			Assert(column < table->column_count);
			cell->desc.size[AXIS_X] = ui_box_pixels(table->resolved_widths[column]);
			cell->measured_size.x = cell->arranged_size.x = table->resolved_widths[column];
		}
		Assert(column == table->column_count);
	}

	f32 table_height = table->row_height * box->child_count + box->desc.gap * Max((i32)box->child_count - 1, 0);
	return v2(table_width, table_height);
}

static void ui_box__prepare_table_layout(UI_Box *box)
{
	UI_BoxTableData *table = box->content;
	f32 committed_width = table->column_gap * Max((i32)table->column_count - 1, 0);
	f32 flex_weight = 0.f;
	for (u32 column = 0; column < table->column_count; column++)
	{
		UI_BoxTableColumn spec = table->columns[column];
		if (spec.kind == UI_BOX_TABLE_COLUMN_FIXED) {
			table->resolved_widths[column] = spec.value;
			committed_width += spec.value;
		}
		else if (spec.kind == UI_BOX_TABLE_COLUMN_CONTENT) {
			table->resolved_widths[column] = table->natural_widths[column];
			committed_width += table->natural_widths[column];
		}
		else {
			flex_weight += spec.value;
		}
	}
	f32 flex_space = Max(0.f, box->viewport.w - committed_width);
	for (u32 column = 0; column < table->column_count; column++)
	{
		UI_BoxTableColumn spec = table->columns[column];
		if (spec.kind == UI_BOX_TABLE_COLUMN_FLEX) {
			table->resolved_widths[column] = flex_weight > 0.f ? flex_space * spec.value / flex_weight : table->natural_widths[column];
		}
	}

	f32 table_width = table->column_gap * Max((i32)table->column_count - 1, 0);
	for (u32 column = 0; column < table->column_count; column++) {
		table_width += table->resolved_widths[column];
	}
	for (UI_Box *row = box->first; row; row = row->next)
	{
		row->measured_size.x = row->arranged_size.x = table_width;
		u32 column = 0;
		for (UI_Box *cell = row->first; cell; cell = cell->next, column++)
		{
			Assert(column < table->column_count);
			cell->desc.size[AXIS_X] = ui_box_pixels(table->resolved_widths[column]);
			cell->measured_size.x = cell->arranged_size.x = table->resolved_widths[column];
		}
		Assert(column == table->column_count);
	}
}

static const UI_BoxHooks ui_box__table_ops = {
	.measure_children = ui_box__measure_table,
	.prepare_layout = ui_box__prepare_table_layout,
};

UI_BoxTable ui_box_table_begin(UI_Context *ui, UI_Key key, String name, UI_BoxTableDesc desc)
{
	Assert(ui);
	UI_BoxBuilder *builder = ui->builder;
	Assert(builder);
	Assert(desc.columns);
	Assert(desc.column_count);
	Assert(desc.row_height > 0.f);
	UI_BoxTableData *data = arena_push_zero(builder->arena, sizeof(*data));
	data->columns = arena_push_copy(builder->arena, desc.column_count * sizeof(*data->columns), desc.columns);
	data->natural_widths = arena_push_zero(builder->arena, desc.column_count * sizeof(*data->natural_widths));
	data->resolved_widths = arena_push_zero(builder->arena, desc.column_count * sizeof(*data->resolved_widths));
	data->column_count = desc.column_count;
	data->row_height = desc.row_height;
	data->column_gap = desc.column_gap;

	UI_BoxDesc box_desc = builder->desc;
	box_desc.axis = AXIS_Y;
	box_desc.gap = desc.row_gap;
	UI_Box *box = ui_box_begin_desc(ui, key, name, box_desc);
	box->content = data;
	box->ops = &ui_box__table_ops;
	return (UI_BoxTable) {
		.ui = ui,
		.box = box,
		.desc = desc,
	};
}

UI_Box *ui_box_table_row_begin(UI_BoxTable *table, UI_Key key)
{
	Assert(table);
	Assert(table->ui);
	Assert(!table->row);
	UI_BoxDesc desc = ui_box_desc();
	desc.axis = AXIS_X;
	desc.size[AXIS_X] = ui_box_fill(1.f);
	desc.size[AXIS_Y] = ui_box_pixels(table->desc.row_height);
	desc.gap = table->desc.column_gap;
	table->row = ui_box_begin_desc(table->ui, key, LIT("table row"), desc);
	table->column_index = 0;
	return table->row;
}

void ui_box_table_row_end(UI_BoxTable *table)
{
	Assert(table);
	Assert(table->row);
	Assert(!table->cell);
	Assert(table->column_index == table->desc.column_count);
	ui_box_end(table->ui);
	table->row = 0;
}

UI_Box *ui_box_table_cell_begin(UI_BoxTable *table)
{
	Assert(table);
	Assert(table->row);
	Assert(!table->cell);
	Assert(table->column_index < table->desc.column_count);
	UI_BoxDesc desc = ui_box_desc();
	desc.size[AXIS_Y] = ui_box_fill(1.f);
	desc.horz_padd[0] = desc.horz_padd[1] = table->desc.cell_padd.x;
	desc.vert_padd[0] = desc.vert_padd[1] = table->desc.cell_padd.y;
	desc.overflow[AXIS_X] = UI_BOX_OVERFLOW_CLIP;
	table->cell = ui_box_begin_desc(table->ui, table->column_index + 1, LIT("table cell"), desc);
	table->column_index++;
	return table->cell;
}

void ui_box_table_cell_end(UI_BoxTable *table)
{
	Assert(table);
	Assert(table->cell);
	ui_box_end(table->ui);
	table->cell = 0;
}

UI_Box *ui_box_table_end(UI_BoxTable *table)
{
	Assert(table);
	Assert(table->box);
	Assert(!table->row);
	Assert(!table->cell);
	ui_box_end(table->ui);
	return table->box;
}
