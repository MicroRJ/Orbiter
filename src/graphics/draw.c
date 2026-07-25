#include "base.h"
#include "graphics_internal.h"
#include "os_graphical.h"

enum
{
	DRAW_PASS_ARENA_CAPACITY = KiB(64),
	DRAW_BATCH_ARENA_CAPACITY = MB(4),
	DRAW_INSTANCE_ARENA_CAPACITY = MB(60),
	DRAW_CLIP_STACK_CAPACITY = 64,
};

struct Draw_Context
{
	GFX_Renderer *renderer;
	Arena pass_arena;
	Arena batch_arena;
	Arena instance_arena;

	GFX_Pass *passes;
	u32 pass_count;
	GFX_PassDesc active_pass;
	u32 active_pass_batch_offset;

	GFX_Batch *batches;
	u32 batch_count;
	GFX_BatchDesc active_batch;
	u32 active_batch_offset;

	GFX_RectInst *instances;
	u32 instance_count;

	GFX_Texture *output;
	rect_i32 viewport;
	rect_i32 clip;
	rect_i32 clip_stack[DRAW_CLIP_STACK_CAPACITY];
	u32 clip_depth;
	b32 frame_active;
	b32 pass_active;
};

static b32 draw__batch_descs_equal(GFX_BatchDesc a, GFX_BatchDesc b)
{
	return a.texture == b.texture &&
		rect_i32_equal(a.scissor, b.scissor) &&
		a.sampler == b.sampler &&
		a.blender == b.blender &&
		a.shader == b.shader &&
		a.texture_mode == b.texture_mode &&
		memory_match(&a.shader_block, &b.shader_block, sizeof(a.shader_block));
}

static void draw__flush_batch(Draw_Context *draw)
{
	u32 instance_count = draw->instance_count - draw->active_batch_offset;
	if (instance_count == 0) {
		return;
	}

	GFX_Batch *batch = arena_push_aligned(&draw->batch_arena, sizeof(*batch), 1);
	*batch = (GFX_Batch) {
		.desc = draw->active_batch,
		.instance_offset = draw->active_batch_offset,
		.instance_count = instance_count,
	};
	draw->batch_count++;
	draw->active_batch_offset = draw->instance_count;
}

static void draw__set_batch_desc(Draw_Context *draw, GFX_BatchDesc desc)
{
	u32 instance_count = draw->instance_count - draw->active_batch_offset;
	if (instance_count == 0) {
		// With no pending instances there is nothing to flush, but this descriptor
		// still owns the instances that will be appended next.
		draw->active_batch = desc;
		return;
	}

	if (draw__batch_descs_equal(draw->active_batch, desc)) {
		return;
	}

	draw__flush_batch(draw);
	draw->active_batch = desc;
}

static void draw__push_instance(Draw_Context *draw, GFX_RectInst instance)
{
	GFX_RectInst *destination = arena_push_aligned(&draw->instance_arena,
		sizeof(*destination), 1);
	*destination = instance;
	draw->instance_count++;
}

static GFX_BatchDesc draw__batch_desc(Draw_Context *draw, GFX_Texture *texture, GFX_Sampler sampler, GFX_Blender blender, GFX_Shader shader, GFX_TextureMode texture_mode)
{
	Assert(draw->output);
	Assert(texture);
	Assert(texture->bind_flags & GFX_TEXTURE_BIND_INPUT);
	Assert(sampler != GRAPHICS_SAMPLER_NONE);
	Assert(blender > GFX_BLENDER_NONE && blender < GFX_BLENDER_COUNT);
	Assert(shader > GFX_SHADER_NONE && shader < GFX_SHADER_COUNT);
	return (GFX_BatchDesc) {
		.texture = texture,
		.scissor = draw->clip,
		.sampler = sampler,
		.blender = blender,
		.shader = shader,
		.texture_mode = texture_mode,
	};
}

static UV_Coords draw__uv_from_region(GFX_Texture *texture, rect_i32 region)
{
	vec2i size = gfx_texture_size(texture);
	if (region.w == 0)
	{
		region.w = size.x - region.x;
	}
	if (region.h == 0)
	{
		region.h = size.y - region.y;
	}
	Assert(region.x >= 0 && region.w >= 0 && region.x + region.w <= size.x);
	Assert(region.y >= 0 && region.h >= 0 && region.y + region.h <= size.y);
	return (UV_Coords) {
		.u0 = (f32)region.x / size.x,
		.v0 = (f32)region.y / size.y,
		.u1 = (f32)(region.x + region.w) / size.x,
		.v1 = (f32)(region.y + region.h) / size.y,
	};
}

static void draw__set_all_colors(GFX_RectInst *instance, Color_Linear color)
{
	for (u32 index = 0; index < _countof(instance->colors); ++index)
	{
		instance->colors[index] = color;
	}
}

GFX_Renderer *gfx_renderer_create(Arena *owner)
{
	return r_renderer_create(owner);
}

GFX_Window *gfx_window_create(Arena *owner, GFX_Renderer *renderer, OS_Window *window)
{
	return r_window_create(owner, renderer, window);
}

GFX_Texture *gfx_window_texture(GFX_Window *window)
{
	Assert(window);
	return r_get_window_output(window);
}

void gfx_window_resize(GFX_Window *window, vec2i size)
{
	Assert(window);
	r_resize_output_targets(window, size);
}

void gfx_window_present(GFX_Window *window)
{
	Assert(window);
	r_present(window);
}

Draw_Context *draw_create(Arena *owner, GFX_Renderer *renderer)
{
	Assert(owner);
	Assert(renderer);
	Draw_Context *draw = arena_push_zero(owner, sizeof(*draw));
	draw->renderer = renderer;
	draw->pass_arena = arena_create(DRAW_PASS_ARENA_CAPACITY, "draw pass arena");
	draw->batch_arena = arena_create(DRAW_BATCH_ARENA_CAPACITY, "draw batch arena");
	draw->instance_arena = arena_create(DRAW_INSTANCE_ARENA_CAPACITY, "draw instance arena");
	return draw;
}

void gfx_begin_frame(Draw_Context *draw)
{
	Assert(draw);
	Assert(!draw->frame_active);
	Assert(!draw->pass_active);
	r_begin_frame(draw->renderer);
	arena_reset(&draw->pass_arena);
	arena_reset(&draw->batch_arena);
	arena_reset(&draw->instance_arena);
	draw->passes = arena_base(&draw->pass_arena);
	draw->pass_count = 0;
	draw->batches = arena_base(&draw->batch_arena);
	draw->batch_count = 0;
	draw->instances = arena_base(&draw->instance_arena);
	draw->instance_count = 0;
	draw->active_batch_offset = 0;
	draw->frame_active = true;
}

void gfx_begin_pass(Draw_Context *draw, GFX_PassDesc desc)
{
	Assert(draw);
	Assert(draw->frame_active);
	Assert(!draw->pass_active);
	Assert(desc.output);
	Assert(desc.output->renderer == draw->renderer);
	Assert(desc.output->bind_flags & GFX_TEXTURE_BIND_OUTPUT);
	draw->active_batch = (GFX_BatchDesc) { 0 };
	draw->clip_depth = 0;
	draw->output = desc.output;
	draw->viewport = desc.viewport;
	if (draw->viewport.w == 0 || draw->viewport.h == 0) {
		draw->viewport = (rect_i32) { 0, 0, desc.output->reso.x, desc.output->reso.y };
	}
	desc.viewport = draw->viewport;
	draw->active_pass = desc;
	draw->active_pass_batch_offset = draw->batch_count;
	draw->clip = draw->viewport;
	draw->pass_active = true;
}

void gfx_end_pass(Draw_Context *draw)
{
	Assert(draw);
	Assert(draw->pass_active);
	Assert(draw->clip_depth == 0);
	draw__flush_batch(draw);
	GFX_Pass *pass = arena_push_aligned(&draw->pass_arena, sizeof(*pass), 1);
	*pass = (GFX_Pass) {
		.desc = draw->active_pass,
		.batch_offset = draw->active_pass_batch_offset,
		.batch_count = draw->batch_count - draw->active_pass_batch_offset,
	};
	draw->pass_count++;
	draw->output = 0;
	draw->pass_active = false;
}

void gfx_end_frame(Draw_Context *draw)
{
	Assert(draw);
	Assert(draw->frame_active);
	Assert(!draw->pass_active);
	GFX_DrawData data = {
		.passes = draw->passes,
		.pass_count = draw->pass_count,
		.batches = draw->batches,
		.batch_count = draw->batch_count,
		.instances = draw->instances,
		.instances_size = draw->instance_count * sizeof(*draw->instances),
	};
	PROF_BLOCK("renderer submit") r_draw(draw->renderer, data);
	draw->frame_active = false;
}

void draw_push_clip(Draw_Context *draw, rect_f32 clip)
{
	Assert(draw);
	Assert(draw->clip_depth < _countof(draw->clip_stack));
	draw->clip_stack[draw->clip_depth++] = draw->clip;

	rect_i32 child = rect_i32_from_f32(clip);
	rect_i32 parent = draw->clip;
	// A nested clip restricts its parent. Replacing the parent scissor allowed
	// child views to draw outside the panel that owned them.
	i32 x0 = Max(parent.x, child.x);
	i32 y0 = Max(parent.y, child.y);
	i32 x1 = Min(parent.x + parent.w, child.x + child.w);
	i32 y1 = Min(parent.y + parent.h, child.y + child.h);
	draw->clip = (rect_i32) {
		x0,
		y0,
		Max(0, x1 - x0),
		Max(0, y1 - y0),
	};
}

void draw_pop_clip(Draw_Context *draw)
{
	Assert(draw);
	Assert(draw->clip_depth > 0);
	draw->clip = draw->clip_stack[--draw->clip_depth];
}

void draw_rect(Draw_Context *draw, Draw_RectParams params)
{
	GFX_Texture *texture = draw->active_batch.texture ? draw->active_batch.texture : r_get_fallback_texture(draw->renderer);
	GFX_BatchDesc desc = draw__batch_desc(draw, texture, GRAPHICS_SAMPLER_LINEAR, GFX_BLENDER_ALPHA_BLEND, GFX_SHADER_SDF_RECT, GRAPHICS_TEXTURE_RGBA);
	draw__set_batch_desc(draw, desc);
	GFX_RectInst instance = {
		.dst = params.rect,
		.src = { 0, 0, 1, 1 },
		.corner_radii = params.corner_radii,
		.border_thickness = params.border_thickness,
		.edge_softness = params.edge_softness,
		.disable_texture = 1.f,
	};
	draw__set_all_colors(&instance, color_linear_from_srgba(params.color));
	draw__push_instance(draw, instance);
}

void draw_gradient(Draw_Context *draw, Draw_GradientParams params)
{
	Assert(params.axis == AXIS_X || params.axis == AXIS_Y);
	GFX_BatchDesc desc = draw__batch_desc(draw, r_get_fallback_texture(draw->renderer), GRAPHICS_SAMPLER_LINEAR, GFX_BLENDER_ALPHA_BLEND, GFX_SHADER_SDF_RECT, GRAPHICS_TEXTURE_RGBA);
	draw__set_batch_desc(draw, desc);
	GFX_RectInst instance = {
		.dst = params.rect,
		.src = { 0, 0, 1, 1 },
		.disable_texture = 1.f,
	};
	Color_Linear start = color_linear_from_srgba(params.start_color);
	Color_Linear end = color_linear_from_srgba(params.end_color);
	if (params.axis == AXIS_X)
	{
		instance.colors[CORNER_BOT_L] = start;
		instance.colors[CORNER_TOP_L] = start;
		instance.colors[CORNER_BOT_R] = end;
		instance.colors[CORNER_TOP_R] = end;
	}
	else
	{
		instance.colors[CORNER_TOP_L] = start;
		instance.colors[CORNER_TOP_R] = start;
		instance.colors[CORNER_BOT_L] = end;
		instance.colors[CORNER_BOT_R] = end;
	}
	draw__push_instance(draw, instance);
}

void draw_image(Draw_Context *draw, Draw_TextureParams params)
{
	Assert(params.texture);
	GFX_Sampler sampler = params.sampler;
	if (sampler == GRAPHICS_SAMPLER_NONE)
	{
		sampler = params.texture->sampler;
	}
	if (sampler == GRAPHICS_SAMPLER_NONE)
	{
		sampler = GRAPHICS_SAMPLER_LINEAR;
	}
	GFX_Blender blender = params.blender;
	if (blender == GFX_BLENDER_NONE)
	{
		blender = GFX_BLENDER_ALPHA_BLEND;
	}
	GFX_Shader shader = params.shader;
	if (shader == GFX_SHADER_NONE)
	{
		shader = GFX_SHADER_SDF_RECT;
	}
	GFX_BatchDesc desc = draw__batch_desc(draw, params.texture, sampler,
		blender, shader, GRAPHICS_TEXTURE_RGBA);
	draw__set_batch_desc(draw, desc);
	GFX_RectInst instance = {
		.dst = params.rect,
		.src = draw__uv_from_region(params.texture, params.region),
	};
	draw__set_all_colors(&instance, color_linear_from_srgba(params.tint));
	draw__push_instance(draw, instance);
}

void draw_barrel(Draw_Context *draw, Draw_BarrelParams params)
{
	Assert(draw);
	Assert(draw->pass_active);
	Assert(params.texture);
	GFX_BatchDesc desc = draw__batch_desc(draw, params.texture, GRAPHICS_SAMPLER_LINEAR, GFX_BLENDER_DISABLED, GFX_SHADER_BARREL, GRAPHICS_TEXTURE_RGBA);
	desc.shader_block.barrel.strength = params.strength;
	draw__set_batch_desc(draw, desc);
	GFX_RectInst instance = {
		.dst = rect_f32_from_i32(draw->viewport),
		.src = { 0, 0, 1, 1 },
	};
	draw__set_all_colors(&instance, color_linear_from_srgba(COLOR_WHITE));
	draw__push_instance(draw, instance);
}

void draw_blit(Draw_Context *draw, GFX_Texture *texture)
{
	Assert(draw);
	Assert(draw->pass_active);
	Assert(texture);
	GFX_BatchDesc desc = draw__batch_desc(draw, texture, GRAPHICS_SAMPLER_POINT, GFX_BLENDER_DISABLED, GFX_SHADER_BLIT, GRAPHICS_TEXTURE_RGBA);
	draw__set_batch_desc(draw, desc);
	GFX_RectInst instance = {
		.dst = rect_f32_from_i32(draw->viewport),
		.src = { 0, 0, 1, 1 },
	};
	draw__set_all_colors(&instance, color_linear_from_srgba(COLOR_WHITE));
	draw__push_instance(draw, instance);
}

void draw_texture_copy(Draw_Context *draw, GFX_Texture *texture)
{
	Assert(draw);
	Assert(draw->pass_active);
	Assert(texture);
	GFX_BatchDesc desc = draw__batch_desc(draw, texture, GRAPHICS_SAMPLER_LINEAR, GFX_BLENDER_DISABLED, GFX_SHADER_COPY, GRAPHICS_TEXTURE_RGBA);
	draw__set_batch_desc(draw, desc);
	GFX_RectInst instance = {
		.dst = rect_f32_from_i32(draw->viewport),
		.src = { 0, 0, 1, 1 },
	};
	draw__set_all_colors(&instance, color_linear_from_srgba(COLOR_WHITE));
	draw__push_instance(draw, instance);
}

static void draw__texture_shader(Draw_Context *draw, GFX_Texture *texture, GFX_Shader shader, GFX_Sampler sampler)
{
	Assert(draw);
	Assert(draw->pass_active);
	Assert(texture);
	GFX_BatchDesc desc = draw__batch_desc(draw, texture, sampler, GFX_BLENDER_DISABLED, shader, GRAPHICS_TEXTURE_RGBA);
	draw__set_batch_desc(draw, desc);
	GFX_RectInst instance = {
		.dst = rect_f32_from_i32(draw->viewport),
		.src = { 0, 0, 1, 1 },
	};
	draw__set_all_colors(&instance, color_linear_from_srgba(COLOR_WHITE));
	draw__push_instance(draw, instance);
}

void draw_crt_scanlines(Draw_Context *draw, GFX_Texture *texture)
{
	draw__texture_shader(draw, texture, GFX_SHADER_CRT_SCANLINES, GRAPHICS_SAMPLER_POINT);
}

void draw_luminance(Draw_Context *draw, Draw_LuminanceParams params)
{
	Assert(draw);
	Assert(draw->pass_active);
	Assert(params.texture);
	GFX_BatchDesc desc = draw__batch_desc(draw, params.texture, GRAPHICS_SAMPLER_LINEAR, GFX_BLENDER_DISABLED, GFX_SHADER_LUMINANCE, GRAPHICS_TEXTURE_RGBA);
	desc.shader_block.luminance.threshold = params.threshold;
	desc.shader_block.luminance.gain = params.gain;
	draw__set_batch_desc(draw, desc);
	GFX_RectInst instance = {
		.dst = rect_f32_from_i32(draw->viewport),
		.src = { 0, 0, 1, 1 },
	};
	draw__set_all_colors(&instance, color_linear_from_srgba(COLOR_WHITE));
	draw__push_instance(draw, instance);
}

void draw_rewind(Draw_Context *draw, Draw_RewindParams params)
{
	Assert(draw);
	Assert(draw->pass_active);
	Assert(params.texture);
	GFX_BatchDesc desc = draw__batch_desc(draw, params.texture, GRAPHICS_SAMPLER_LINEAR, GFX_BLENDER_DISABLED, GFX_SHADER_REWIND, GRAPHICS_TEXTURE_RGBA);
	desc.shader_block.rewind.time = params.time;
	desc.shader_block.rewind.strength = params.strength;
	draw__set_batch_desc(draw, desc);
	GFX_RectInst instance = {
		.dst = rect_f32_from_i32(draw->viewport),
		.src = { 0, 0, 1, 1 },
	};
	draw__set_all_colors(&instance, color_linear_from_srgba(COLOR_WHITE));
	draw__push_instance(draw, instance);
}

static GFX_ShaderBlock draw__make_gaussian_block(Draw_GaussianBlurParams params)
{
	Assert(params.sigma > 0.f);
	f32 weights[7];
	f32 sum = 0.f;
	for (u32 index = 0; index < ArrayCount(weights); ++index)
	{
		f32 distance = (f32)index;
		weights[index] = expf(-(distance * distance) / (2.f * params.sigma * params.sigma));
		sum += index ? weights[index] * 2.f : weights[index];
	}
	for (u32 index = 0; index < ArrayCount(weights); ++index) {
		weights[index] /= sum;
	}
	GFX_ShaderBlock block = {};
	block.gaussian.direction_x = params.direction.x;
	block.gaussian.direction_y = params.direction.y;
	block.gaussian.center_weight = weights[0];
	for (u32 pair = 0; pair < 3; ++pair)
	{
		u32 first = pair * 2 + 1;
		u32 second = first + 1;
		f32 weight = weights[first] + weights[second];
		f32 offset = ((f32)first * weights[first] + (f32)second * weights[second]) / weight;
		block.gaussian.pairs[pair].offset = offset;
		block.gaussian.pairs[pair].weight = weight;
	}
	f32 packed_sum = block.gaussian.center_weight;
	for (u32 pair = 0; pair < 3; ++pair) {
		packed_sum += block.gaussian.pairs[pair].weight * 2.f;
	}
	Assert(fabsf(packed_sum - 1.f) < 0.0001f);
	return block;
}

void draw_gaussian_blur(Draw_Context *draw, Draw_GaussianBlurParams params)
{
	Assert(draw);
	Assert(draw->pass_active);
	Assert(params.texture);
	GFX_BatchDesc desc = draw__batch_desc(draw, params.texture, GRAPHICS_SAMPLER_LINEAR, GFX_BLENDER_DISABLED, GFX_SHADER_GAUSSIAN, GRAPHICS_TEXTURE_RGBA);
	desc.shader_block = draw__make_gaussian_block(params);
	draw__set_batch_desc(draw, desc);
	GFX_RectInst instance = {
		.dst = rect_f32_from_i32(draw->viewport),
		.src = { 0, 0, 1, 1 },
	};
	draw__set_all_colors(&instance, color_linear_from_srgba(COLOR_WHITE));
	draw__push_instance(draw, instance);
}

void draw_blur_material(Draw_Context *draw, Draw_BlurMaterialParams params)
{
	Assert(draw);
	Assert(draw->pass_active);
	Assert(params.texture);
	GFX_BatchDesc desc = draw__batch_desc(draw, params.texture, GRAPHICS_SAMPLER_LINEAR, GFX_BLENDER_DISABLED, GFX_SHADER_BLUR_MATERIAL, GRAPHICS_TEXTURE_RGBA);
	Color_Linear tint = color_linear_from_srgba(params.tint);
	desc.shader_block.blur_material.saturation = params.saturation;
	desc.shader_block.blur_material.tint_opacity = tint.a;
	desc.shader_block.blur_material.grain = params.grain;
	desc.shader_block.blur_material.tint_r = tint.r;
	desc.shader_block.blur_material.tint_g = tint.g;
	desc.shader_block.blur_material.tint_b = tint.b;
	draw__set_batch_desc(draw, desc);
	rect_f32 viewport = rect_f32_from_i32(draw->viewport);
	GFX_RectInst instance = {
		.dst = params.rect,
		.src = {
			(params.rect.x - viewport.x) / viewport.w,
			(params.rect.y - viewport.y) / viewport.h,
			(params.rect.x + params.rect.w - viewport.x) / viewport.w,
			(params.rect.y + params.rect.h - viewport.y) / viewport.h,
		},
	};
	draw__set_all_colors(&instance, color_linear_from_srgba(COLOR_WHITE));
	draw__push_instance(draw, instance);
}

void draw_glass(Draw_Context *draw, Draw_GlassParams params)
{
	Assert(draw);
	Assert(draw->pass_active);
	Assert(params.texture);
	GFX_BatchDesc desc = draw__batch_desc(draw, params.texture, GRAPHICS_SAMPLER_LINEAR, GFX_BLENDER_ALPHA_BLEND, GFX_SHADER_GLASS, GRAPHICS_TEXTURE_RGBA);
	Color_Linear tint = color_linear_from_srgba(params.tint);
	desc.shader_block.glass.saturation = params.saturation;
	desc.shader_block.glass.tint_opacity = tint.a;
	desc.shader_block.glass.grain = params.grain;
	desc.shader_block.glass.corner_radius = params.corner_radius;
	desc.shader_block.glass.distortion = params.distortion;
	desc.shader_block.glass.distortion_width = params.distortion_width;
	desc.shader_block.glass.highlight = params.highlight;
	desc.shader_block.glass.shadow = params.shadow;
	desc.shader_block.glass.tint_r = tint.r;
	desc.shader_block.glass.tint_g = tint.g;
	desc.shader_block.glass.tint_b = tint.b;
	draw__set_batch_desc(draw, desc);
	rect_f32 viewport = rect_f32_from_i32(draw->viewport);
	GFX_RectInst instance = {
		.dst = params.rect,
		.src = {
			(params.rect.x - viewport.x) / viewport.w,
			(params.rect.y - viewport.y) / viewport.h,
			(params.rect.x + params.rect.w - viewport.x) / viewport.w,
			(params.rect.y + params.rect.h - viewport.y) / viewport.h,
		},
	};
	draw__set_all_colors(&instance, color_linear_from_srgba(COLOR_WHITE));
	draw__push_instance(draw, instance);
}

void draw_inset_shadow(Draw_Context *draw, rect_f32 rect, f32 strength)
{
	f32 thickness = Min(rect.size.x, rect.size.y) * strength;
	f32 intensity = strength * 0.85f;
	Color_SRGBA color = color_with_alpha(COLOR_BLACK, intensity);
	draw_gradient(draw, (Draw_GradientParams) {
		.rect = rect_f32_from_slice(rect, AXIS_X, (i32)thickness),
		.axis = AXIS_X,
		.start_color = color,
		.end_color = COLOR_TRANSPARENT,
	});
	draw_gradient(draw, (Draw_GradientParams) {
		.rect = rect_f32_from_slice(rect, AXIS_X, -(i32)thickness),
		.axis = AXIS_X,
		.start_color = COLOR_TRANSPARENT,
		.end_color = color,
	});
	draw_gradient(draw, (Draw_GradientParams) {
		.rect = rect_f32_from_slice(rect, AXIS_Y, (i32)thickness),
		.axis = AXIS_Y,
		.start_color = color,
		.end_color = COLOR_TRANSPARENT,
	});
	draw_gradient(draw, (Draw_GradientParams) {
		.rect = rect_f32_from_slice(rect, AXIS_Y, -(i32)thickness),
		.axis = AXIS_Y,
		.start_color = COLOR_TRANSPARENT,
		.end_color = color,
	});
}

void draw_mask_rects(Draw_Context *draw, Draw_MaskRectsParams params)
{
	Assert(draw);
	Assert(draw->pass_active);
	Assert(params.texture);
	Assert(params.rects || !params.rect_count);
	if (!params.rect_count) {
		return;
	}
	GFX_BatchDesc desc = draw__batch_desc(draw, params.texture, GRAPHICS_SAMPLER_POINT, GFX_BLENDER_ALPHA_BLEND, GFX_SHADER_TEXT_MASK, GRAPHICS_TEXTURE_MASK);
	draw__set_batch_desc(draw, desc);
	Color_Linear color = color_linear_from_srgba(params.color);
	for (u32 index = 0; index < params.rect_count; ++index)
	{
		GFX_RectInst instance = {
			.dst = rect_f32_translate(params.rects[index].destination, params.position),
			.src = params.rects[index].source,
			.grain = 1.f,
		};
		draw__set_all_colors(&instance, color);
		draw__push_instance(draw, instance);
	}
}
