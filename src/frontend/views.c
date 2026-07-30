#include "views.h"

ViewFrameData view_begin_frame(ViewFrameData *frame, String title)
{
	UI_Context *ui = frame->ui;
	ViewFrameData result = *frame;
	f32 height = ui->theme.code.size + 10.f;
	result.header_height = height;

	ui_draw_inset_shadow(ui, result.rect, 0.25f);
	rect_f32 header = rect_f32_inset(rect_f32_slice(&result.rect, AXIS_Y, height + 24.f), 12.f);
	ui_push_z(ui, UI_Z_HEADER);
	ui_draw_backdrop(ui, header);
	UI_TextStyle style = ui->theme.code;
	style.color = ui->theme.text_vibrant;
	header.x += 8.f;
	header.y += 3.f;
	ui_push_emission(ui, 0.15f);
	ui_draw_text(ui, header, style, title);
	ui_pop_emission(ui);
	ui_pop_z(ui);
	ui_push_clip(ui, frame->rect);
	return result;
}

void view_end_frame(ViewFrameData *frame)
{
	ui_pop_clip(frame->ui);
}

void view_frame(ViewFrameData *frame)
{
	Assert(frame);
	Assert(frame->view);
	switch (frame->view->kind)
	{
		case VIEW_VIDEO: video_view_frame(frame); break;
		case VIEW_PROGRAM: program_view_frame(frame); break;
		case VIEW_CPU: cpu_view_frame(frame); break;
		case VIEW_PROFILER: profiler_view_frame(frame); break;
		case VIEW_CPU_MAPPING: cpu_mapping_view_frame(frame); break;
		case VIEW_PRG_ACTIVITY: prg_activity_view_frame(frame); break;
		case VIEW_CHR_MAP: chr_map_view_frame(frame); break;
		case VIEW_NONE:
		case VIEW_COUNT: Assert(false); break;
	}
}
