// Included by main.c after the shared playground types and helpers.

static PlaygroundScene playground_build_scroll_history(Arena *arena, UI_Context *ui, Font_Handle font, PlaygroundDensity density, b32 reset_scroll)
{
	PlaygroundScene scene = {};
	Color_SRGBA teal = color_srgba(0x18B8A4);
	Color_SRGBA violet = color_srgba(0x9558E8);
	Color_SRGBA amber = color_srgba(0xE5A83C);
	Color_SRGBA slate = color_srgba(0x25343A);
	Color_SRGBA panel = color_srgba_mix(slate, color_srgba(0x071013), 0.35f);
	Color_SRGBA text_color = color_srgba(0xD6E7E4);
	Color_SRGBA subtle_color = color_srgba(0x8EAAA5);
	UI_TextStyle title = { .font = font, .size = 16, .color = teal };
	UI_TextStyle text = { .font = font, .size = 14, .color = text_color };
	UI_TextStyle subtle = { .font = font, .size = 14, .color = subtle_color };
	UI_TextStyle accent = { .font = font, .size = 14, .color = amber };

	UI_BoxDesc root_desc = playground_fill_desc();
	root_desc.axis = AXIS_Y;
	root_desc.horz_padd[0] = root_desc.horz_padd[1] = density.outer_padding;
	root_desc.vert_padd[0] = root_desc.vert_padd[1] = density.outer_padding;
	root_desc.gap = density.gap;
	UI_BoxBuilder builder;
	ui_box_builder_begin(&builder, arena, ui, UI_KEY("scroll history playground"), LIT("scroll history playground"), root_desc);

	UI_BoxDesc header = playground_fill_desc();
	header.axis = AXIS_X;
	header.size[AXIS_Y] = ui_box_pixels(52.f);
	header.horz_padd[0] = header.horz_padd[1] = 16.f;
	header.vert_padd[0] = header.vert_padd[1] = 8.f;
	header.gap = 8.f;
	playground_begin_box(&builder, 7000, LIT(""), header, slate, false);
	UI_BoxDesc header_text = ui_box_desc();
	header_text.perp_align = 0.5f;
	ui_text_box_string_desc(&builder, 1, header_text, title, LIT("SCROLL HISTORY LAB"));
	ui_text_box_string_desc(&builder, 2, header_text, subtle, LIT("|  ONE VIRTUAL LIST + ONE BOX SCROLLBAR"));
	UI_BoxDesc spacer = playground_fill_desc();
	ui_box_make_desc(&builder, 3, LIT(""), spacer);
	UI_TextStyle status = title;
	status.align.x = 1.f;
	ui_text_box_string_desc(&builder, 4, header_text, status, LIT("CONTEXT-OWNED STATE"));
	ui_box_end(&builder);

	UI_BoxDesc body = playground_fill_desc();
	body.axis = AXIS_X;
	body.gap = density.gap;
	ui_box_begin_desc(&builder, 7001, LIT(""), body);

	ui_push(&builder);
	ui_size(&builder, AXIS_X, ui_box_fill(1.f));
	ui_size(&builder, AXIS_Y, ui_box_fill(1.f));
	ui_min_size(&builder, AXIS_X, 360.f);
	UI_Scroll *scroll = ui_scroll_begin(&builder, 7100, AXIS_Y);
	if (reset_scroll) ui_scroll_reset(scroll);

	ui_size(&builder, AXIS_X, ui_box_fill(1.f));
	ui_size(&builder, AXIS_Y, ui_box_fill(1.f));
	ui_padd(&builder, AXIS_X, density.card_padding, density.card_padding);
	ui_padd(&builder, AXIS_Y, density.card_padding, density.card_padding);
	ui_gap(&builder, 6.f);
	ui_overflow(&builder, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	PlaygroundProfilerRows *rows = arena_push_zero(arena, sizeof(*rows));
	rows->violet = violet;
	rows->slate = slate;
	rows->title_style = (UI_TextStyle) { .font = font, .size = 16, .color = text_color };
	rows->subtitle_style = subtle;
	UI_Box *list_box = ui_box_make_virtual_list(&builder, 1, LIT("50,000 FIXED-EXTENT BOX SUBTREES"), (UI_BoxVirtualListDesc) {
		.item_count = 50000,
		.user = rows,
		.build_item = playground_build_profiler_row,
	});
	list_box->user = playground_visual(arena, violet, true);
	ui_scroll_end(scroll);
	scroll->track->user = playground_visual(arena, color_srgba_mix(violet, slate, 0.72f), false);
	scroll->thumb->user = playground_visual(arena, color_srgba(0xC99CFF), false);
	ui_pop(&builder);

	UI_ScrollGeometry *previous = &scroll->previous;
	b32 input_ready = scroll->has_previous;

	UI_BoxDesc state_panel = playground_fill_desc();
	state_panel.axis = AXIS_Y;
	state_panel.size[AXIS_X] = ui_box_pixels(330.f);
	state_panel.horz_padd[0] = state_panel.horz_padd[1] = density.card_padding;
	state_panel.vert_padd[0] = state_panel.vert_padd[1] = density.card_padding;
	state_panel.gap = 8.f;
	playground_begin_box(&builder, 7200, LIT(""), state_panel, panel, false);

	UI_BoxDesc line = playground_fill_desc();
	line.size[AXIS_Y] = ui_box_pixels(22.f);
	ui_text_box_string_desc(&builder, 1, line, title, LIT("PERSISTENT STATE"));
	ui_text_box_string_desc(&builder, 2, line, input_ready ? title : accent, input_ready ? LIT("input source: previous frame") : LIT("input source: unavailable"));
	ui_text_box_string_desc(&builder, 3, line, text, push_formatted(arena, "offset          %9.2f", scroll->offset));
	ui_text_box_string_desc(&builder, 4, line, text, push_formatted(arena, "target          %9.2f", scroll->target));
	ui_text_box_string_desc(&builder, 5, line, subtle, push_formatted(arena, "generation      %9llu", ui->layout_generation));
	ui_text_box_string_desc(&builder, 6, line, subtle, push_formatted(arena, "cached gen      %9llu", previous->layout_generation));
	ui_text_box_string_desc(&builder, 7, line, text, push_formatted(arena, "viewport h      %9.2f", previous->viewport.h));
	ui_text_box_string_desc(&builder, 8, line, text, push_formatted(arena, "content h       %9.2f", previous->content_size.y));
	ui_text_box_string_desc(&builder, 9, line, text, push_formatted(arena, "scroll range    %9.2f", previous->scroll_max.y - previous->scroll_min.y));
	ui_text_box_string_desc(&builder, 10, line, text, push_formatted(arena, "track y / h  %7.2f / %7.2f", previous->track.y, previous->track.h));
	ui_text_box_string_desc(&builder, 11, line, text, push_formatted(arena, "thumb y / h  %7.2f / %7.2f", previous->thumb.y, previous->thumb.h));

	UI_BoxDesc divider = playground_fill_desc();
	divider.size[AXIS_Y] = ui_box_pixels(2.f);
	playground_make_box(&builder, 12, LIT(""), divider, color_srgba_mix(teal, panel, 0.35f), false);
	ui_text_box_string_desc(&builder, 13, line, title, LIT("FRAME PIPELINE"));

	static const String pipeline[] = {
		LIT("1  ui_scroll_begin finds context state"),
		LIT("2  ui_scroll_end handles old geometry"),
		LIT("3  current box tree lays out once"),
		LIT("4  layout commits geometry to context"),
	};
	for (u32 pipeline_index = 0; pipeline_index < ArrayCount(pipeline); pipeline_index ++) {
		ui_text_box_string_desc(&builder, 14 + pipeline_index, line, pipeline_index == 2 ? accent : subtle, pipeline[pipeline_index]);
	}
	ui_box_end(&builder);
	ui_box_end(&builder);

	UI_BoxDesc footer = playground_fill_desc();
	footer.size[AXIS_Y] = ui_box_pixels(42.f);
	footer.horz_padd[0] = footer.horz_padd[1] = 14.f;
	footer.vert_padd[0] = footer.vert_padd[1] = 8.f;
	playground_make_box(&builder, 7300, LIT("WHEEL / DRAG  |  R: RESET HISTORY  |  TAB: NEXT MODE  |  SPACE: DENSITY"), footer, color_srgba_mix(amber, slate, 0.55f), false);

	scene.root = ui_box_builder_end(&builder);
	return scene;
}
