#ifndef GRAPHICS_INTERNAL_H
#define GRAPHICS_INTERNAL_H

#include "graphics.h"

typedef union
{
	struct { vec4 x, y, z, w; };
	vec4 rows[4];
}
Matrix;

static const Matrix MIXER_RGBA = {
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1,
};

static const Matrix MIXER_RRRR = {
	1, 0, 0, 0,
	1, 0, 0, 0,
	1, 0, 0, 0,
	1, 0, 0, 0,
};

static Matrix graphics_matrix_for_ndc_transform(vec2 size)
{
	vec2 scale = v2_div(v2(2.f, -2.f), size);
	return (Matrix) {
		scale.x, 0, 0, -1.f,
		0, scale.y, 0, 1.f,
		0, 0, 1, 0,
		0, 0, 0, 1,
	};
}

typedef enum
{
	GRAPHICS_TEXTURE_RGBA,
	GRAPHICS_TEXTURE_MASK,
}
GFX_TextureMode;

struct GFX_Texture
{
	GFX_Renderer    *renderer;
	GFX_TextureUsage usage;
	GFX_TextureBindFlags bind_flags;
	GFX_Format       format;
	GFX_Sampler      sampler;
	vec2i            reso;
	const char      *label;
};

typedef struct
{
	GFX_Texture     *texture;
	rect_i32         scissor;
	GFX_Sampler      sampler;
	GFX_Blender        blender;
	GFX_Shader       shader;
	GFX_TextureMode  texture_mode;
	GFX_ShaderBlock  shader_block;
}
GFX_BatchDesc;

typedef struct
{
	rect_f32        dst;
	UV_Coords       src;
	vec4            colors[4];
	Draw_CornerRadii corner_radii;
	f32             border_thickness;
	f32             edge_softness;
	f32             disable_texture;
	f32             grain;
}
GFX_RectInst;

typedef struct
{
	GFX_BatchDesc desc;
	u32           instance_offset;
	u32           instance_count;
}
GFX_Batch;

typedef struct
{
	GFX_PassDesc desc;
	u32          batch_offset;
	u32          batch_count;
}
GFX_Pass;

typedef struct
{
	GFX_Pass        *passes;
	u32              pass_count;
	GFX_Batch        *batches;
	u32               batch_count;
	GFX_RectInst     *instances;
	u32               instances_size;
}
GFX_DrawData;

GFX_Renderer *r_renderer_create(Arena *owner);
void r_begin_frame(GFX_Renderer *renderer);
GFX_Window *gfx_create_window(Arena *owner, GFX_Renderer *renderer, OS_Window *window);
GFX_Texture *r_get_window_output(GFX_Window *window);
GFX_Texture *r_get_fallback_texture(GFX_Renderer *renderer);
void r_resize_output_targets(GFX_Window *window, vec2i resolution);
void r_clear_output(GFX_Renderer *renderer, GFX_Texture *output, Color_SRGBA color);
void gfx_submit_draw(GFX_Renderer *renderer, GFX_DrawData draw);
void gfx_present_window(GFX_Window *window);

#endif
