#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "color.h"
#include "text.h"
typedef enum
{
	GFX_BLENDER_NONE = 0,
	GFX_BLENDER_DISABLED,
	GFX_BLENDER_ALPHA_BLEND,
	GFX_BLENDER_ADDITIVE,
	GFX_BLENDER_COUNT,
}
GFX_Blender;

typedef enum
{
	GFX_SHADER_NONE = 0,
	GFX_SHADER_BLIT,
	GFX_SHADER_BARREL,
	GFX_SHADER_GAUSSIAN,
	GFX_SHADER_COPY,
	GFX_SHADER_BLUR_MATERIAL,
	GFX_SHADER_GLASS,
	GFX_SHADER_SDF_RECT,
	GFX_SHADER_TEXT_MASK,
	GFX_SHADER_CRT_SCANLINES,
	GFX_SHADER_LUMINANCE,
	GFX_SHADER_REWIND,
	GFX_SHADER_COUNT,
}
GFX_Shader;

typedef enum
{
	GRAPHICS_TEXTURE_USAGE_RARE_UPDATES,
	GRAPHICS_TEXTURE_USAGE_PER_FRAME,
}
GFX_TextureUsage;

typedef enum
{
	GFX_TEXTURE_BIND_INPUT  = 1 << 0,
	GFX_TEXTURE_BIND_OUTPUT = 1 << 1,
}
GFX_TextureBindFlags;

// The low byte stores the number of bytes per pixel.
typedef enum
{
	GRAPHICS_FORMAT_NONE         = 0 << 8 |  0,
	GRAPHICS_FORMAT_R_U8         = 1 << 8 |  1,
	GRAPHICS_FORMAT_RGBA_U8      = 2 << 8 |  4,
	GRAPHICS_FORMAT_RGBA_U8_SRGB = 3 << 8 |  4,
	GRAPHICS_FORMAT_RGBA_F32     = 4 << 8 | 16,
	GRAPHICS_FORMAT_COUNT,
}
GFX_Format;

typedef enum
{
	GRAPHICS_SAMPLER_NONE = 0,
	GRAPHICS_SAMPLER_POINT,
	GRAPHICS_SAMPLER_LINEAR,
	GRAPHICS_SAMPLER_COUNT,
}
GFX_Sampler;

typedef struct GFX_Texture GFX_Texture;
typedef struct Draw_Context Draw_Context;
typedef struct GFX_Renderer GFX_Renderer;
typedef struct GFX_Window GFX_Window;
typedef struct OS_Window OS_Window;
typedef struct Text_GFX Text_GFX;

typedef struct
{
	GFX_TextureUsage usage;
	GFX_TextureBindFlags bind_flags;
	GFX_Format       format;
	vec2i            size;
	void            *data;
	GFX_Sampler      sampler;
	const char      *label;
}
GFX_TextureDesc;

typedef struct
{
	vec2i dest;
	vec2i size;
	u32   stride;
	void *data;
}
GFX_TextureUpdateParams;

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
		f32 direction_x;
		f32 direction_y;
		f32 center_weight;
		f32 padding;
		GFX_GaussianPair pairs[3];
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
GFX_ShaderBlock;

STATIC_ASSERT(sizeof(GFX_GaussianPair) == 16);
STATIC_ASSERT(sizeof(GFX_ShaderBlock) == sizeof(f32) * 16);

GFX_Texture *gfx_create_texture(GFX_Renderer *renderer, GFX_TextureDesc desc);
GFX_Texture *gfx_acquire_transient_texture(GFX_Renderer *renderer, GFX_TextureDesc desc);
void gfx_destroy_texture(GFX_Texture *texture);
vec2i gfx_texture_size(const GFX_Texture *texture);

// Rare-update textures accept partial updates. Per-frame textures use a mapped
// discard upload and therefore require a full-texture replacement.
void gfx_update_texture(GFX_Texture *texture, GFX_TextureUpdateParams desc);
// Copies the texture's native pixel format into caller memory. The destination
// stride must hold one complete row.
b32 gfx_read_texture(GFX_Texture *texture, void *data, u32 stride);

typedef UV_Rect Draw_MaskRect;

typedef struct
{
	f32 top_left;
	f32 bot_left;
	f32 top_right;
	f32 bot_right;
}
Draw_CornerRadii;

typedef struct
{
	rect_f32         rect;
	Color_SRGBA      color;
	Draw_CornerRadii corner_radii;
	f32              border_thickness;
	f32              edge_softness;
}
Draw_RectParams;

typedef struct
{
	rect_f32 rect;
	i32      axis;
	Color_SRGBA start_color;
	Color_SRGBA end_color;
}
Draw_GradientParams;

typedef struct
{
	rect_f32    rect;
	GFX_Texture *texture;
	rect_i32    region;
	Color_SRGBA tint;
	GFX_Sampler sampler;
	GFX_Blender blender;
	GFX_Shader  shader;
}
Draw_TextureParams;

typedef struct
{
	GFX_Texture *texture;
	const Draw_MaskRect *rects;
	u32 rect_count;
	vec2 position;
	Color_SRGBA color;
}
Draw_MaskRectsParams;

typedef struct
{
	GFX_Texture *texture;
	vec2 direction;
	f32 sigma;
}
Draw_GaussianBlurParams;

typedef struct
{
	GFX_Texture *texture;
	rect_f32 rect;
	f32 saturation;
	Color_SRGBA tint;
	f32 grain;
}
Draw_BlurMaterialParams;

typedef struct
{
	GFX_Texture *texture;
	rect_f32 rect;
	f32 corner_radius;
	f32 distortion;
	f32 distortion_width;
	f32 saturation;
	Color_SRGBA tint;
	f32 grain;
	f32 highlight;
	f32 shadow;
}
Draw_GlassParams;

typedef struct
{
	GFX_Texture *texture;
	f32 strength;
}
Draw_BarrelParams;

typedef struct
{
	GFX_Texture *texture;
	f32 time;
	f32 strength;
}
Draw_RewindParams;

typedef struct
{
	GFX_Texture *texture;
	f32 threshold;
	f32 gain;
}
Draw_LuminanceParams;

typedef struct
{
	Text_DrawRun run;
	vec2 position;
	Color_SRGBA color;
}
Draw_TextParams;

typedef struct
{
	rect_f32 rect;
	f32 strength;
}
Draw_InsetShadowParams;

typedef struct
{
	rect_f32 rect;
	f32 corner_radius;
	f32 distortion;
	f32 distortion_width;
	f32 saturation;
	Color_SRGBA tint;
	f32 grain;
	f32 highlight;
	f32 shadow;
}
Draw_BackdropParams;

typedef enum
{
	DRAW_LAYER_CONTENT,
	DRAW_LAYER_HEADER,
	DRAW_LAYER_OVERLAY,
	DRAW_LAYER_COUNT,
}
Draw_LayerKind;

typedef enum
{
	DRAW_COMMAND_RECT,
	DRAW_COMMAND_IMAGE,
	DRAW_COMMAND_TEXT,
	DRAW_COMMAND_INSET_SHADOW,
	DRAW_COMMAND_BACKDROP,
}
Draw_CommandKind;

typedef struct Draw_Command Draw_Command;
struct Draw_Command
{
	Draw_Command *next;
	Draw_CommandKind kind;
	f32 emission;
	b32 has_clip;
	rect_f32 clip;
	union
	{
		Draw_RectParams rect;
		Draw_TextureParams image;
		Draw_TextParams text;
		Draw_InsetShadowParams inset_shadow;
		Draw_BackdropParams backdrop;
	};
};

typedef struct
{
	GFX_Texture *output;
	rect_i32 viewport;
	b32 clear;
	Color_SRGBA clear_color;
}
GFX_PassDesc;

Draw_Context *draw_create(Arena *owner, GFX_Renderer *renderer);
void draw_push_clip(Draw_Context *draw, rect_f32 clip);
void draw_pop_clip(Draw_Context *draw);
void draw_rect(Draw_Context *draw, Draw_RectParams params);
void draw_gradient(Draw_Context *draw, Draw_GradientParams params);
void draw_image(Draw_Context *draw, Draw_TextureParams params);
void draw_crt_scanlines(Draw_Context *draw, GFX_Texture *texture);
void draw_gaussian_blur(Draw_Context *draw, Draw_GaussianBlurParams params);
void draw_blur_material(Draw_Context *draw, Draw_BlurMaterialParams params);
void draw_glass(Draw_Context *draw, Draw_GlassParams params);
void draw_rewind(Draw_Context *draw, Draw_RewindParams params);
void draw_luminance(Draw_Context *draw, Draw_LuminanceParams params);
void draw_inset_shadow(Draw_Context *draw, rect_f32 rect, f32 strength);

void draw_texture_copy(Draw_Context *draw, GFX_Texture *texture);
void draw_barrel(Draw_Context *draw, Draw_BarrelParams params);
void draw_blit(Draw_Context *draw, GFX_Texture *texture);
void draw_mask_rects(Draw_Context *draw, Draw_MaskRectsParams params);

void draw_list_push_layer(Draw_Context *draw, Draw_LayerKind layer);
void draw_list_pop_layer(Draw_Context *draw);
void draw_list_push_clip(Draw_Context *draw, rect_f32 clip);
void draw_list_pop_clip(Draw_Context *draw);
void draw_list_push_unclipped(Draw_Context *draw);
void draw_list_pop_unclipped(Draw_Context *draw);
void draw_list_push_emission(Draw_Context *draw, f32 emission);
void draw_list_pop_emission(Draw_Context *draw);
Draw_Command *draw_list_rect(Draw_Context *draw, Draw_RectParams params);
void draw_list_image(Draw_Context *draw, Draw_TextureParams params);
void draw_list_text(Draw_Context *draw, Draw_TextParams params);
void draw_list_inset_shadow(Draw_Context *draw, Draw_InsetShadowParams params);
void draw_list_backdrop(Draw_Context *draw, Draw_BackdropParams params);
void draw_compose(Draw_Context *draw, Text_GFX *text_gfx, GFX_Texture *output, rect_f32 output_rect);

GFX_Renderer *gfx_renderer_create(Arena *owner);
GFX_Window *gfx_window_create(Arena *owner, GFX_Renderer *renderer, OS_Window *window);
void gfx_window_resize(GFX_Window *window, vec2i size);
GFX_Texture *gfx_window_texture(GFX_Window *window);
void gfx_window_present(GFX_Window *window);
void gfx_begin_frame(Draw_Context *draw);
void gfx_begin_pass(Draw_Context *draw, GFX_PassDesc desc);
void gfx_end_pass(Draw_Context *draw);
void gfx_end_frame(Draw_Context *draw);

#endif
