#include "debugger.h"
#include "views.h"

static void video_view_content(ViewFrameData *frame)
{
	Assert(frame->video_texture);

	rect_i32 region = { 0, 0, NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT };
	rect_f32 container_rect = rect_f32_inset(frame->rect, 12);
	f32 scale = Min(container_rect.size.x / (f32)region.size.x, container_rect.size.y / (f32)region.size.y);
	rect_f32 centered = rect_f32_align(container_rect, v2(region.size.x * scale, region.size.y * scale), v2(0.5f, 0.5f));
	rect_f32 image_rect = rect_f32_round_out(centered);
	ui_draw_image(frame->ui, (Draw_TextureParams) {
		.rect = image_rect,
		.texture = frame->video_texture,
		.region = region,
		.tint = COLOR_WHITE,
	});
}

void video_view_frame(ViewFrameData *frame)
{
	ViewFrameData content = view_begin_frame(frame, LIT("VIDEO"));
	video_view_content(&content);
	view_end_frame(&content);
}
