#include "base.h"
#include "graphics.h"

GFX_Texture *gfx_create_texture_from_image(GFX_Renderer *renderer, Image_rgba_u8 image, GFX_Sampler sampler)
{
	Assert(renderer);
	Assert(image.data);
	Assert(image.reso.x > 0 && image.reso.y > 0);
	Assert(image.elem_stride == (u32)image.reso.x);
	return gfx_create_texture(renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_RARE_UPDATES,
		.bind_flags = GFX_TEXTURE_BIND_INPUT,
		.format = GRAPHICS_FORMAT_RGBA_U8_SRGB,
		.size = image.reso,
		.data = image.data,
		.sampler = sampler,
		.label = "image texture",
	});
}
