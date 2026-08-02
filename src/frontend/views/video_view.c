#include "debugger.h"
#include "ui_widgets.h"
#include "views.h"

void video_view_build_ui(ViewFrameData *frame)
{
	Assert(frame->video_texture);
	ViewFrameData content = view_begin_frame(frame, LIT("VIDEO"));
	UI_BoxDesc image_desc = ui_defaults();
	image_desc.size[AXIS_X] = ui_grow(1.f);
	image_desc.size[AXIS_Y] = ui_grow(1.f);
	image_desc.horz_padd[0] = image_desc.horz_padd[1] = 12.f;
	image_desc.vert_padd[0] = image_desc.vert_padd[1] = 12.f;
	UI_ImageStyle image_style = ui_default_image_style();
	image_style.region = (rect_i32) { 0, 0, NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT };
	ui_image_box_desc(frame->ui, 1, image_desc, image_style, frame->video_texture);
	view_end_frame(&content);
}
