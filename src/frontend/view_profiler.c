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

	String graph_label = push_formatted(&ui->frame_arena, "FRAME TIME  %i MS / 16 MS BUDGET  %s  WHEEL SCROLLS  CTRL+WHEEL ZOOMS  CLICK: TOGGLE LIVE", (i32)scale_ms, state->following ? "LIVE" : "HISTORY");
	ui_draw_text(ui, rect_f32_inset(rect, 4.f), label_style, graph_label);
	ui_pop_clip(ui);
}

static const UI_BoxHooks profiler_graph_box_hooks = {
	.paint = profiler_graph_box_paint,
};

static const Prof_Frame *profiler_build_graph(UI_Context *ui, Profiler_View_State *state)
{
	ui_push(ui);
	// NOTE(RJ) shrink easily, profiler graph tends to take up lots of space
	ui_size(ui, AXIS_X, ui_box_fill(1.f));
	ui_size(ui, AXIS_Y, ui_box_flex(0.5f, 2.f));
	ui_min_size(ui, AXIS_Y, 128.f);
	ui_max_size(ui, AXIS_Y, 128.f * 2.f);
	UI_Box *box = ui_box_make(ui, 1, LIT("profiler graph"));
	ui_pop(ui);
	box->ops = &profiler_graph_box_hooks;
	box->content = state;

	box->paint = (UI_BoxPaintDesc) {
		.flags = UI_BOX_DRAW_BACKGROUND,
		.background = ui->theme.slider_track,
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
	if (response.hovered && timeline_cursor) {
		u64 position = (u64)((state->offset + ui->mouse.x - rect.x) / state->bar_width);
		selected_frame = Min(position, selected_frame);
	}

	return prof_timeline_frame(selected_frame);
}

static Color_SRGBA profiler_color_for_frame_pct(const UI_Theme *theme, Color_SRGBA low, f64 frame_pct)
{
	f32 amount = (f32)CLAMP(frame_pct, 0.0, 1.0);
	return color_srgba_mix(low, theme->palette.error, amount);
}

static void profiler_table_cell(UI_BoxTable *table, UI_TextStyle style, String sizing_text, String text, f32 align)
{
	ui_box_table_cell_begin(table);
	UI_BoxDesc desc = ui_box_desc();
	desc.perp_align = align;
	if (sizing_text.size) ui_text_box_sized_string_desc(table->ui, 1, desc, style, sizing_text, text);
	else ui_text_box_string_desc(table->ui, 1, desc, style, text);
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
	ui_push(ui);
	ui_size(ui, AXIS_X, ui_box_fill(1.f));
	ui_size(ui, AXIS_Y, ui_box_fill(1.f));
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_SCROLL);
	UI_BoxTable table = ui_box_table_begin(ui, 1, LIT("profiler timing table"), (UI_BoxTableDesc) {
		.columns = columns,
		.column_count = ArrayCount(columns),
		.row_height = row_height,
		.column_gap = 8.f,
		.cell_padd = v2(2.f, 2.f),
	});
	ui_pop(ui);

	ui_box_table_row_begin(&table, 1);
	profiler_table_cell(&table, header_style, (String) {}, LIT("SCOPE"), 0.f);
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
		profiler_table_cell(&table, row_style, (String) {}, field->name, 0.f);
		profiler_table_cell(&table, row_style, LIT("999.999"), push_formatted(&ui->frame_arena, "%.3f", field->time.seconds * 1000.0), 1.f);
		profiler_table_cell(&table, row_style, LIT("100.0"), push_formatted(&ui->frame_arena, "%.1f", frame_pct * 100.0), 1.f);
		profiler_table_cell(&table, row_style, LIT("999999"), push_formatted(&ui->frame_arena, "%u", field->freq), 1.f);
		profiler_table_cell(&table, row_style, LIT("99999.99"), push_formatted(&ui->frame_arena, "%.2f", microseconds_per_call), 1.f);
		ui_box_table_row_end(&table);
	}
	return ui_box_table_end(&table);
}

static UI_Box *profiler_build_metric_table(UI_Context *ui, const Prof_Frame *snapshot, f32 row_height)
{
	static const String metric_names[] = {
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
	ui_push(ui);
	ui_size(ui, AXIS_X, ui_box_fill(1.f));
	ui_size(ui, AXIS_Y, ui_box_fill(1.f));
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_SCROLL);
	UI_BoxTable table = ui_box_table_begin(ui, 2, LIT("profiler metric table"), (UI_BoxTableDesc) {
		.columns = columns,
		.column_count = ArrayCount(columns),
		.row_height = row_height,
		.column_gap = 8.f,
		.cell_padd = v2(2.f, 2.f),
	});
	ui_pop(ui);

	ui_box_table_row_begin(&table, 1);
	profiler_table_cell(&table, header_style, (String) {}, LIT("METRIC"), 0.f);
	profiler_table_cell(&table, header_style, LIT("9999999999"), LIT("VALUE"), 1.f);
	ui_box_table_row_end(&table);

	for (u32 index = 0; index < PROF_METRIC_COUNT_; ++index)
	{
		ui_box_table_row_begin(&table, index + 2);
		profiler_table_cell(&table, value_style, (String) {}, metric_names[index], 0.f);
		profiler_table_cell(&table, value_style, LIT("9999999999"), push_formatted(&ui->frame_arena, "%lld", snapshot->metrics.fields[index]), 1.f);
		ui_box_table_row_end(&table);
	}
	return ui_box_table_end(&table);
}

static void profiler_view_content(ViewFrameData *frame)
{
	UI_Context *ui = frame->ui;
	Profiler_View_State *state = &frame->view->profiler;
	f32 row_height = ui->theme.code.size + 4.f;
	UI_BoxDesc root_desc = ui_box_desc();
	root_desc.axis = AXIS_Y;
	root_desc.size[AXIS_X] = ui_box_fill(1.f);
	root_desc.size[AXIS_Y] = ui_box_fill(1.f);
	root_desc.horz_padd[0] = root_desc.horz_padd[1] = 12.f;
	root_desc.vert_padd[0] = root_desc.vert_padd[1] = 12.f;
	root_desc.overflow[AXIS_X] = UI_BOX_OVERFLOW_CLIP;
	root_desc.overflow[AXIS_Y] = UI_BOX_OVERFLOW_CLIP;
	ui_box_begin_desc(ui, ui_key_child(UI_KEY("profiler"), frame->view->id), LIT("profiler"), root_desc);

	const Prof_Frame *snapshot = profiler_build_graph(ui, state);

	UI_TextStyle selection_style = ui->theme.code;
	selection_style.color = ui->theme.text_neutral;
	UI_BoxDesc selection_desc = ui_box_desc();
	selection_desc.size[AXIS_X] = ui_box_fill(1.f);
	selection_desc.size[AXIS_Y] = ui_box_pixels(row_height);
	ui_text_box_string_desc(ui, 2, selection_desc, selection_style, push_formatted(&ui->frame_arena, "SELECTED FRAME %llu  /  %.3f MS", snapshot->id, snapshot->time.seconds * 1000.0));

	ui_push(ui);
	ui_size(ui, AXIS_X, ui_box_fill(1.f));
	ui_size(ui, AXIS_Y, ui_box_fill(1.f));
	ui_min_size(ui, AXIS_Y, row_height * 5.f);
	ui_axis(ui, AXIS_X);
	ui_gap(ui, 12.f);
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_CLIP);
	ui_box_begin(ui, 3, LIT("profiler tables"));
	ui_pop(ui);

	ui_push(ui);
	ui_size(ui, AXIS_X, ui_box_fill(1.f));
	ui_size(ui, AXIS_Y, ui_box_fill(1.f));
	UI_Scroll *timing_scroll = ui_scroll_begin(ui, 1, AXIS_Y);
	profiler_build_time_table(ui, snapshot, row_height);
	ui_scroll_end(timing_scroll);
	ui_pop(ui);

	ui_push(ui);
	ui_size(ui, AXIS_X, ui_box_fill(1.f));
	ui_size(ui, AXIS_Y, ui_box_fill(1.f));
	UI_Scroll *metric_scroll = ui_scroll_begin(ui, 2, AXIS_Y);
	profiler_build_metric_table(ui, snapshot, row_height);
	ui_scroll_end(metric_scroll);
	ui_pop(ui);

	ui_box_end(ui);
	ui_box_end(ui);
}

void profiler_view_build_ui(ViewFrameData *frame)
{
	ViewFrameData content = view_begin_frame(frame, LIT("PROFILER"));
	profiler_view_content(&content);
	view_end_frame(&content);
}
