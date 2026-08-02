#include "ui_widgets.h"
#include "views.h"

static void profiler_graph_box_paint(UI_Box *box)
{
	UI_Context *ui = box->ui;
	rect_f32 rect = box->rect;
	const Profiler_View_State *state = box->content;
	u64 first_visible_frame = state->first_visible_frame;
	u64 visible_frame_count = state->visible_frame_count;

	f32 scale_ms = 64.f;

	ui_push_clip(ui, rect);
	f32 x = rect.x - (f32)fmod(state->offset, state->bar_width);
	for (u64 index = first_visible_frame; index < first_visible_frame + visible_frame_count; index ++)
	{
		const Prof_Frame *frame = prof_timeline_frame(index);
		f32 h = rect.h * (f32)(frame->time.seconds * 1000.0 / scale_ms);
		f32 y = rect.y + rect.h - h;
		ui_draw_rect(ui, (rect_f32) { x, y, state->bar_width - 1.f, h }, ui->theme.text_neutral);
		x += state->bar_width;
	}

	f32 budget_y = rect.y + rect.h - rect.h * (16.f / scale_ms);
	ui_draw_rect(ui, (rect_f32) { rect.x, budget_y, rect.w, 1.f }, ui->theme.text_neutral);

	UI_TextStyle label_style = ui->theme.code;
	label_style.color = ui->theme.text_neutral;

	Str graph_label = str_push_copy_f(&ui->frame_arena, "FRAME TIME  %i MS / 16 MS BUDGET  %s  WHEEL SCROLLS  CTRL+WHEEL ZOOMS  CLICK: TOGGLE LIVE", (i32)scale_ms, state->following ? "LIVE" : "HISTORY");
	ui_draw_text(ui, rect_f32_inset(rect, 4.f), label_style, graph_label);
	ui_pop_clip(ui);
}

static const UI_BoxHooks profiler_graph_box_hooks = {
	.paint = profiler_graph_box_paint,
};

static const Prof_Frame *profiler_build_graph(UI_Context *ui, Profiler_View_State *state)
{
	ui_clean(ui);
	// NOTE(RJ) shrink easily, profiler graph tends to take up lots of space
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_flex(0.5f, 2.f));
	ui_min_size(ui, AXIS_Y, 128.f);
	ui_max_size(ui, AXIS_Y, 128.f * 2.f);
	UI_Box *box = ui_box_make(ui, 1, LIT("profiler graph"));
	box->hooks = &profiler_graph_box_hooks;
	box->content = state;

	i32 paint_z = box->paint.z;
	box->paint = (UI_BoxPaintDesc) {
		.flags = UI_BOX_DRAW_BACKGROUND,
		.background = ui->theme.slider_track,
		.z = paint_z,
	};

	if (!state->initialized) {
		state->bar_width = 5.f;
		state->following    = true;
		state->initialized  = true;
	}

	rect_f32 rect = box->state->rect;

	UI_Response response = ui_signal_from_box(box);

	b32 control = ui->window->keys[OS_Key_LeftControl] & OS_KEY_DOWN || ui->window->keys[OS_Key_RightControl] & OS_KEY_DOWN;
	i32 zoom = control ? ui->window->mouse_wheel.y : 0;
	i32 scroll = !control ? ui->window->mouse_wheel.y : 0;

	if (!ui->mouse_wheel_consumed && response.hovered && zoom)
	{
		ui->mouse_wheel_consumed = true;
		state->offset = state->target_offset;

		f64 zoom_point = ui->mouse.x - rect.x;

		f64 position = (state->offset + zoom_point) / state->bar_width;

		if (zoom > 0) state->bar_width = Min(state->bar_width * 2.f, 128.f);
		else          state->bar_width = Max(state->bar_width * 0.5f,  2.f);

		state->target_offset = position * state->bar_width - zoom_point;
		state->offset = state->target_offset;
		state->following = false;
	}
	if (!ui->mouse_wheel_consumed && response.hovered && scroll)
	{
		ui->mouse_wheel_consumed = true;
		f64 scroll_step = state->bar_width * 8.f;
		state->target_offset += scroll > 0 ? -scroll_step : scroll_step;
		state->following = false;
	}
	if (response.pressed)
	{
		state->following = !state->following;
	}

	u64 timeline_cursor = prof_timeline_cursor();
	u64 timeline_length = Min(timeline_cursor, (u64)PROF_TIMELINE_CAPACITY);
	u64 visible_frame_count = Min((u64)ceilf(rect.w / state->bar_width), timeline_length);

	// NOTE(RJ) offset encodes the absolute timeline index, but we can't go back more than PROF_TIMELINE_CAPACITY without
	// wrapping ...
	f64 min_offset = (timeline_cursor - timeline_length) * (f64)state->bar_width;
	f64 max_offset = (timeline_cursor - visible_frame_count) * (f64)state->bar_width;

	state->target_offset = CLAMP(state->target_offset, min_offset, max_offset);
	state->offset = CLAMP(state->offset, min_offset, max_offset);

	state->offset += (state->target_offset - state->offset) * 0.25f;

	u64 first_visible_frame = (u64)(state->offset / state->bar_width);

	if (state->following) state->target_offset = max_offset;

	state->first_visible_frame = first_visible_frame;
	state->visible_frame_count = visible_frame_count;

	u64 selected_frame = timeline_cursor ? timeline_cursor - 1 : 0;
	const Prof_Frame *frame = prof_timeline_frame(selected_frame);

	if (response.hovered && timeline_cursor) {
		u64 position = (u64)((state->offset + ui->mouse.x - rect.x) / state->bar_width);
		selected_frame = Min(position, selected_frame);
		frame = prof_timeline_frame(selected_frame);

		ui_clean(ui);
		UI_Box *tooltip = ui_tooltip_begin(ui, UI_KEY("profiler_tooltip"), ui->mouse);
		if (tooltip)
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
			ui_text_box_string(ui, UI_KEY("1"), style, str_push_copy_f(&ui->frame_arena, "Frame %llu", selected_frame));
			ui_clean(ui);
			ui_text_box_string(ui, UI_KEY("2"), style, str_push_copy_f(&ui->frame_arena, "%.2f MS", frame->time.seconds * 1000));

			ui_box_end(ui);

			ui_tooltip_end(ui);
		}

	}

	return frame;
}

static Color_SRGBA profiler_color_for_frame_pct(const UI_Theme *theme, Color_SRGBA low, f64 frame_pct)
{
	f32 amount = (f32)CLAMP(frame_pct, 0.0, 1.0);
	return color_srgba_mix(low, theme->palette.error, amount);
}

static void profiler_table_cell(UI_BoxTable *table, UI_TextStyle style, Str sizing_text, Str text, f32 align)
{
	UI_Box *cell = ui_box_table_cell_begin(table);
	cell->desc.layout = &ui_layout_frame;
	ui_clean(table->ui);
	ui_align(table->ui, AXIS_X, align);
	if (sizing_text.size) ui_text_box_sized_string(table->ui, 1, style, sizing_text, text);
	else ui_text_box_string(table->ui, 1, style, text);
	ui_box_table_cell_end(table);
}

static UI_Box *profiler_build_time_table(UI_Context *ui, const Prof_Frame *snapshot, f32 row_height)
{
	UI_TextStyle header_style = ui->theme.code;
	header_style.color = ui->theme.text_neutral;
	UI_TextStyle value_style = ui->theme.code;
	value_style.color = ui->theme.text_subtle;
	f64 frame_seconds = snapshot->time.seconds;
	UI_BoxTableColumn columns[] = {
		ui_box_table_flex(1.f),
		ui_box_table_content(),
		ui_box_table_content(),
		ui_box_table_content(),
		ui_box_table_content(),
	};
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	UI_BoxTable table = ui_box_table_begin(ui, 1, LIT("profiler timing table"), (UI_BoxTableDesc) {
		.columns = columns,
		.column_count = ArrayCount(columns),
		.row_height = row_height,
		.column_gap = 8.f,
		.cell_padd = v2(2.f, 2.f),
	});

	ui_box_table_row_begin(&table, 1);
	profiler_table_cell(&table, header_style, (Str) {}, LIT("SCOPE"), 0.f);
	profiler_table_cell(&table, header_style, LIT("999.999"), LIT("MS"), 1.f);
	profiler_table_cell(&table, header_style, LIT("100.0"), LIT("FRAME %"), 1.f);
	profiler_table_cell(&table, header_style, LIT("999999"), LIT("CALLS"), 1.f);
	profiler_table_cell(&table, header_style, LIT("99999.99"), LIT("US/CALL"), 1.f);
	ui_box_table_row_end(&table);

	for (u32 index = 0; index < snapshot->nfields; ++index)
	{
		const Prof_Field *field = &snapshot->fields[index];
		f64 frame_pct = frame_seconds > 0.0 ? field->time.seconds / frame_seconds : 0.0;
		f64 microseconds_per_call = field->freq ? field->time.seconds * 1000000.0 / field->freq : 0.0;
		UI_TextStyle row_style = value_style;
		row_style.color = profiler_color_for_frame_pct(&ui->theme, value_style.color, frame_pct);
		ui_box_table_row_begin(&table, index + 2);
		profiler_table_cell(&table, row_style, (Str) {}, field->name, 0.f);
		profiler_table_cell(&table, row_style, LIT("999.999"), str_push_copy_f(&ui->frame_arena, "%.3f", field->time.seconds * 1000.0), 1.f);
		profiler_table_cell(&table, row_style, LIT("100.0"), str_push_copy_f(&ui->frame_arena, "%.1f", frame_pct * 100.0), 1.f);
		profiler_table_cell(&table, row_style, LIT("999999"), str_push_copy_f(&ui->frame_arena, "%u", field->freq), 1.f);
		profiler_table_cell(&table, row_style, LIT("99999.99"), str_push_copy_f(&ui->frame_arena, "%.2f", microseconds_per_call), 1.f);
		ui_box_table_row_end(&table);
	}
	return ui_box_table_end(&table);
}

static UI_Box *profiler_build_metric_table(UI_Context *ui, const Prof_Frame *snapshot, f32 row_height)
{
	static const Str metric_names[] = {
#define XPAND(name, text) LIT(text),
		PROF_METRICS_XDEF(XPAND)
#undef XPAND
	};
	STATIC_ASSERT(ArrayCount(metric_names) == PROF_METRIC_COUNT_);

	UI_TextStyle header_style = ui->theme.code;
	header_style.color = ui->theme.text_neutral;
	UI_TextStyle value_style = ui->theme.code;
	value_style.color = ui->theme.text_subtle;
	UI_BoxTableColumn columns[] = {
		ui_box_table_flex(1.f),
		ui_box_table_content(),
	};
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_flex(0.5f, 2.0f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	UI_BoxTable table = ui_box_table_begin(ui, 2, LIT("profiler metric table"), (UI_BoxTableDesc) {
		.columns = columns,
		.column_count = ArrayCount(columns),
		.row_height = row_height,
		.column_gap = 8.f,
		.cell_padd = v2(2.f, 2.f),
	});

	ui_box_table_row_begin(&table, 1);
	profiler_table_cell(&table, header_style, (Str) {}, LIT("METRIC"), 0.f);
	profiler_table_cell(&table, header_style, LIT("9999999999"), LIT("VALUE"), 1.f);
	ui_box_table_row_end(&table);

	for (u32 index = 0; index < PROF_METRIC_COUNT_; ++index)
	{
		ui_box_table_row_begin(&table, index + 2);
		profiler_table_cell(&table, value_style, (Str) {}, metric_names[index], 0.f);
		profiler_table_cell(&table, value_style, LIT("9999999999"), str_push_copy_f(&ui->frame_arena, "%lld", snapshot->metrics.fields[index]), 1.f);
		ui_box_table_row_end(&table);
	}
	return ui_box_table_end(&table);
}

static void profiler_view_content(ViewFrameData *frame)
{
	UI_Context *ui = frame->ui;
	Profiler_View_State *state = &frame->view->profiler;
	f32 row_height = ui->theme.code.size + 4.f;
	ui_clean(ui);
	ui_axis(ui, AXIS_Y);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_padd(ui, AXIS_X, 12.f, 12.f);
	ui_padd(ui, AXIS_Y, 12.f, 12.f);
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_CLIP);
	ui_box_begin(ui, ui_key_child(UI_KEY("profiler"), frame->view->id), LIT("profiler"));

	const Prof_Frame *snapshot = profiler_build_graph(ui, state);

	UI_TextStyle selection_style = ui->theme.code;
	selection_style.color = ui->theme.text_neutral;
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(row_height));
	ui_text_box_string(ui, 2, selection_style, str_push_copy_f(&ui->frame_arena, "SELECTED FRAME %llu  /  %.3f MS", snapshot->id, snapshot->time.seconds * 1000.0));

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_min_size(ui, AXIS_Y, row_height * 5.f);
	ui_axis(ui, AXIS_X);
	ui_gap(ui, 12.f);
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_CLIP);
	ui_box_begin(ui, 3, LIT("profiler tables"));

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	UI_ScrollBox *timing_scroll = ui_scroll_box_begin(ui, 1, AXIS_Y);
	profiler_build_time_table(ui, snapshot, row_height);
	ui_scroll_box_end(timing_scroll);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	UI_ScrollBox *metric_scroll = ui_scroll_box_begin(ui, 2, AXIS_Y);
	profiler_build_metric_table(ui, snapshot, row_height);
	ui_scroll_box_end(metric_scroll);

	ui_box_end(ui);
	ui_box_end(ui);
}

void profiler_view_build_ui(ViewFrameData *frame)
{
	ViewFrameData content = view_begin_frame(frame, LIT("PROFILER"));
	profiler_view_content(&content);
	view_end_frame(&content);
}
