// Included by main.c after the shared playground types and helpers.

typedef struct
{
	Font_Handle font;
	Color_SRGBA slate;
	Color_SRGBA teal;
	Color_SRGBA violet;
	Color_SRGBA error;
	UI_TextStyle header_style;
	UI_TextStyle value_style;
	UI_TextStyle numeric_style;
}
PlaygroundDummyProfiler;

static void playground_dummy_table_cell(UI_BoxTable *table, UI_TextStyle style, Str sizing_text, Str text, f32 align)
{
	UI_Box *cell = ui_box_table_cell_begin(table);
	cell->desc.layout = &UI_FlatLayoutHooks;
	ui_clean(table->ui);
	ui_align(table->ui, AXIS_X, align);
	if (sizing_text.size)
	{
		ui_text_sized(table->ui, 1, style, sizing_text, text);
	}
	else
	{
		ui_text(table->ui, 1, style, text);
	}
	ui_box_table_cell_end(table);
}

static void playground_build_dummy_timing_row(UI_BoxTable *table, PlaygroundDummyProfiler *profiler, u32 row, b32 header)
{
	static const Str scope_names[] = {
		LIT("main frame incl wait"),
		LIT("debug stepping"),
		LIT("emulation"),
		LIT("audio buffering"),
		LIT("audio"),
		LIT("snapshot"),
		LIT("program refinement"),
		LIT("application"),
		LIT("application draw"),
		LIT("present wait"),
		LIT("frame pacing"),
	};
	Color_SRGBA row_color = header ? profiler->teal : color_srgba_mix(profiler->slate, profiler->violet, row & 1 ? 0.12f : 0.20f);
	UI_Box *row_box = ui_box_table_row_begin(table, header ? 1 : row + 2);
	row_box->user = playground_visual(&table->ui->frame_arena, row_color, false);

	UI_TextStyle label_style = header ? profiler->header_style : profiler->value_style;
	UI_TextStyle numeric_style = header ? profiler->header_style : profiler->numeric_style;
	Str scope = header ? LIT("SCOPE") : scope_names[row % ArrayCount(scope_names)];
	Str milliseconds = header ? LIT("MS") : str_push_copy_f(&table->ui->frame_arena, "%.3f", 0.025f + (f32)(row % 400) * 0.013f);
	Str frame_pct = header ? LIT("FRAME %") : str_push_copy_f(&table->ui->frame_arena, "%.1f", 0.2f + (f32)(row % 97));
	Str calls = header ? LIT("CALLS") : str_push_copy_f(&table->ui->frame_arena, "%u", 1 + row % 99999);
	Str per_call = header ? LIT("US/CALL") : str_push_copy_f(&table->ui->frame_arena, "%.2f", 0.1f + (f32)(row % 1200) * 0.07f);
	playground_dummy_table_cell(table, label_style, (Str) {}, scope, 0.f);
	playground_dummy_table_cell(table, numeric_style, LIT("999.999"), milliseconds, 1.f);
	playground_dummy_table_cell(table, numeric_style, LIT("100.0 %"), frame_pct, 1.f);
	playground_dummy_table_cell(table, numeric_style, LIT("999999"), calls, 1.f);
	playground_dummy_table_cell(table, numeric_style, LIT("99999.99"), per_call, 1.f);
	ui_box_table_row_end(table);
}

static void playground_build_dummy_metric_row(UI_BoxTable *table, PlaygroundDummyProfiler *profiler, u32 row, b32 header)
{
	static const Str metric_names[] = {
		LIT("TEXT LAYOUT CALLS"),
		LIT("TEXT DRAW RUNS"),
		LIT("BOXES BUILT"),
		LIT("VISIBLE VIRTUAL ROWS"),
		LIT("DRAW COMMANDS"),
		LIT("GLYPH CACHE HITS"),
		LIT("GLYPH CACHE MISSES"),
		LIT("TRANSIENT TEXTURES"),
	};
	Color_SRGBA row_color = header ? profiler->violet : color_srgba_mix(profiler->slate, profiler->teal, row & 1 ? 0.10f : 0.18f);
	UI_Box *row_box = ui_box_table_row_begin(table, header ? 1 : row + 2);
	row_box->user = playground_visual(&table->ui->frame_arena, row_color, false);

	UI_TextStyle label_style = header ? profiler->header_style : profiler->value_style;
	UI_TextStyle numeric_style = header ? profiler->header_style : profiler->numeric_style;
	Str metric = header ? LIT("METRIC") : metric_names[row % ArrayCount(metric_names)];
	Str value = header ? LIT("VALUE") : str_push_copy_f(&table->ui->frame_arena, "%llu", 1000ull + (u64)row * 7919ull);
	playground_dummy_table_cell(table, label_style, (Str) {}, metric, 0.f);
	playground_dummy_table_cell(table, numeric_style, LIT("9999999999"), value, 1.f);
	ui_box_table_row_end(table);
}

static PlaygroundScene playground_build_dummy_profiler(Arena *arena, UI_Context *ui, Font_Handle font, PlaygroundDensity density)
{
	PlaygroundScene scene = {};
	Color_SRGBA teal = color_srgba(0x18B8A4);
	Color_SRGBA violet = color_srgba(0x9558E8);
	Color_SRGBA amber = color_srgba(0xE5A83C);
	Color_SRGBA slate = color_srgba(0x25343A);
	PlaygroundDummyProfiler *profiler = arena_push_zero(arena, sizeof(*profiler));
	profiler->font = font;
	profiler->slate = slate;
	profiler->teal = teal;
	profiler->violet = violet;
	profiler->error = color_srgba(0xE65B65);
	profiler->header_style = (UI_TextStyle) { .font = font, .size = 14, .color = color_srgba(0xD6E7E4), .align = v2(0.f, 0.5f) };
	profiler->value_style = (UI_TextStyle) { .font = font, .size = 14, .color = color_srgba(0xAFC5C1), .align = v2(0.f, 0.5f) };
	profiler->numeric_style = profiler->value_style;

	UI_BoxDesc root_desc = playground_fill_desc();
	root_desc.axis = AXIS_Y;
	root_desc.horz_padd[0] = root_desc.horz_padd[1] = density.outer_padding;
	root_desc.vert_padd[0] = root_desc.vert_padd[1] = density.outer_padding;
	root_desc.gap = density.gap;
	UI_Box *root = ui_build_begin(ui, UI_KEY("dummy profiler"), LIT("dummy profiler"), root_desc);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(48.f));
	ui_axis(ui, AXIS_X);
	ui_padd(ui, AXIS_X, 16.f, 16.f);
	ui_padd(ui, AXIS_Y, 8.f, 8.f);
	ui_gap(ui, 8.f);
	playground_begin_box(ui, 6000, LIT(""), slate, false);
	UI_TextStyle title = { .font = font, .size = 16, .color = teal };
	UI_TextStyle subtle = { .font = font, .size = 14, .color = color_srgba(0x8EAAA5) };
	playground_frame_slot_begin(ui, 1, AXIS_Y);
	ui_clean(ui);
	ui_align(ui, AXIS_Y, 0.5f);
	ui_text(ui, 1, title, LIT("ORBITER PROFILER"));
	ui_box_end(ui);
	playground_frame_slot_begin(ui, 2, AXIS_Y);
	ui_clean(ui);
	ui_align(ui, AXIS_Y, 0.5f);
	ui_text(ui, 1, subtle, LIT("|  BOX TABLE PROTOTYPE"));
	ui_box_end(ui);
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_box_make(ui, 3, LIT(""));
	subtle.align.x = 1.f;
	playground_frame_slot_begin(ui, 4, AXIS_Y);
	ui_clean(ui);
	ui_align(ui, AXIS_Y, 0.5f);
	ui_text(ui, 1, subtle, LIT("TAB: BASICS  |  SPACE: DENSITY"));
	ui_box_end(ui);
	ui_box_end(ui);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_flex(1.f, 1.f));
	ui_min_size(ui, AXIS_Y, 128.f);
	ui_max_size(ui, AXIS_Y, 240.f);
	UI_Box *graph_box = playground_make_box(ui, 6001, LIT("DUMMY FRAME PHASE GRAPH  |  custom rendering stays inside this rectangle"), color_srgba_mix(teal, slate, 0.58f), false);
	graph_box->intrinsic_size.y = 184.f;

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(34.f));
	ui_axis(ui, AXIS_X);
	ui_padd(ui, AXIS_X, 10.f, 10.f);
	playground_begin_box(ui, 6002, LIT(""), color_srgba_mix(amber, slate, 0.72f), false);
	playground_frame_slot_begin(ui, 1, AXIS_Y);
	ui_clean(ui);
	ui_align(ui, AXIS_Y, 0.5f);
	ui_text(ui, 1, profiler->value_style, LIT("SELECTED FRAME 123456  /  16.667 MS"));
	ui_box_end(ui);
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_box_make(ui, 2, LIT(""));
	UI_TextStyle live = profiler->header_style;
	live.color = amber;
	live.align.x = 1.f;
	playground_frame_slot_begin(ui, 3, AXIS_Y);
	ui_clean(ui);
	ui_align(ui, AXIS_Y, 0.5f);
	ui_text(ui, 1, live, LIT("LIVE"));
	ui_box_end(ui);
	ui_box_end(ui);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_axis(ui, AXIS_X);
	ui_gap(ui, density.gap);
	ui_box_begin(ui, 6003, LIT(""));

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.6f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_axis(ui, AXIS_Y);
	ui_padd(ui, AXIS_X, density.card_padding, density.card_padding);
	ui_padd(ui, AXIS_Y, density.card_padding, density.card_padding);
	ui_gap(ui, 4.f);
	playground_begin_box(ui, 6100, LIT(""), slate, false);
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	UI_ScrollBox *timing_scroll = ui_scroll_box_begin(ui, 1, AXIS_Y);
	UI_BoxTableColumn timing_columns[] = {
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
	UI_BoxTable timing_table = ui_box_table_begin(ui, 2, LIT("timing table"), (UI_BoxTableDesc) {
		.columns = timing_columns,
		.column_count = ArrayCount(timing_columns),
		.row_height = 32.f,
		.column_gap = 10.f,
		.row_gap = 1.f,
		.cell_padd = v2(8.f, 5.f),
	});
	playground_build_dummy_timing_row(&timing_table, profiler, 0, true);
	for (u32 row = 0; row < 128; row++) {
		playground_build_dummy_timing_row(&timing_table, profiler, row, false);
	}
	ui_box_table_end(&timing_table);
	ui_scroll_box_end(timing_scroll);
	timing_scroll->track->paint.background = color_srgba_mix(violet, slate, 0.72f);
	timing_scroll->thumb->paint.background = color_srgba(0xC99CFF);
	ui_clean(ui);
	ui_box_end(ui);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_axis(ui, AXIS_Y);
	ui_padd(ui, AXIS_X, density.card_padding, density.card_padding);
	ui_padd(ui, AXIS_Y, density.card_padding, density.card_padding);
	ui_gap(ui, 4.f);
	playground_begin_box(ui, 6200, LIT(""), slate, false);
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	UI_ScrollBox *metric_scroll = ui_scroll_box_begin(ui, 1, AXIS_Y);
	UI_BoxTableColumn metric_columns[] = {
		ui_box_table_flex(1.f),
		ui_box_table_content(),
	};
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	UI_BoxTable metric_table = ui_box_table_begin(ui, 2, LIT("metric table"), (UI_BoxTableDesc) {
		.columns = metric_columns,
		.column_count = ArrayCount(metric_columns),
		.row_height = 32.f,
		.column_gap = 10.f,
		.row_gap = 1.f,
		.cell_padd = v2(8.f, 5.f),
	});
	playground_build_dummy_metric_row(&metric_table, profiler, 0, true);
	for (u32 row = 0; row < 64; row++) {
		playground_build_dummy_metric_row(&metric_table, profiler, row, false);
	}
	ui_box_table_end(&metric_table);
	ui_scroll_box_end(metric_scroll);
	metric_scroll->track->paint.background = color_srgba_mix(violet, slate, 0.72f);
	metric_scroll->thumb->paint.background = color_srgba(0xC99CFF);
	ui_clean(ui);
	ui_box_end(ui);

	ui_box_end(ui);
	scene.root = ui_build_end(ui);
	return scene;
}
