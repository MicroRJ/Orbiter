#include "ui_widgets.h"
#include "views.h"

const ViewDesc view_descs[] = {
	{ "video",        OS_Key_1, video_view_build_ui },
	{ "program",      OS_Key_2, program_view_build_ui },
	{ "cpu",          OS_Key_3, cpu_view_build_ui },
	{ "profiler",     OS_Key_4, profiler_view_build_ui },
	{ "prg_activity", OS_Key_5, prg_activity_view_build_ui },
	{ "chr_map",      OS_Key_6, chr_map_view_build_ui },
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

void view_build_ui(ViewFrameData *frame)
{
	Assert(frame);
	Assert(frame->view);
	Assert(frame->view->desc);
	Assert(frame->view->desc->build_ui);
	frame->view->desc->build_ui(frame);
}

// if (!frame->publication->valid || !nes_emulator_ready_to_run(frame->emulator))
// {
// 	UI_TextStyle style = frame->ui->theme.code;
// 	style.color = frame->ui->theme.text_subtle;
// 	ui_clean(frame->ui);
// 	ui_size(frame->ui, AXIS_X, ui_grow(1.f));
// 	ui_size(frame->ui, AXIS_Y, ui_grow(1.f));
// 	ui_begin_flat(frame->ui, 1);
// 	ui_clean(frame->ui);
// 	ui_align(frame->ui, AXIS_X, 0.5f);
// 	ui_align(frame->ui, AXIS_Y, 0.5f);
// 	ui_text(frame->ui, 1, style, LIT("No Game Loaded"));
// 	ui_box_end(frame->ui);
// 	return;
// }
