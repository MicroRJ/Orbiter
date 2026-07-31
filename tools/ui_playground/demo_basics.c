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

	UI_BoxDesc header = playground_fill_desc();
	header.axis = AXIS_X;
	header.size[AXIS_Y] = ui_fixed(48.f);
	header.horz_padd[0] = header.horz_padd[1] = 18.f;
	header.vert_padd[0] = header.vert_padd[1] = 8.f;
	header.gap = 8.f;
	playground_begin_box(ui, 1, LIT(""), header, slate, false);
	UI_BoxDesc status_text = ui_defaults();
	status_text.perp_align = 0.5f;
	UI_TextStyle vibrant = { .font = font, .size = 16, .color = color_srgba(0x18B8A4) };
	UI_TextStyle subtle = { .font = font, .size = 16, .color = color_srgba(0x8EAAA5) };
	UI_TextStyle running = { .font = font, .size = 16, .color = amber };
	ui_text_box_string_desc(ui, 1, status_text, vibrant, LIT("ORBITER"));
	ui_text_box_string_desc(ui, 2, status_text, subtle, LIT("|  UI BOX PLAYGROUND  |"));
	ui_text_box_string_desc(ui, 3, status_text, running, LIT("RUNNING"));
	UI_BoxDesc status_spacer = playground_fill_desc();
	ui_box_make_desc(ui, 4, LIT(""), status_spacer);
	subtle.align.x = 1.f;
	ui_text_box_sized_string_desc(ui, 5, status_text, subtle, LIT("999.9 FPS  |  FRAME 9999999999"), LIT("60.0 FPS  |  FRAME 123456"));
	ui_box_end(ui);

	UI_BoxDesc laboratory = playground_fill_desc();
	laboratory.axis = AXIS_X;
	laboratory.gap = density.gap;
	laboratory.vert_padd[0] = 32.f;
	playground_begin_box(ui, 2, LIT("layout laboratory"), laboratory, slate, false);

	UI_BoxDesc navigation = playground_fill_desc();
	navigation.size[AXIS_X] = ui_flex(0.f, 3.f);
	navigation.min_size.x = 120.f;
	navigation.max_size.x = 310.f;
	navigation.horz_padd[0] = navigation.horz_padd[1] = density.card_padding;
	navigation.vert_padd[0] = 48.f;
	navigation.vert_padd[1] = density.card_padding;
	navigation.gap = 8.f;

	UI_Box *navigation_box = playground_begin_box(ui, 10, LIT("CONTENT BASIS 270  |  SHRINK 3"), navigation, teal, true);
	navigation_box->intrinsic_size.x = 270.f;

	UI_Response overview_response = {};
	for (u32 row = 0; row < 4; ++row)
	{
		UI_BoxDesc item = playground_fill_desc();
		item.size[AXIS_Y] = ui_fixed(38.f);
		UI_Box *item_box = playground_make_box(ui, 11 + row, row == 0 ? LIT("Overview") : row == 1 ? LIT("Call tree") : row == 2 ? LIT("Flame graph") : LIT("Memory"), item, color_srgba_mix(teal, slate, 0.45f), false);
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
			ui_text_box_string(ui, 1, tooltip_title, LIT("OVERVIEW"));
			ui_text_box_string(ui, 2, tooltip_body, LIT("This tooltip is an ordinary box subtree."));
			ui_tooltip_end(ui);
		}
	}
	ui_box_end(ui);

	UI_BoxDesc timeline = playground_fill_desc();
	timeline.size[AXIS_X] = ui_grow(2.f);
	timeline.min_size.x = 160.f;
	timeline.horz_padd[0] = timeline.horz_padd[1] = density.card_padding;
	timeline.vert_padd[0] = 48.f;
	timeline.vert_padd[1] = density.card_padding;
	timeline.gap = 10.f;
	playground_begin_box(ui, 20, LIT("ZERO BASIS  |  GROW 2"), timeline, blue, true);

	UI_BoxDesc chart = playground_fill_desc();
	chart.axis = AXIS_X;
	chart.gap = 7.f;
	playground_begin_box(ui, 21, LIT("weighted children"), chart, color_srgba_mix(blue, slate, 0.55f), false);
	for (u32 bar = 0; bar < 5; ++bar)
	{
		UI_BoxDesc bar_desc = playground_fill_desc();
		bar_desc.size[AXIS_X] = ui_grow((f32)(bar + 1));
		bar_desc.size[AXIS_Y] = ui_fixed(54.f + 22.f * bar);
		bar_desc.perp_align = 1.f;
		playground_make_box(ui, 22 + bar, LIT(""), bar_desc, color_srgba_mix(blue, violet, (f32)bar / 5.f), false);
	}
	ui_box_end(ui);

	UI_BoxDesc timeline_status = playground_fill_desc();
	timeline_status.size[AXIS_Y] = ui_fixed(36.f);
	playground_make_box(ui, 28, LIT("1x  2x  3x  4x  5x grow weights"), timeline_status, color_srgba_mix(blue, slate, 0.35f), false);
	ui_box_end(ui);

	UI_BoxDesc inspector_area = playground_fill_desc();
	inspector_area.size[AXIS_X] = ui_flex(1.f, 1.f);
	inspector_area.min_size.x = 180.f;
	inspector_area.max_size.x = 420.f;
	PlaygroundScrollArea scroll_area = playground_scroll_area_begin(ui, 30, inspector_area);

	UI_BoxDesc inspector = playground_fill_desc();
	inspector.horz_padd[0] = inspector.horz_padd[1] = density.card_padding;
	inspector.vert_padd[0] = 48.f;
	inspector.vert_padd[1] = density.card_padding;
	inspector.gap = 8.f;
	inspector.overflow[AXIS_X] = UI_BOX_OVERFLOW_CLIP;
	inspector.overflow[AXIS_Y] = UI_BOX_OVERFLOW_SCROLL;
	PlaygroundProfilerRows *profiler_rows = arena_push_zero(arena, sizeof(*profiler_rows));
	profiler_rows->violet = violet;
	profiler_rows->slate = slate;
	profiler_rows->title_style = (UI_TextStyle) { .font = font, .size = 16, .color = color_srgba(0xD6E7E4) };
	profiler_rows->subtitle_style = (UI_TextStyle) { .font = font, .size = 14, .color = color_srgba(0x8EAAA5) };
	UI_Box *inspector_box = ui_box_make_virtual_list_desc(ui, 1, LIT("VIRTUAL 10,000 ROWS  |  CONTENT BASIS 300"), inspector, (UI_BoxVirtualListDesc) {
		.item_count = 10000,
		.user = profiler_rows,
		.build_item = playground_build_profiler_row,
	});
	inspector_box->user = playground_visual(arena, violet, true);
	scroll_area.root->intrinsic_size.x = 300.f;
	playground_scroll_area_end(ui, &scroll_area, inspector_box, color_srgba_mix(violet, slate, 0.72f), color_srgba(0xC99CFF));
	scene.scroll_areas[scene.scroll_area_count++] = scroll_area;

	ui_box_end(ui);

	UI_BoxDesc footer = playground_fill_desc();
	footer.size[AXIS_Y] = ui_fixed(42.f);
	footer.horz_padd[0] = footer.horz_padd[1] = 14.f;
	footer.vert_padd[0] = footer.vert_padd[1] = 8.f;
	playground_make_box(ui, 40, LIT("Hover OVERVIEW for the box-built overlay. Mouse-wheel the purple card or drag its scrollbar."), footer, color_srgba_mix(amber, slate, 0.55f), false);

	scene.root = ui_build_end(ui);
	return scene;
}
