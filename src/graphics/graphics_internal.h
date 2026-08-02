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
	f32 offset;
	f32 weight;
	f32 padding0;
	f32 padding1;
}
GFX_GaussianPair;

typedef union
{
	f32 values[16];
	struct
	{
		f32            direction_x;
		f32            direction_y;
		f32          center_weight;
		f32                padding;
		GFX_GaussianPair  pairs[3];
	}
	gaussian;
	struct
	{
		f32 saturation;
		f32 tint_opacity;
		f32 grain;
		f32 padding0;
		f32 tint_r;
		f32 tint_g;
		f32 tint_b;
		f32 padding1;
		f32 padding2[8];
	}
	blur_material;
	struct
	{
		f32 strength;
		f32 padding[15];
	}
	barrel;
	struct
	{
		f32 saturation;
		f32 tint_opacity;
		f32 grain;
		f32 corner_radius;
		f32 distortion;
		f32 distortion_width;
		f32 highlight;
		f32 shadow;
		f32 tint_r;
		f32 tint_g;
		f32 tint_b;
		f32 padding[5];
	}
	glass;
	struct
	{
		f32 time;
		f32 strength;
		f32 padding[14];
	}
	rewind;
	struct
	{
		f32 threshold;
		f32 gain;
		f32 padding[14];
	}
	luminance;
}
GFX_BatchParams;

STATIC_ASSERT(sizeof(GFX_GaussianPair) == 16);
STATIC_ASSERT(sizeof(GFX_BatchParams) == sizeof(f32) * 16);

typedef struct
{
	GFX_Texture     *texture;
	rect_i32         scissor;
	GFX_Sampler      sampler;
	GFX_Blender      blender;
	GFX_Shader       shader;
	GFX_TextureMode  texture_mode;
	GFX_BatchParams  shader_block;
}
GFX_BatchDesc;

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
	rect_f32         dst;
	UV_Coords        src;
	vec4             colors[4];
	Draw_CornerRadii corner_radii;
	f32              border_thickness;
	f32              edge_softness;
	f32              disable_texture;
	f32              grain;
}
GFX_RectInst;

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



void gfx_submit_draw(GFX_Renderer *renderer, GFX_DrawData draw);



#endif
