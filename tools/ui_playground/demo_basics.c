// Included by main.c after the shared playground types and helpers.

static PlaygroundScene playground_build_basics(Arena *arena, UI_Context *ui, Font_Handle font, PlaygroundDensity density)
{
	PlaygroundScene scene = {};
	Color_SRGBA teal = color_srgba(0x18B8A4);
	Color_SRGBA blue = color_srgba(0x3478F6);
	Color_SRGBA violet = color_srgba(0x9558E8);
	Color_SRGBA amber = color_srgba(0xE5A83C);
	Color_SRGBA slate = color_srgba(0x25343A);

	UI_BoxDesc root_desc = playground_fill_desc();
	root_desc.axis = AXIS_Y;
	root_desc.horz_padd[0] = root_desc.horz_padd[1] = density.outer_padding;
	root_desc.vert_padd[0] = root_desc.vert_padd[1] = density.outer_padding;
	root_desc.gap = density.gap;

	UI_Box *root = ui_build_begin(ui, UI_KEY("basics"), LIT("root"), root_desc);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(48.f));
	ui_axis(ui, AXIS_X);
	ui_padd(ui, AXIS_X, 18.f, 18.f);
	ui_padd(ui, AXIS_Y, 8.f, 8.f);
	ui_gap(ui, 8.f);
	playground_begin_box(ui, 1, LIT(""), slate, false);
	UI_TextStyle vibrant = { .font = font, .size = 16, .color = color_srgba(0x18B8A4) };
	UI_TextStyle subtle = { .font = font, .size = 16, .color = color_srgba(0x8EAAA5) };
	UI_TextStyle running = { .font = font, .size = 16, .color = amber };
	playground_frame_slot_begin(ui, 1, AXIS_Y);
	ui_clean(ui);
	ui_align(ui, AXIS_Y, 0.5f);
	ui_text_box_string(ui, 1, vibrant, LIT("ORBITER"));
	ui_box_end(ui);
	playground_frame_slot_begin(ui, 2, AXIS_Y);
	ui_clean(ui);
	ui_align(ui, AXIS_Y, 0.5f);
	ui_text_box_string(ui, 1, subtle, LIT("|  UI BOX PLAYGROUND  |"));
	ui_box_end(ui);
	playground_frame_slot_begin(ui, 3, AXIS_Y);
	ui_clean(ui);
	ui_align(ui, AXIS_Y, 0.5f);
	ui_text_box_string(ui, 1, running, LIT("RUNNING"));
	ui_box_end(ui);
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_box_make(ui, 4, LIT(""));
	subtle.align.x = 1.f;
	playground_frame_slot_begin(ui, 5, AXIS_Y);
	ui_clean(ui);
	ui_align(ui, AXIS_Y, 0.5f);
	ui_text_box_sized_string(ui, 1, subtle, LIT("999.9 FPS  |  FRAME 9999999999"), LIT("60.0 FPS  |  FRAME 123456"));
	ui_box_end(ui);
	ui_box_end(ui);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_axis(ui, AXIS_X);
	ui_gap(ui, density.gap);
	ui_padd(ui, AXIS_Y, 32.f, 0.f);
	playground_begin_box(ui, 2, LIT("layout laboratory"), slate, false);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_flex(0.f, 3.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_min_size(ui, AXIS_X, 120.f);
	ui_max_size(ui, AXIS_X, 310.f);
	ui_padd(ui, AXIS_X, density.card_padding, density.card_padding);
	ui_padd(ui, AXIS_Y, 48.f, density.card_padding);
	ui_gap(ui, 8.f);
	UI_Box *navigation_box = playground_begin_box(ui, 10, LIT("CONTENT BASIS 270  |  SHRINK 3"), teal, true);
	navigation_box->intrinsic_size.x = 270.f;

	UI_Response overview_response = {};
	for (u32 row = 0; row < 4; ++row)
	{
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_grow(1.f));
		ui_size(ui, AXIS_Y, ui_fixed(38.f));
		UI_Box *item_box = playground_make_box(ui, 11 + row, row == 0 ? LIT("Overview") : row == 1 ? LIT("Call tree") : row == 2 ? LIT("Flame graph") : LIT("Memory"), color_srgba_mix(teal, slate, 0.45f), false);
		if (row == 0) overview_response = ui_signal_from_box(item_box);
	}
	if (overview_response.hovered)
	{
		UI_Box *tooltip = ui_tooltip_begin(ui, UI_KEY("overview tooltip"), ui->mouse);
		if (tooltip)
		{
			UI_TextStyle tooltip_title = vibrant;
			tooltip_title.align = v2(0.f, 0.f);
			UI_TextStyle tooltip_body = subtle;
			tooltip_body.align = v2(0.f, 0.f);
			ui_push_box_z(ui, UI_Z_OVERLAY);
			ui_clean(ui);
			ui_text_box_string(ui, 1, tooltip_title, LIT("OVERVIEW"));
			ui_clean(ui);
			ui_text_box_string(ui, 2, tooltip_body, LIT("This tooltip is an ordinary box subtree."));
			ui_clean(ui);
			ui_pop_box_z(ui);
			ui_tooltip_end(ui);
		}
	}
	ui_box_end(ui);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(2.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_min_size(ui, AXIS_X, 160.f);
	ui_padd(ui, AXIS_X, density.card_padding, density.card_padding);
	ui_padd(ui, AXIS_Y, 48.f, density.card_padding);
	ui_gap(ui, 10.f);
	playground_begin_box(ui, 20, LIT("ZERO BASIS  |  GROW 2"), blue, true);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_axis(ui, AXIS_X);
	ui_gap(ui, 7.f);
	playground_begin_box(ui, 21, LIT("weighted children"), color_srgba_mix(blue, slate, 0.55f), false);
	for (u32 bar = 0; bar < 5; ++bar)
	{
		ui_clean(ui);
		ui_layout(ui, &UI_FlatLayoutHooks);
		ui_size(ui, AXIS_X, ui_grow((f32)(bar + 1)));
		ui_size(ui, AXIS_Y, ui_grow(1.f));
		ui_box_begin(ui, 22 + bar, LIT("bar slot"));
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_grow(1.f));
		ui_size(ui, AXIS_Y, ui_fixed(54.f + 22.f * bar));
		ui_align(ui, AXIS_Y, 1.f);
		playground_make_box(ui, 1, LIT(""), color_srgba_mix(blue, violet, (f32)bar / 5.f), false);
		ui_box_end(ui);
	}
	ui_box_end(ui);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(36.f));
	playground_make_box(ui, 28, LIT("1x  2x  3x  4x  5x grow weights"), color_srgba_mix(blue, slate, 0.35f), false);
	ui_box_end(ui);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_flex(1.f, 1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_min_size(ui, AXIS_X, 180.f);
	ui_max_size(ui, AXIS_X, 420.f);
	UI_ScrollBox *scroll = ui_scroll_box_begin(ui, 30, AXIS_Y);

	PlaygroundProfilerRows *profiler_rows = arena_push_zero(arena, sizeof(*profiler_rows));
	profiler_rows->violet = violet;
	profiler_rows->slate = slate;
	profiler_rows->title_style = (UI_TextStyle) { .font = font, .size = 16, .color = color_srgba(0xD6E7E4) };
	profiler_rows->subtitle_style = (UI_TextStyle) { .font = font, .size = 14, .color = color_srgba(0x8EAAA5) };
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_padd(ui, AXIS_X, density.card_padding, density.card_padding);
	ui_padd(ui, AXIS_Y, 48.f, density.card_padding);
	ui_gap(ui, 8.f);
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	UI_Box *inspector_box = ui_virtual_list(ui, 1, LIT("VIRTUAL 10,000 ROWS  |  CONTENT BASIS 300"), (UI_VirtualListDesc) {
		.item_count = 10000,
		.user = profiler_rows,
		.build_item = playground_build_profiler_row,
	});
	inspector_box->user = playground_visual(arena, violet, true);
	ui_scroll_box_end(scroll);
	scroll->root->intrinsic_size.x = 300.f;
	scroll->track->user = playground_visual(arena, color_srgba_mix(violet, slate, 0.72f), false);
	scroll->thumb->user = playground_visual(arena, color_srgba(0xC99CFF), false);
	ui_clean(ui);

	ui_box_end(ui);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(42.f));
	ui_padd(ui, AXIS_X, 14.f, 14.f);
	ui_padd(ui, AXIS_Y, 8.f, 8.f);
	playground_make_box(ui, 40, LIT("Hover OVERVIEW for the box-built overlay. Mouse-wheel the purple card or drag its scrollbar."), color_srgba_mix(amber, slate, 0.55f), false);

	scene.root = ui_build_end(ui);
	return scene;
}
