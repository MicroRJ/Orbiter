#include "views.h"

ViewFrameData view_begin_frame(ViewFrameData *frame, String title)
{
	UI_Context *ui = frame->ui;
	ViewFrameData result = *frame;
	f32 height = ui->theme.code.size + 10.f;
	result.header_height = height;

	UI_BoxDesc frame_desc = ui_defaults();
	frame_desc.size[AXIS_X] = ui_grow(1.f);
	frame_desc.size[AXIS_Y] = ui_grow(1.f);
	frame_desc.overflow[AXIS_X] = UI_BOX_OVERFLOW_CLIP;
	frame_desc.overflow[AXIS_Y] = UI_BOX_OVERFLOW_CLIP;
	ui_push(ui);
	ui_inset_shadow(ui, 0.25f);
	result.frame_box = ui_box_begin_desc(ui, UI_KEY("view frame"), LIT("view frame"), frame_desc);
	ui_pop(ui);

	rect_f32 header = rect_f32_inset(rect_f32_slice(&result.rect, AXIS_Y, height + 24.f), 12.f);
	ui_push_z(ui, UI_Z_HEADER);
	ui_draw_backdrop(ui, header, 5.f);
	UI_TextStyle style = ui->theme.code;
	style.color = ui->theme.text_vibrant;
	header.x += 8.f;
	header.y += 3.f;
	ui_push_emission(ui, 0.15f);
	ui_draw_text(ui, header, style, title);
	ui_pop_emission(ui);
	ui_pop_z(ui);

	ui_push(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(height + 24.f));
	ui_box_make(ui, 1, LIT("view header space"));
	ui_pop(ui);

	ui_push(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_CLIP);
	result.content_box = ui_box_begin(ui, 2, LIT("view content"));
	ui_pop(ui);

	ui_push_clip(ui, frame->rect);
	return result;
}

void view_end_frame(ViewFrameData *frame)
{
	Assert(frame->frame_box);
	Assert(frame->content_box);
	Assert(frame->content_box->parent == frame->frame_box);
	ui_box_end(frame->ui);
	ui_box_end(frame->ui);
	ui_pop_clip(frame->ui);
}

void view_build_ui(ViewFrameData *frame)
{
	Assert(frame);
	Assert(frame->view);
	switch (frame->view->kind)
	{
		case VIEW_VIDEO: video_view_build_ui(frame); break;
		case VIEW_PROGRAM: program_view_build_ui(frame); break;
		case VIEW_CPU: cpu_view_build_ui(frame); break;
		case VIEW_PROFILER: profiler_view_build_ui(frame); break;
		case VIEW_PRG_ACTIVITY: prg_activity_view_build_ui(frame); break;
		case VIEW_CHR_MAP: chr_map_view_build_ui(frame); break;
		case VIEW_NONE:
		case VIEW_COUNT: Assert(false); break;
	}
}
