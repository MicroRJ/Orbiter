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
	ui_build_begin(ui, UI_KEY("scroll history playground"), LIT("scroll history playground"), root_desc);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(52.f));
	ui_axis(ui, AXIS_X);
	ui_padd(ui, AXIS_X, 16.f, 16.f);
	ui_padd(ui, AXIS_Y, 8.f, 8.f);
	ui_gap(ui, 8.f);
	playground_begin_box(ui, 7000, LIT(""), slate, false);
	playground_frame_slot_begin(ui, 1, AXIS_Y);
	ui_clean(ui);
	ui_align(ui, AXIS_Y, 0.5f);
	ui_text_box_string(ui, 1, title, LIT("SCROLL HISTORY LAB"));
	ui_box_end(ui);
	playground_frame_slot_begin(ui, 2, AXIS_Y);
	ui_clean(ui);
	ui_align(ui, AXIS_Y, 0.5f);
	ui_text_box_string(ui, 1, subtle, LIT("|  ONE VIRTUAL LIST + ONE BOX SCROLLBAR"));
	ui_box_end(ui);
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_box_make(ui, 3, LIT(""));
	UI_TextStyle status = title;
	status.align.x = 1.f;
	playground_frame_slot_begin(ui, 4, AXIS_Y);
	ui_clean(ui);
	ui_align(ui, AXIS_Y, 0.5f);
	ui_text_box_string(ui, 1, status, LIT("CONTEXT-OWNED STATE"));
	ui_box_end(ui);
	ui_clean(ui);
	UI_Response reset_button = ui_button(ui, UI_KEY("reset scroll"), LIT("RESET"));
	ui_box_end(ui);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_axis(ui, AXIS_X);
	ui_gap(ui, density.gap);
	ui_box_begin(ui, 7001, LIT(""));

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_min_size(ui, AXIS_X, 360.f);
	UI_ScrollBox *scroll = ui_scroll_box_begin(ui, 7100, AXIS_Y);
	if (reset_scroll || reset_button.pressed) ui_scroll_box_reset(scroll);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_padd(ui, AXIS_X, density.card_padding, density.card_padding);
	ui_padd(ui, AXIS_Y, density.card_padding, density.card_padding);
	ui_gap(ui, 6.f);
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	PlaygroundProfilerRows *rows = arena_push_zero(arena, sizeof(*rows));
	rows->violet = violet;
	rows->slate = slate;
	rows->title_style = (UI_TextStyle) { .font = font, .size = 16, .color = text_color };
	rows->subtitle_style = subtle;
	UI_Box *list_box = ui_virtual_list(ui, 1, LIT("50,000 FIXED-EXTENT BOX SUBTREES"), (UI_VirtualListDesc) {
		.item_count = 50000,
		.user = rows,
		.build_item = playground_build_profiler_row,
	});
	list_box->user = playground_visual(arena, violet, true);
	ui_scroll_box_end(scroll);
	scroll->track->user = playground_visual(arena, color_srgba_mix(violet, slate, 0.72f), false);
	scroll->thumb->user = playground_visual(arena, color_srgba(0xC99CFF), false);
	ui_clean(ui);

	UI_BoxState *viewport_previous = scroll->viewport->state;
	UI_BoxState *track_previous = scroll->track->state;
	UI_BoxState *thumb_previous = scroll->thumb->state;
	b32 input_ready = scroll->has_previous;

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_fixed(330.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_axis(ui, AXIS_Y);
	ui_padd(ui, AXIS_X, density.card_padding, density.card_padding);
	ui_padd(ui, AXIS_Y, density.card_padding, density.card_padding);
	ui_gap(ui, 8.f);
	playground_begin_box(ui, 7200, LIT(""), panel, false);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(22.f));
	ui_text_box_string(ui, 1, title, LIT("PERSISTENT STATE"));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(22.f));
	ui_text_box_string(ui, 2, input_ready ? title : accent, input_ready ? LIT("input source: previous frame") : LIT("input source: unavailable"));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(22.f));
	ui_text_box_string(ui, 3, text, str_push_copy_f(arena, "offset          %9.2f", scroll->offset));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(22.f));
	ui_text_box_string(ui, 4, text, str_push_copy_f(arena, "target          %9.2f", scroll->target));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(22.f));
	ui_text_box_string(ui, 5, subtle, str_push_copy_f(arena, "generation      %9llu", ui->layout_generation));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(22.f));
	ui_text_box_string(ui, 6, subtle, str_push_copy_f(arena, "cached gen      %9llu", viewport_previous->layout_generation));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(22.f));
	ui_text_box_string(ui, 7, text, str_push_copy_f(arena, "viewport h      %9.2f", viewport_previous->viewport.h));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(22.f));
	ui_text_box_string(ui, 8, text, str_push_copy_f(arena, "content h       %9.2f", viewport_previous->content_size.y));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(22.f));
	ui_text_box_string(ui, 9, text, str_push_copy_f(arena, "scroll range    %9.2f", Max(viewport_previous->content_size.y - viewport_previous->viewport.h, 0.f)));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(22.f));
	ui_text_box_string(ui, 10, text, str_push_copy_f(arena, "track y / h  %7.2f / %7.2f", track_previous->rect.y, track_previous->rect.h));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(22.f));
	ui_text_box_string(ui, 11, text, str_push_copy_f(arena, "thumb y / h  %7.2f / %7.2f", thumb_previous->rect.y, thumb_previous->rect.h));

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(2.f));
	playground_make_box(ui, 12, LIT(""), color_srgba_mix(teal, panel, 0.35f), false);
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(22.f));
	ui_text_box_string(ui, 13, title, LIT("FRAME PIPELINE"));

	static const Str pipeline[] = {
		LIT("1  boxes recover context-owned state"),
		LIT("2  box signals handle old geometry"),
		LIT("3  current box tree lays out once"),
		LIT("4  layout commits geometry to context"),
	};
	for (u32 pipeline_index = 0; pipeline_index < ArrayCount(pipeline); pipeline_index ++) {
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_grow(1.f));
		ui_size(ui, AXIS_Y, ui_fixed(22.f));
		ui_text_box_string(ui, 14 + pipeline_index, pipeline_index == 2 ? accent : subtle, pipeline[pipeline_index]);
	}
	ui_box_end(ui);
	ui_box_end(ui);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(42.f));
	ui_padd(ui, AXIS_X, 14.f, 14.f);
	ui_padd(ui, AXIS_Y, 8.f, 8.f);
	playground_make_box(ui, 7300, LIT("WHEEL / DRAG  |  R: RESET HISTORY  |  TAB: NEXT MODE  |  SPACE: DENSITY"), color_srgba_mix(amber, slate, 0.55f), false);

	scene.root = ui_build_end(ui);
	return scene;
}
