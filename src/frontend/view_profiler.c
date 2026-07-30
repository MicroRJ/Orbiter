#include "ui_widgets.h"
#include "views.h"

static const u64 PROFILER_TIMING_SCROLLBAR_ID = 0x50524F4654494D45ull;
static const u64 PROFILER_METRIC_SCROLLBAR_ID = 0x50524F464D455452ull;

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

static void profiler_scroll_table(ViewFrameData *frame, UI_Box *table, f32 *scroll, u64 scrollbar_key)
{
	*scroll = table->scroll_offset.y;
	if (rect_f32_contains(table->viewport, frame->ui->mouse) && frame->ui->window->mouse_wheel.y) {
		*scroll = CLAMP(*scroll - frame->ui->window->mouse_wheel.y * 48.f, table->scroll_min.y, table->scroll_max.y);
	}
	rect_f32 track = {
		.x = table->viewport.x + table->viewport.w,
		.y = table->viewport.y,
		.w = 12.f,
		.h = table->viewport.h,
	};
	f32 laid_out_scroll = table->scroll_offset.y;
	ui_push_layer(frame->ui, UI_LAYER_HEADER);
	ui_scrollbar(frame->ui, ui_id_child(table->id, scrollbar_key), track, table->viewport.h, scroll, table->content_size.y);
	ui_pop_layer(frame->ui);
	if (fabsf(*scroll - laid_out_scroll) > 0.001f)
	{
		table->scroll_offset.y = *scroll;
		ui_box_relayout(table);
		*scroll = table->scroll_offset.y;
	}
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

	Assert(frame->draw_box_tree);
	UI_BoxDesc root_desc = ui_box_desc();
	root_desc.axis = AXIS_X;
	root_desc.size[AXIS_X] = ui_box_fill(1.f);
	root_desc.size[AXIS_Y] = ui_box_fill(1.f);
	root_desc.gap = 12.f;
	root_desc.overflow[AXIS_X] = UI_BOX_OVERFLOW_CLIP;
	root_desc.overflow[AXIS_Y] = UI_BOX_OVERFLOW_CLIP;
	UI_Box *root = ui_build_begin(ui, ui_key_child(UI_KEY("profiler tables"), frame->view->id), LIT("profiler tables"), root_desc);
	UI_Box *timing_table = profiler_build_time_table(ui, snapshot, row_height);
	UI_Box *metric_table = profiler_build_metric_table(ui, snapshot, row_height);
	ui_build_end(ui);

	ProfilerViewState *state = &frame->view->profiler;
	timing_table->scroll_offset.y = state->timing_scroll;
	metric_table->scroll_offset.y = state->metric_scroll;
	ui_box_measure(root, (UI_BoxConstraints) { .min = layout.size, .max = layout.size });
	ui_box_layout(root, layout);
	profiler_scroll_table(frame, timing_table, &state->timing_scroll, PROFILER_TIMING_SCROLLBAR_ID);
	profiler_scroll_table(frame, metric_table, &state->metric_scroll, PROFILER_METRIC_SCROLLBAR_ID);
	frame->draw_box_tree(root);
}

void profiler_view_frame(ViewFrameData *frame)
{
	ViewFrameData content = view_begin_frame(frame, LIT("PROFILER"));
	profiler_view_content(&content);
	view_end_frame(&content);
}
