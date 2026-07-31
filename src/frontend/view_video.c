#include "debugger.h"
#include "views.h"

static void video_view_box_paint(UI_Box *box)
{
	GFX_Texture *texture = box->user;
	Assert(texture);

	rect_i32 region = { 0, 0, NES_VIDEO_WIDTH, NES_VIDEO_HEIGHT };
	rect_f32 container_rect = rect_f32_inset(box->viewport, 12);
	f32 scale = Min(container_rect.size.x / (f32)region.size.x, container_rect.size.y / (f32)region.size.y);
	rect_f32 centered = rect_f32_align(container_rect, v2(region.size.x * scale, region.size.y * scale), v2(0.5f, 0.5f));
	rect_f32 image_rect = rect_f32_round_out(centered);
	ui_push_clip(box->ui, box->viewport);
	ui_draw_image(box->ui, (Draw_TextureParams) {
		.rect = image_rect,
		.texture = texture,
		.region = region,
		.tint = COLOR_WHITE,
	});
	ui_pop_clip(box->ui);
}

static const UI_BoxHooks video_view_box_hooks = {
	.paint = video_view_box_paint,
};

void video_view_build_ui(ViewFrameData *frame)
{
	Assert(frame->video_texture);
	ViewFrameData content = view_begin_frame(frame, LIT("VIDEO"));
	content.content_box->ops = &video_view_box_hooks;
	content.content_box->user = frame->video_texture;
	view_end_frame(&content);
}
