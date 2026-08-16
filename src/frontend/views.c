#include "app_window.h"
#include "ui_widgets.h"
#include "views.h"

const ViewDesc view_descs[] = {
	{ .name = "video",        .title = "VIDEO",          .hotkey = OS_Key_1, .requirements = VIEW_REQUIRE_ACTIVE_GAME, .build_ui = video_view_build_ui },
	{ .name = "program",      .title = "PROGRAM",        .hotkey = OS_Key_2, .requirements = VIEW_REQUIRE_ACTIVE_GAME, .build_ui = program_view_build_ui },
	{ .name = "cpu",          .title = "CPU",            .hotkey = OS_Key_3, .requirements = VIEW_REQUIRE_ACTIVE_GAME, .build_ui = cpu_view_build_ui },
	{ .name = "profiler",     .title = "PROFILER",       .hotkey = OS_Key_4, .requirements = VIEW_REQUIRE_NONE,        .build_ui = profiler_view_build_ui },
	{ .name = "prg_activity", .title = "EXECUTION FLOW", .hotkey = OS_Key_5, .requirements = VIEW_REQUIRE_ACTIVE_GAME, .build_ui = prg_activity_view_build_ui },
	{ .name = "chr_map",      .title = "CHR MAP",        .hotkey = OS_Key_6, .requirements = VIEW_REQUIRE_ACTIVE_GAME, .build_ui = chr_map_view_build_ui },
};

const u32 view_desc_count = ArrayCount(view_descs);

ViewFrameData view_begin_frame(ViewFrameData *frame, Str title)
{
	UI_Context *ui = frame->ui;
	ViewFrameData result = *frame;
	f32 height = ui->theme.code.size + 10.f;
	result.header_height = height;


	ui_clean(ui);
	ui_inset_shadow(ui, 0.25f);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_CLIP);
	result.frame_box = ui_box_begin(ui, UI_KEY("view frame"), LIT("view frame"));

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(height + 24.f));
	ui_padd(ui, AXIS_X, 12.f, 12.f);
	ui_padd(ui, AXIS_Y, 12.f, 12.f);
	ui_box_begin(ui, 1, LIT("view header slot"));

	ui_clean(ui);
	ui_axis(ui, AXIS_X);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_padd(ui, AXIS_X, 8.f, 0.f);
	ui_padd(ui, AXIS_Y, 3.f, 0.f);
	ui_backdrop(ui, 5.f);
	ui_paint_z(ui, UI_Z_HEADER);
	ui_box_begin(ui, 1, LIT("view header"));

	UI_TextStyle style = ui->theme.code;
	style.color = ui->theme.text_vibrant;
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_emission(ui, 0.15f);
	ui_paint_z(ui, UI_Z_HEADER);
	ui_text(ui, 1, style, title);
	ui_box_end(ui);
	ui_box_end(ui);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_CLIP);
	result.content_box = ui_box_begin(ui, 2, LIT("view content"));
	return result;
}

void view_end_frame(ViewFrameData *frame)
{
	Assert(frame->frame_box);
	Assert(frame->content_box);
	Assert(frame->content_box->parent == frame->frame_box);
	ui_box_end(frame->ui);
	ui_box_end(frame->ui);
}

static void view_build_active_game_required(ViewFrameData *frame)
{
	Assert(frame->window);
	const ViewDesc *desc = frame->view->desc;
	ViewFrameData content = view_begin_frame(frame, str_from_cstr(desc->title));
	UI_Context *ui = frame->ui;

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_begin_flat(ui, UI_KEY("game required"));

	ui_clean(ui);
	ui_axis(ui, AXIS_Y);
	ui_size(ui, AXIS_X, ui_wrap());
	ui_size(ui, AXIS_Y, ui_wrap());
	ui_align(ui, AXIS_X, 0.5f);
	ui_align(ui, AXIS_Y, 0.5f);
	ui_gap(ui, 8.f);
	ui_box_begin(ui, 1, LIT("game required message"));

	UI_TextStyle title = ui->theme.code;
	title.size += 4;
	title.color = ui->theme.text_neutral;
	title.align.x = 0.5f;
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_wrap());
	ui_text(ui, 1, title, LIT("NO GAME LOADED"));

	UI_TextStyle detail = ui->theme.code;
	detail.color = ui->theme.text_subtle;
	detail.align.x = 0.5f;
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_wrap());
	ui_size(ui, AXIS_Y, ui_wrap());
	ui_text(ui, 2, detail, LIT("Open the library to choose or import a game."));

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_wrap());
	ui_begin_flat(ui, 3);
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_wrap());
	ui_size(ui, AXIS_Y, ui_wrap());
	ui_align(ui, AXIS_X, 0.5f);
	UI_Response open = ui_button(ui, 1, LIT("OPEN LIBRARY  TAB"));
	ui_box_end(ui);

	ui_box_end(ui);
	ui_box_end(ui);
	view_end_frame(&content);
	if (open.pressed) app_window_emit_action(frame->window, (App_Action) { .kind = APP_ACTION_SHOW_LIBRARY_OVERLAY });
}

void view_build_ui(ViewFrameData *frame)
{
	Assert(frame);
	Assert(frame->view);
	Assert(frame->view->desc);
	Assert(frame->view->desc->build_ui);
	const ViewDesc *desc = frame->view->desc;
	Assert(!(desc->requirements & ~VIEW_REQUIRE_ACTIVE_GAME));
	b32 active_game = frame->emulator && frame->publication && nes_emulator_ready_to_run(frame->emulator) && frame->publication->valid;
	if ((desc->requirements & VIEW_REQUIRE_ACTIVE_GAME) && !active_game)
	{
		view_build_active_game_required(frame);
		return;
	}
	frame->view->desc->build_ui(frame);
}
