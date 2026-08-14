#include "base.h"
#include "graphics.h"
#include "text.h"
#include "text_gfx.h"

typedef struct
{
	GFX_Texture *texture;
	u64 uploaded_revision;
}
Text_GFXPage;

struct Text_GFX
{
	GFX_Renderer *renderer;
	Text_Context *text;
	Text_GFXPage pages[TEXT_RASTER_PAGE_LIMIT];
};

Text_GFX *text_gfx_create(Arena *owner, GFX_Renderer *renderer, Text_Context *text)
{
	Assert(owner);
	Assert(renderer);
	Assert(text);
	Text_GFX *gfx = arena_push_zero(owner, sizeof(*gfx));
	gfx->renderer = renderer;
	gfx->text = text;
	return gfx;
}

static GFX_Texture *text_gfx_page_texture(Text_GFX *gfx, Text_PageId id)
{
	Assert(id < text_raster_page_count(gfx->text));
	Assert(id < ArrayCount(gfx->pages));
	Text_GFXPage *gpu_page = &gfx->pages[id];
	if (!gpu_page->texture)
	{
		Text_RasterPage page = text_raster_page(gfx->text, id);
		gpu_page->texture = gfx_create_texture(gfx->renderer, (GFX_TextureDesc) {
			.usage = GRAPHICS_TEXTURE_USAGE_RARE_UPDATES,
			.bind_flags = GFX_TEXTURE_BIND_INPUT,
			.format = GRAPHICS_FORMAT_R_U8,
			.size = page.size,
			.sampler = GRAPHICS_SAMPLER_POINT,
			.label = "text raster page",
		});
	}
	return gpu_page->texture;
}

void text_gfx_draw_run(Text_GFX *gfx, Draw_Context *draw, Text_DrawRun run, vec2 position, Color_SRGBA color)
{
	Assert(gfx);
	position.x = roundf(position.x);
	position.y = roundf(position.y);
	for (Text_DrawPass *pass = run.passes; pass; pass = pass->next)
	{
		draw_mask_rects(draw, (Draw_MaskRectsParams) {
			.texture = text_gfx_page_texture(gfx, pass->page),
			.rects = pass->quads,
			.rect_count = pass->quad_count,
			.position = position,
			.color = color,
		});
	}
}

void text_gfx_sync(Text_GFX *gfx)
{
	Assert(gfx);
	u32 page_count = text_raster_page_count(gfx->text);
	Assert(page_count <= ArrayCount(gfx->pages));
	for (Text_PageId id = 0; id < page_count; ++id)
	{
		Text_GFXPage *gpu_page = &gfx->pages[id];
		if (!gpu_page->texture) {
			continue;
		}
		Text_RasterPage page = text_raster_page(gfx->text, id);
		if (gpu_page->uploaded_revision == page.revision) {
			continue;
		}
		gfx_update_texture(gpu_page->texture, (GFX_UpdateTextureParams) {
			.size = page.size,
			.stride = page.stride,
			.data = (void *)page.pixels,
		});
		gpu_page->uploaded_revision = page.revision;
	}
}
