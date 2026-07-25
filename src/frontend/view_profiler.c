#include "views.h"

typedef struct
{
	String name;
	Color_SRGBA color;
}
ProfilerGraphPhase;

static f64 profiler_scope_seconds(const Prof_Frame *frame, String name)
{
	for (u32 index = 0; index < frame->nfields; ++index) {
		if (string_match(frame->fields[index].name, name)) return frame->fields[index].time.seconds;
	}
	return 0.0;
}

static const Prof_Frame *profiler_draw_graph(ViewFrameData *frame, rect_f32 rect, ProfilerViewState *state)
{
	UI_Context *ui = frame->ui;
	ui_draw_rect(ui, rect, ui->theme.slider_track);

	f32 scale_ms = 64.f;
	if (!state->initialized)
	{
		state->frame_stride = 5.f;
		state->following = true;
		state->initialized = true;
	}
	i64 newest_frame = prof_timeline_index() - 1;
	i64 oldest_frame = Max(0, newest_frame - 1023);
	if (state->following) state->right_frame_index = newest_frame;
	state->right_frame_index = CLAMP(state->right_frame_index, oldest_frame, newest_frame);

	b32 hovered = rect_f32_contains(rect, ui->mouse);
	i32 wheel = ui->window->mouse_wheel.y;
	b32 control = ui->window->keys[OS_Key_LeftControl] & OS_KEY_DOWN || ui->window->keys[OS_Key_RightControl] & OS_KEY_DOWN;
	if (hovered && wheel)
	{
		if (control)
		{
			if (wheel > 0) state->frame_stride = Min(state->frame_stride * 2.f, 20.f);
			else state->frame_stride = Max(state->frame_stride * 0.5f, 2.f);
		}
		else
		{
			i64 scroll_step = Max(1, (i64)(rect.w / state->frame_stride / 8.f));
			state->right_frame_index += wheel > 0 ? -scroll_step : scroll_step;
			state->right_frame_index = CLAMP(state->right_frame_index, oldest_frame, newest_frame);
			state->following = state->right_frame_index == newest_frame;
		}
	}
	if (hovered && ui->window->keys[OS_Key_MouseLeft] & OS_KEY_PRESSED) {
		state->following = true;
		state->right_frame_index = newest_frame;
	}

	f32 gap = 1.f;
	f32 bar_width = Max(1.f, state->frame_stride - gap);
	i32 visible_count = Min((i32)(state->right_frame_index - oldest_frame + 1), (i32)(rect.w / state->frame_stride + 0.5f));
	f32 start_draw_x = rect.x + rect.w - bar_width;
	ProfilerGraphPhase phases[] = {
		{ LIT("debug stepping"),     color_srgba_mix(ui->theme.palette.teal, ui->theme.palette.background, 0.35f) },
		{ LIT("emulation"),          ui->theme.palette.teal },
		{ LIT("audio buffering"),    ui->theme.palette.cyan },
		{ LIT("audio"),              color_srgba_mix(ui->theme.palette.violet, ui->theme.palette.background, 0.30f) },
		{ LIT("program refinement"), ui->theme.palette.violet },
		{ LIT("application"),        color_srgba_mix(ui->theme.palette.blue, ui->theme.palette.background, 0.35f) },
		{ LIT("application draw"),   ui->theme.palette.blue },
		{ LIT("present wait"),       color_srgba_mix(ui->theme.palette.amber, ui->theme.palette.background, 0.35f) },
		{ LIT("frame pacing"),       ui->theme.palette.amber },
	};
	i32 hovered_index = -1;
	ui_push_clip(ui, rect);

	for (i32 index = 0; index < visible_count; ++index)
	{
		const Prof_Frame *snapshot = prof_timeline_frame(state->right_frame_index - index);
		f64 phase_seconds[ArrayCount(phases)] = {};
		for (u32 phase = 0; phase < ArrayCount(phases); ++phase) {
			phase_seconds[phase] = profiler_scope_seconds(snapshot, phases[phase].name);
		}
		phase_seconds[7] = Max(0.0, phase_seconds[7] - phase_seconds[8]);

		f32 x = start_draw_x - index * state->frame_stride;
		f32 y = rect.y + rect.h;
		for (u32 phase = 0; phase < ArrayCount(phases); ++phase)
		{
			f32 height = rect.h * (f32)(phase_seconds[phase] * 1000.0 / scale_ms);
			height = Min(height, y - rect.y);
			y -= height;
			ui_draw_rect(ui, (rect_f32) { x, y, bar_width, height }, phases[phase].color);
		}

		f32 total_height = rect.h * CLAMP((f32)(snapshot->time.seconds * 1000.0 / scale_ms), 0.f, 1.f);
		rect_f32 total = { x, rect.y + rect.h - total_height, bar_width, total_height };
		ui_draw_rect_outline(ui, total, 1.f, color_with_alpha(ui->theme.text_neutral, 0.65f));
		if (rect_f32_contains((rect_f32) { x - gap * 0.5f, rect.y, bar_width + gap, rect.h }, ui->mouse)) {
			hovered_index = index;
		}
	}

	f32 budget_y = rect.y + rect.h - rect.h * (16.f / scale_ms);
	ui_draw_rect(ui, (rect_f32) { rect.x, budget_y, rect.w, 1.f }, ui->theme.text_neutral);
	i64 selected_frame = state->right_frame_index - Max(hovered_index, 0);
	UI_TextStyle label_style = ui->theme.code;
	label_style.color = ui->theme.text_neutral;
	String graph_label = push_formatted(frame->scratch, "FRAME PHASES  %i MS / 16 MS BUDGET  %s  WHEEL SCROLLS  CTRL+WHEEL ZOOMS  CLICK: LIVE", (i32)scale_ms, state->following ? "LIVE" : "HISTORY");
	ui_draw_text(ui, rect_f32_inset(rect, 4.f), label_style, graph_label);
	ui_pop_clip(ui);

	return prof_timeline_frame(selected_frame);
}

static Color_SRGBA profiler_color_for_frame_pct(const UI_Theme *theme, Color_SRGBA low, f64 frame_pct)
{
	f32 amount = (f32)CLAMP(frame_pct, 0.0, 1.0);
	return color_srgba_mix(low, theme->palette.error, amount);
}

static void profiler_set_header(UI_Table *table, u32 column, String text)
{
	UI_TextStyle style = table->ui->theme.code;
	style.color = table->ui->theme.text_neutral;
	ui_table_set_text(table, 0, column, style, text);
}

static void profiler_draw_time_table(ViewFrameData *frame, rect_f32 rect, const Prof_Frame *snapshot, f32 row_height)
{
	UI_Context *ui = frame->ui;
	UI_TextStyle value_style = ui->theme.code;
	value_style.color = ui->theme.text_subtle;
	f64 frame_seconds = snapshot->time.seconds;
	u32 visible_count = Min(snapshot->nfields, (u32)Max(0.f, floorf(rect.h / row_height) - 1.f));
	UI_Table table = ui_table_begin(ui, frame->scratch, rect, visible_count + 1, 5, row_height);
	ui_table_set_column(&table, 0, ui_table_column_flex(1.f));
	ui_table_set_column(&table, 1, ui_table_column_content());
	ui_table_set_column(&table, 2, ui_table_column_content());
	ui_table_set_column(&table, 3, ui_table_column_content());
	ui_table_set_column(&table, 4, ui_table_column_content());
	profiler_set_header(&table, 0, LIT("SCOPE"));
	profiler_set_header(&table, 1, LIT("MS"));
	profiler_set_header(&table, 2, LIT("FRAME %"));
	profiler_set_header(&table, 3, LIT("CALLS"));
	profiler_set_header(&table, 4, LIT("US/CALL"));

	for (u32 index = 0; index < visible_count; ++index)
	{
		const Prof_Field *field = &snapshot->fields[index];
		u32 row = index + 1;
		f64 frame_pct = frame_seconds > 0.0 ? field->time.seconds / frame_seconds : 0.0;
		f64 microseconds_per_call = field->freq ? field->time.seconds * 1000000.0 / field->freq : 0.0;
		UI_TextStyle row_style = value_style;
		row_style.color = profiler_color_for_frame_pct(&ui->theme, value_style.color, frame_pct);
		ui_table_set_text(&table, row, 0, row_style, field->name);
		ui_table_set_text(&table, row, 1, row_style, push_formatted(frame->scratch, "%.3f", field->time.seconds * 1000.0));
		ui_table_set_text(&table, row, 2, row_style, push_formatted(frame->scratch, "%.1f", frame_pct * 100.0));
		ui_table_set_text(&table, row, 3, row_style, push_formatted(frame->scratch, "%u", field->freq));
		ui_table_set_text(&table, row, 4, row_style, push_formatted(frame->scratch, "%.2f", microseconds_per_call));
	}
	ui_table_draw(&table);
}

static void profiler_draw_metric_table(ViewFrameData *frame, rect_f32 rect, const Prof_Frame *snapshot, f32 row_height)
{
	static const String metric_names[] = {
#define XPAND(name, text) LIT(text),
		PROF_METRICS_XDEF(XPAND)
#undef XPAND
	};
	STATIC_ASSERT(ArrayCount(metric_names) == PROF_METRIC_COUNT_);

	UI_Context *ui = frame->ui;
	UI_TextStyle value_style = ui->theme.code;
	value_style.color = ui->theme.text_subtle;
	u32 visible_count = Min((u32)PROF_METRIC_COUNT_, (u32)Max(0.f, floorf(rect.h / row_height) - 1.f));
	UI_Table table = ui_table_begin(ui, frame->scratch, rect, visible_count + 1, 2, row_height);
	ui_table_set_column(&table, 0, ui_table_column_flex(1.f));
	ui_table_set_column(&table, 1, ui_table_column_content());
	profiler_set_header(&table, 0, LIT("METRIC"));
	profiler_set_header(&table, 1, LIT("VALUE"));
	for (u32 index = 0; index < visible_count; ++index)
	{
		ui_table_set_text(&table, index + 1, 0, value_style, metric_names[index]);
		ui_table_set_text(&table, index + 1, 1, value_style, push_formatted(frame->scratch, "%lld", snapshot->metrics.fields[index]));
	}
	ui_table_draw(&table);
}

static void profiler_view_content(ViewFrameData *frame)
{
	UI_Context *ui = frame->ui;
	rect_f32 layout = rect_f32_inset(frame->rect, 12.f);
	f32 row_height = ui->theme.code.size + 4.f;
	f32 graph_height = Min(280.f, Max(128.f, layout.h * 0.64f));
	rect_f32 graph = rect_f32_slice(&layout, AXIS_Y, graph_height);
	const Prof_Frame *snapshot = profiler_draw_graph(frame, graph, &frame->view->profiler);
	rect_f32 selection = rect_f32_slice(&layout, AXIS_Y, row_height);
	UI_TextStyle selection_style = ui->theme.code;
	selection_style.color = ui->theme.text_neutral;
	ui_draw_text(ui, selection, selection_style, push_formatted(frame->scratch, "SELECTED FRAME %llu  /  %.3f MS", snapshot->id, snapshot->time.seconds * 1000.0));

	f32 gap = 12.f;
	rect_f32 timing = rect_f32_slice(&layout, AXIS_X, (layout.w - gap) * 0.5f);
	rect_f32_slice(&layout, AXIS_X, gap);
	profiler_draw_time_table(frame, timing, snapshot, row_height);
	profiler_draw_metric_table(frame, layout, snapshot, row_height);
}

void profiler_view_frame(ViewFrameData *frame)
{
	ViewFrameData content = view_begin_frame(frame, LIT("PROFILER"));
	profiler_view_content(&content);
	view_end_frame(&content);
}
