#include "nes_process.h"
#include "ui_widgets.h"
#include "views.h"

void video_view_build_ui(ViewFrameData *frame)
{
	Assert(frame->video_texture);
	ViewFrameData content = view_begin_frame(frame, LIT("VIDEO"));
	UI_ImageStyle image_style = ui_default_image_style();
	image_style.region = (rect_i32) { 0, 0, NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT };
	ui_clean(frame->ui);
	ui_size(frame->ui, AXIS_X, ui_grow(1.f));
	ui_size(frame->ui, AXIS_Y, ui_grow(1.f));
	ui_padd(frame->ui, AXIS_X, 12.f, 12.f);
	ui_padd(frame->ui, AXIS_Y, 12.f, 12.f);
	ui_image_box(frame->ui, 1, image_style, frame->video_texture);
	view_end_frame(&content);
}
