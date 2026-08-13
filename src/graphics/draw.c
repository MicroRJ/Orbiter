#include "base.h"
#include "graphics_internal.h"
#include "os_graphical.h"
#include "text_gfx.h"

enum
{
	DRAW_COMMAND_ARENA_CAPACITY       = MB(256),
	DRAW_PASS_ARENA_CAPACITY          = MB(256),
	DRAW_BATCH_ARENA_CAPACITY         = MB(256),
	DRAW_INSTANCE_ARENA_CAPACITY      = MB(256),
	DRAW_MAX_PASS_COUNT               = DRAW_PASS_ARENA_CAPACITY / sizeof(GFX_Pass),
	DRAW_MAX_BATCH_COUNT              = DRAW_BATCH_ARENA_CAPACITY / sizeof(GFX_Batch),
	DRAW_MAX_INSTANCE_COUNT           = DRAW_INSTANCE_ARENA_CAPACITY / sizeof(GFX_Inst),

	DRAW_CLIP_STACK_CAPACITY          = 64,
	DRAW_LIST_Z_STACK_CAPACITY        = 8,
	DRAW_LIST_CLIP_STACK_CAPACITY     = 16,
	DRAW_LIST_EMISSION_STACK_CAPACITY = 8,
};

typedef struct
{
	u32 breaks;
	u32 pass_ends;
	u32 texture;
	u32 scissor;
	u32 sampler;
	u32 blender;
	u32 shader;
	u32 texture_mode;
	u32 shader_block;
}
Draw_BatchMetrics;

typedef struct Draw_Run Draw_Run;

typedef enum
{
	DRAW_COMMAND_RECT,
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
	union
	{
		Draw_RectParams rect;
		Draw_TextParams text;
		Draw_InsetShadowParams inset_shadow;
		Draw_BackdropParams backdrop;
	};
};

typedef struct
{
	rect_i32 clip;
	i32 z;
	b32 has_clip;
}
Draw_RunState;

struct Draw_Run
{
	Draw_Run *next;
	Draw_Command *first;
	Draw_Command *last;
	Draw_RunState state;
	u32 command_count;
	b32 has_backdrops;
	b32 has_emission;
};

typedef struct
{
	Draw_Run *first;
	Draw_Run *last;
	u32 run_count;
	u32 command_count;
}
Draw_Frame;

struct Draw_Context
{
	GFX_Renderer *renderer;
	Arena pass_arena;
	Arena batch_arena;
	Arena instance_arena;
	Arena command_arena;

	GFX_Pass *passes;
	u32 pass_count;
	GFX_PassDesc active_pass;
	u32 active_pass_batch_offset;

	GFX_Batch *batches;
	u32 batch_count;
	GFX_BatchDesc active_batch;
	u32 active_batch_offset;

	GFX_Inst *instances;
	u32 instance_count;

	GFX_Texture *output;
	rect_i32 viewport;
	rect_i32 clip;
	rect_i32 clip_stack[DRAW_CLIP_STACK_CAPACITY];
	u32 clip_depth;
	Draw_BatchMetrics batch_metrics;

	Draw_Frame frame;
	i32 z;
	i32 z_stack[DRAW_LIST_Z_STACK_CAPACITY];
	u32 z_stack_count;
	rect_f32 list_clip_stack[DRAW_LIST_CLIP_STACK_CAPACITY];
	u32 list_clip_stack_count;
	u32 unclipped_scope_count;
	f32 emission;
	f32 emission_stack[DRAW_LIST_EMISSION_STACK_CAPACITY];
	u32 emission_stack_count;
	b32 commands_composed;

	b32 frame_active;
	b32 pass_active;
};

static b32 draw__arena_has_capacity(const Arena *arena, u64 size, u64 alignment)
{
	Assert(arena);
	Assert(arena->memory);
	Assert(alignment && !(alignment & (alignment - 1)));
	if (arena->position > arena->reserved_size) return false;
	u64 address = (u64)(uintptr_t)arena->memory + arena->position;
	u64 misalignment = address & (alignment - 1);
	u64 padding = misalignment ? alignment - misalignment : 0;
	u64 remaining = arena->reserved_size - arena->position;
	return padding <= remaining && size <= remaining - padding;
}

static void draw__require_arena_capacity(Draw_Context *draw, Arena *arena, u64 size, u64 alignment, const char *allocation)
{
	if (draw__arena_has_capacity(arena, size, alignment)) return;
	LOG_FATAL("draw overflow allocating %s from '%s': request=%llu used=%llu capacity=%llu passes=%u batches=%u instances=%u runs=%u commands=%u", allocation, arena->name, size, arena->position, arena->reserved_size, draw->pass_count, draw->batch_count, draw->instance_count, draw->frame.run_count, draw->frame.command_count);
	Assert(!"draw arena overflow");
}

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

	draw__require_arena_capacity(draw, &draw->batch_arena, sizeof(GFX_Batch), 1, "batch");
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

	GFX_BatchDesc previous = draw->active_batch;
	draw->batch_metrics.breaks++;
	draw->batch_metrics.texture += previous.texture != desc.texture;
	draw->batch_metrics.scissor += !rect_i32_equal(previous.scissor, desc.scissor);
	draw->batch_metrics.sampler += previous.sampler != desc.sampler;
	draw->batch_metrics.blender += previous.blender != desc.blender;
	draw->batch_metrics.shader += previous.shader != desc.shader;
	draw->batch_metrics.texture_mode += previous.texture_mode != desc.texture_mode;
	draw->batch_metrics.shader_block += !memory_match(&previous.shader_block, &desc.shader_block, sizeof(desc.shader_block));
	draw__flush_batch(draw);
	draw->active_batch = desc;
}

static void draw__push_instance(Draw_Context *draw, GFX_Inst instance)
{
	draw__require_arena_capacity(draw, &draw->instance_arena, sizeof(GFX_Inst), 1, "instance");
	GFX_Inst *destination = arena_push_aligned(&draw->instance_arena, sizeof(*destination), 1);
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

static UV_Coords draw__uv_from_region(GFX_Texture *texture, rect_f32 region)
{
	vec2i size = gfx_texture_size(texture);
	if (region.w == 0.f)
	{
		region.w = size.x - region.x;
	}
	if (region.h == 0.f)
	{
		region.h = size.y - region.y;
	}
	f32 x0 = region.x;
	f32 y0 = region.y;
	f32 x1 = region.x + region.w;
	f32 y1 = region.y + region.h;
	f32 epsilon = 0.001f;
	Assert(region.w >= 0.f && region.h >= 0.f);
	Assert(x0 >= -epsilon && y0 >= -epsilon);
	Assert(x1 <= size.x + epsilon && y1 <= size.y + epsilon);
	x0 = CLAMP(x0, 0.f, (f32)size.x);
	y0 = CLAMP(y0, 0.f, (f32)size.y);
	x1 = CLAMP(x1, 0.f, (f32)size.x);
	y1 = CLAMP(y1, 0.f, (f32)size.y);
	return (UV_Coords) {
		.u0 = x0 / size.x,
		.v0 = y0 / size.y,
		.u1 = x1 / size.x,
		.v1 = y1 / size.y,
	};
}

static void draw__set_all_colors(GFX_Inst *instance, Color_Linear color)
{
	for (u32 index = 0; index < _countof(instance->colors); ++index)
	{
		instance->colors[index] = color;
	}
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
	draw->command_arena = arena_create(DRAW_COMMAND_ARENA_CAPACITY, "draw command arena");
	return draw;
}

void draw_begin_frame(Draw_Context *draw)
{
	Assert(draw);
	Assert(!draw->frame_active);
	Assert(!draw->pass_active);
	arena_reset(&draw->pass_arena);
	arena_reset(&draw->batch_arena);
	arena_reset(&draw->instance_arena);
	arena_reset(&draw->command_arena);
	draw->passes = arena_base(&draw->pass_arena);
	draw->pass_count = 0;
	draw->batches = arena_base(&draw->batch_arena);
	draw->batch_count = 0;
	draw->instances = arena_base(&draw->instance_arena);
	draw->instance_count = 0;
	draw->active_batch_offset = 0;
	draw->batch_metrics = (Draw_BatchMetrics) { 0 };
	draw->frame = (Draw_Frame) { 0 };
	draw->z = 0;
	draw->z_stack_count = 0;
	draw->list_clip_stack_count = 0;
	draw->unclipped_scope_count = 0;
	draw->emission = 0.f;
	draw->emission_stack_count = 0;
	draw->commands_composed = false;
	draw->frame_active = true;
}

void draw_begin_pass(Draw_Context *draw, GFX_PassDesc desc)
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

void draw_end_pass(Draw_Context *draw)
{
	Assert(draw);
	Assert(draw->pass_active);
	Assert(draw->clip_depth == 0);
	draw->batch_metrics.pass_ends += draw->instance_count != draw->active_batch_offset;
	draw__flush_batch(draw);
	draw__require_arena_capacity(draw, &draw->pass_arena, sizeof(GFX_Pass), 1, "pass");
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

void draw_end_frame(Draw_Context *draw)
{
	Assert(draw);
	Assert(draw->frame_active);
	Assert(!draw->pass_active);
	Assert(draw->z_stack_count == 0);
	Assert(draw->list_clip_stack_count == 0);
	Assert(draw->unclipped_scope_count == 0);
	Assert(draw->emission_stack_count == 0);
	Assert(draw->commands_composed || draw->frame.command_count == 0);
	GFX_DrawData data = {
		.passes = draw->passes,
		.pass_count = draw->pass_count,
		.batches = draw->batches,
		.batch_count = draw->batch_count,
		.instances = draw->instances,
		.instances_size = draw->instance_count * sizeof(*draw->instances),
	};
	prof_add_metric(PROF_METRIC_DRAW_PASSES, draw->pass_count);
	prof_add_metric(PROF_METRIC_DRAW_BATCHES, draw->batch_count);
	prof_add_metric(PROF_METRIC_DRAW_CALLS, draw->batch_count);
	prof_add_metric(PROF_METRIC_DRAW_INSTANCES, draw->instance_count);
	prof_add_metric(PROF_METRIC_DRAW_INSTANCE_BYTES, data.instances_size);
	prof_add_metric(PROF_METRIC_DRAW_BATCH_BREAKS, draw->batch_metrics.breaks);
	prof_add_metric(PROF_METRIC_DRAW_BATCH_PASS_ENDS, draw->batch_metrics.pass_ends);
	prof_add_metric(PROF_METRIC_DRAW_BATCH_TEXTURE, draw->batch_metrics.texture);
	prof_add_metric(PROF_METRIC_DRAW_BATCH_SCISSOR, draw->batch_metrics.scissor);
	prof_add_metric(PROF_METRIC_DRAW_BATCH_SAMPLER, draw->batch_metrics.sampler);
	prof_add_metric(PROF_METRIC_DRAW_BATCH_BLENDER, draw->batch_metrics.blender);
	prof_add_metric(PROF_METRIC_DRAW_BATCH_SHADER, draw->batch_metrics.shader);
	prof_add_metric(PROF_METRIC_DRAW_BATCH_TEXTURE_MODE, draw->batch_metrics.texture_mode);
	prof_add_metric(PROF_METRIC_DRAW_BATCH_SHADER_BLOCK, draw->batch_metrics.shader_block);
	PROF_BLOCK("renderer submit") gfx_submit_draw(draw->renderer, data);
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
	b32 textured = params.texture != 0;
	GFX_Texture *texture = params.texture;
	GFX_Sampler sampler = params.sampler;
	if (textured)
	{
		if (sampler == GRAPHICS_SAMPLER_NONE) sampler = texture->sampler;
		if (sampler == GRAPHICS_SAMPLER_NONE) sampler = GRAPHICS_SAMPLER_LINEAR;
	}
	else
	{
		texture = draw->active_batch.texture ? draw->active_batch.texture : gfx_get_fallback_texture(draw->renderer);
		if (sampler == GRAPHICS_SAMPLER_NONE) sampler = draw->active_batch.sampler;
		if (sampler == GRAPHICS_SAMPLER_NONE) sampler = GRAPHICS_SAMPLER_LINEAR;
	}
	GFX_Blender blender = params.blender != GFX_BLENDER_NONE ? params.blender : GFX_BLENDER_ALPHA_BLEND;
	GFX_BatchDesc desc = draw__batch_desc(draw, texture, sampler, blender, GFX_SHADER_SDF_RECT, GRAPHICS_TEXTURE_RGBA);
	draw__set_batch_desc(draw, desc);
	GFX_Inst instance = {
		.dst = params.rect,
		.src = textured ? draw__uv_from_region(texture, params.texture_region) : (UV_Coords) { 0, 0, 1, 1 },
		.corner_radii = params.corner_radii,
		.border_thickness = params.border_thickness,
		.edge_softness = params.edge_softness,
		.disable_texture = textured ? 0.f : 1.f,
	};
	draw__set_all_colors(&instance, color_linear_from_srgba(params.color));
	draw__push_instance(draw, instance);
}

void draw_gradient(Draw_Context *draw, Draw_GradientParams params)
{
	Assert(params.axis == AXIS_X || params.axis == AXIS_Y);
	GFX_BatchDesc desc = draw__batch_desc(draw, gfx_get_fallback_texture(draw->renderer), GRAPHICS_SAMPLER_LINEAR, GFX_BLENDER_ALPHA_BLEND, GFX_SHADER_SDF_RECT, GRAPHICS_TEXTURE_RGBA);
	draw__set_batch_desc(draw, desc);
	GFX_Inst instance = {
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

void draw_barrel(Draw_Context *draw, Draw_BarrelParams params)
{
	Assert(draw);
	Assert(draw->pass_active);
	Assert(params.texture);
	GFX_BatchDesc desc = draw__batch_desc(draw, params.texture, GRAPHICS_SAMPLER_LINEAR, GFX_BLENDER_DISABLED, GFX_SHADER_BARREL, GRAPHICS_TEXTURE_RGBA);
	desc.shader_block.barrel.strength = params.strength;
	draw__set_batch_desc(draw, desc);
	GFX_Inst instance = {
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
	GFX_Inst instance = {
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
	GFX_Inst instance = {
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
	GFX_Inst instance = {
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
	GFX_Inst instance = {
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
	GFX_Inst instance = {
		.dst = rect_f32_from_i32(draw->viewport),
		.src = { 0, 0, 1, 1 },
	};
	draw__set_all_colors(&instance, color_linear_from_srgba(COLOR_WHITE));
	draw__push_instance(draw, instance);
}

static GFX_BatchParams draw__make_gaussian_block(Draw_GaussianBlurParams params)
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
	GFX_BatchParams block = {};
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
	GFX_Inst instance = {
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
	GFX_Inst instance = {
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
	GFX_Inst instance = {
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
		GFX_Inst instance = {
			.dst = rect_f32_translate(params.rects[index].destination, params.position),
			.src = params.rects[index].source,
			.grain = 1.f,
		};
		draw__set_all_colors(&instance, color);
		draw__push_instance(draw, instance);
	}
}

// Deferred draw list

static b32 draw__run_states_equal(Draw_RunState a, Draw_RunState b)
{
	return a.z == b.z && a.has_clip == b.has_clip && (!a.has_clip || rect_i32_equal(a.clip, b.clip));
}

static Draw_Command *draw__push_command(Draw_Context *draw, Draw_CommandKind kind, b32 inherit_clip)
{
	Assert(draw);
	Assert(draw->frame_active);
	Assert(!draw->commands_composed);

	Draw_RunState state = {
		.z = draw->z,
	};
	if (inherit_clip && draw->list_clip_stack_count && !draw->unclipped_scope_count)
	{
		state.has_clip = true;
		state.clip = rect_i32_from_f32(draw->list_clip_stack[draw->list_clip_stack_count - 1]);
	}

	Draw_Run *run = draw->frame.last;
	if (!run || !draw__run_states_equal(run->state, state))
	{
		draw__require_arena_capacity(draw, &draw->command_arena, sizeof(Draw_Run), ARENA_DEFAULT_ALIGNMENT, "run");
		run = arena_push_zero(&draw->command_arena, sizeof(*run));
		run->state = state;
		draw->frame.run_count++;
		prof_add_metric(PROF_METRIC_DRAW_RUNS, 1);
		prof_add_metric(PROF_METRIC_DRAW_RUN_BYTES, sizeof(*run));
		if (draw->frame.last) {
			draw->frame.last->next = run;
		} else {
			draw->frame.first = run;
		}
		draw->frame.last = run;
	}

	draw__require_arena_capacity(draw, &draw->command_arena, sizeof(Draw_Command), ARENA_DEFAULT_ALIGNMENT, "command");
	Draw_Command *command = arena_push_zero(&draw->command_arena, sizeof(*command));
	command->kind = kind;
	prof_add_metric(PROF_METRIC_DRAW_COMMANDS, 1);
	prof_add_metric(PROF_METRIC_DRAW_COMMAND_BYTES, sizeof(*command));
	switch (kind)
	{
		case DRAW_COMMAND_RECT:         prof_add_metric(PROF_METRIC_DRAW_RECT_COMMANDS, 1); command->emission = draw->emission; break;
		case DRAW_COMMAND_TEXT:         prof_add_metric(PROF_METRIC_DRAW_TEXT_COMMANDS, 1); command->emission = draw->emission; break;
		case DRAW_COMMAND_INSET_SHADOW:
		case DRAW_COMMAND_BACKDROP:     prof_add_metric(PROF_METRIC_DRAW_EFFECT_COMMANDS, 1); break;
		default: Assert(!"invalid draw command");
	}
	if (command->emission > 0.f) {
		prof_add_metric(PROF_METRIC_DRAW_EMISSIVE_COMMANDS, 1);
	}
	if (state.has_clip) {
		prof_add_metric(PROF_METRIC_DRAW_CLIPPED_COMMANDS, 1);
	}

	run->has_backdrops |= kind == DRAW_COMMAND_BACKDROP;
	run->has_emission |= command->emission > 0.f;
	if (run->last) {
		run->last->next = command;
	} else {
		run->first = command;
	}
	run->last = command;
	run->command_count += 1;
	draw->frame.command_count += 1;
	return command;
}

void draw_list_push_z(Draw_Context *draw, i32 z)
{
	Assert(draw);
	Assert(draw->frame_active);
	Assert(draw->z_stack_count < ArrayCount(draw->z_stack));
	draw->z_stack[draw->z_stack_count++] = draw->z;
	draw->z = z;
}

void draw_list_pop_z(Draw_Context *draw)
{
	Assert(draw);
	Assert(draw->frame_active);
	Assert(draw->z_stack_count > 0);
	draw->z = draw->z_stack[--draw->z_stack_count];
}

void draw_list_push_clip(Draw_Context *draw, rect_f32 clip)
{
	Assert(draw);
	Assert(draw->frame_active);
	Assert(draw->list_clip_stack_count < ArrayCount(draw->list_clip_stack));
	if (draw->list_clip_stack_count)
	{
		rect_i32 parent = rect_i32_from_f32(draw->list_clip_stack[draw->list_clip_stack_count - 1]);
		rect_i32 child = rect_i32_from_f32(clip);
		clip = rect_f32_from_i32(rect_i32_intersect(parent, child));
	}
	draw->list_clip_stack[draw->list_clip_stack_count++] = clip;
}

void draw_list_pop_clip(Draw_Context *draw)
{
	Assert(draw);
	Assert(draw->frame_active);
	Assert(draw->list_clip_stack_count > 0);
	draw->list_clip_stack_count -= 1;
}

void draw_list_push_unclipped(Draw_Context *draw)
{
	Assert(draw);
	Assert(draw->frame_active);
	draw->unclipped_scope_count += 1;
}

void draw_list_pop_unclipped(Draw_Context *draw)
{
	Assert(draw);
	Assert(draw->frame_active);
	Assert(draw->unclipped_scope_count > 0);
	draw->unclipped_scope_count -= 1;
}

void draw_list_push_emission(Draw_Context *draw, f32 emission)
{
	Assert(draw);
	Assert(draw->frame_active);
	Assert(draw->emission_stack_count < ArrayCount(draw->emission_stack));
	draw->emission_stack[draw->emission_stack_count++] = draw->emission;
	draw->emission = emission;
}

void draw_list_pop_emission(Draw_Context *draw)
{
	Assert(draw);
	Assert(draw->frame_active);
	Assert(draw->emission_stack_count > 0);
	draw->emission = draw->emission_stack[--draw->emission_stack_count];
}

void draw_list_rect(Draw_Context *draw, Draw_RectParams params)
{
	Draw_Command *command = draw__push_command(draw, DRAW_COMMAND_RECT, true);
	command->rect = params;
}

void draw_list_text(Draw_Context *draw, Draw_TextParams params)
{
	Draw_Command *command = draw__push_command(draw, DRAW_COMMAND_TEXT, true);
	command->text = params;
}

void draw_list_inset_shadow(Draw_Context *draw, Draw_InsetShadowParams params)
{
	Draw_Command *command = draw__push_command(draw, DRAW_COMMAND_INSET_SHADOW, true);
	command->inset_shadow = params;
}

void draw_list_backdrop(Draw_Context *draw, Draw_BackdropParams params)
{
	Draw_Command *command = draw__push_command(draw, DRAW_COMMAND_BACKDROP, false);
	command->backdrop = params;
}

// Composition

static GFX_Texture *draw__acquire_pass_output(Draw_Context *draw, vec2i size, GFX_Sampler sampler, const char *label)
{
	return gfx_acquire_transient_texture(draw->renderer, (GFX_TextureDesc) {
		.usage = GRAPHICS_TEXTURE_USAGE_RARE_UPDATES,
		.bind_flags = GFX_TEXTURE_BIND_INPUT | GFX_TEXTURE_BIND_OUTPUT,
		.format = GRAPHICS_FORMAT_RGBA_F32,
		.size = size,
		.sampler = sampler,
		.label = label,
	});
}

static GFX_Texture *draw__copy_pass(Draw_Context *draw, GFX_Texture *input, vec2i output_size, const char *label)
{
	GFX_Texture *output = draw__acquire_pass_output(draw, output_size, GRAPHICS_SAMPLER_LINEAR, label);
	draw_begin_pass(draw, (GFX_PassDesc) { .output = output });
	draw_texture_copy(draw, input);
	draw_end_pass(draw);
	return output;
}

static GFX_Texture *draw__gaussian_blur_pass(Draw_Context *draw, GFX_Texture *input, vec2 direction, f32 sigma, const char *label)
{
	GFX_Texture *output = draw__acquire_pass_output(draw, gfx_texture_size(input), GRAPHICS_SAMPLER_LINEAR, label);
	draw_begin_pass(draw, (GFX_PassDesc) { .output = output, .clear = true, .clear_color = COLOR_BLACK });
	draw_gaussian_blur(draw, (Draw_GaussianBlurParams) { .texture = input, .direction = direction, .sigma = sigma });
	draw_end_pass(draw);
	return output;
}

static GFX_Texture *draw__backdrop_blur_pass(Draw_Context *draw, GFX_Texture *frame_texture)
{
	prof_add_metric(PROF_METRIC_DRAW_BACKDROP_BLURS, 1);
	vec2i size = gfx_texture_size(frame_texture);
	vec2i blur_size = v2i(Max(2, (size.x + 1) / 2), Max(2, (size.y + 1) / 2));
	GFX_Texture *blurred = draw__copy_pass(draw, frame_texture, blur_size, "backdrop downsample");

	for (u32 round = 0; round < 2; ++round)
	{
		blurred = draw__gaussian_blur_pass(draw, blurred, v2(1.f, 0.f), 3.f, "backdrop blur horizontal");
		blurred = draw__gaussian_blur_pass(draw, blurred, v2(0.f, 1.f), 3.f, "backdrop blur vertical");
	}
	return blurred;
}

static Color_SRGBA draw__emission_color(Color_SRGBA color, f32 emission)
{
	color.r = color_encode_srgb_channel(color_decode_srgb_channel(color.r) * emission);
	color.g = color_encode_srgb_channel(color_decode_srgb_channel(color.g) * emission);
	color.b = color_encode_srgb_channel(color_decode_srgb_channel(color.b) * emission);
	return color;
}

static void draw__sort_runs_by_z(Draw_Run **runs, u32 run_count)
{
	for (u32 index = 1; index < run_count; ++index)
	{
		Draw_Run *run = runs[index];
		u32 insert_index = index;
		while (insert_index && runs[insert_index - 1]->state.z > run->state.z)
		{
			runs[insert_index] = runs[insert_index - 1];
			insert_index--;
		}
		runs[insert_index] = run;
	}
}

static void draw__replay_runs(Draw_Context *draw, Text_GFX *text_gfx, Draw_Run **runs, u32 run_count, GFX_Texture *backdrop_texture, b32 emission_only)
{
	u32 replay_count = 0;
	PROF_BLOCK("draw__replay_runs")
	for (u32 run_index = 0; run_index < run_count; ++run_index)
	{
		Draw_Run *run = runs[run_index];
		if (emission_only && !run->has_emission) {
			continue;
		}
		replay_count += run->command_count;
		if (run->state.has_clip) {
			draw_push_clip(draw, rect_f32_from_i32(run->state.clip));
		}

		for (Draw_Command *command = run->first; command; command = command->next)
		{
			if (emission_only && command->emission <= 0.f) {
				continue;
			}
			switch (command->kind)
			{
				case DRAW_COMMAND_RECT:
				{
					Draw_RectParams params = command->rect;
					if (emission_only) params.color = draw__emission_color(params.texture ? COLOR_WHITE : params.color, command->emission);
					draw_rect(draw, params);
				} break;
				case DRAW_COMMAND_TEXT:
				{
					Assert(text_gfx);
					text_gfx_draw_run(text_gfx, draw, command->text.run, command->text.position,
						emission_only ? draw__emission_color(command->text.color, command->emission) : command->text.color);
				} break;
				case DRAW_COMMAND_INSET_SHADOW:
				{
					if (!emission_only) draw_inset_shadow(draw, command->inset_shadow.rect, command->inset_shadow.strength);
				} break;
				case DRAW_COMMAND_BACKDROP:
				{
					if (emission_only) break;
					Assert(backdrop_texture);
					draw_glass(draw, (Draw_GlassParams) {
						.texture = backdrop_texture,
						.rect = command->backdrop.rect,
						.corner_radius = command->backdrop.corner_radius,
						.distortion = command->backdrop.distortion,
						.distortion_width = command->backdrop.distortion_width,
						.saturation = command->backdrop.saturation,
						.tint = command->backdrop.tint,
						.grain = command->backdrop.grain,
						.highlight = command->backdrop.highlight,
						.shadow = command->backdrop.shadow,
					});
				} break;
				default: Assert(!"invalid draw command");
			}
		}

		if (run->state.has_clip) {
			draw_pop_clip(draw);
		}
	}
	prof_add_metric(PROF_METRIC_DRAW_COMMAND_REPLAYS, replay_count);
}

static void draw__bloom_pass(Draw_Context *draw, Text_GFX *text_gfx, Draw_Run **runs, u32 run_count, b32 has_emission, GFX_Texture *frame_texture, rect_f32 output_rect)
{
	if (!has_emission) {
		return;
	}
	prof_add_metric(PROF_METRIC_DRAW_BLOOM_SLICES, 1);

	vec2i size = gfx_texture_size(frame_texture);
	vec2i blur_size = v2i(Max(2, (size.x + 1) / 2), Max(2, (size.y + 1) / 2));
	GFX_Texture *emission = draw__acquire_pass_output(draw, size, GRAPHICS_SAMPLER_LINEAR, "draw emission");
	draw_begin_pass(draw, (GFX_PassDesc) { .output = emission, .clear = true, .clear_color = COLOR_TRANSPARENT });
	draw__replay_runs(draw, text_gfx, runs, run_count, 0, true);
	draw_end_pass(draw);

	GFX_Texture *bloom = draw__copy_pass(draw, emission, blur_size, "draw emission downsample");
	bloom = draw__gaussian_blur_pass(draw, bloom, v2(1.f, 0.f), 5.f, "draw bloom horizontal");
	bloom = draw__gaussian_blur_pass(draw, bloom, v2(0.f, 1.f), 5.f, "draw bloom vertical");

	draw_begin_pass(draw, (GFX_PassDesc) { .output = frame_texture });
	draw_rect(draw, (Draw_RectParams) {
		.rect = output_rect,
		.texture = bloom,
		.texture_region = rect_f32_from_i32(rect_i32_from_size(gfx_texture_size(bloom))),
		.color = color_srgba(0xB8B8B8),
		.sampler = GRAPHICS_SAMPLER_LINEAR,
		.blender = GFX_BLENDER_ADDITIVE,
	});
	draw_end_pass(draw);
}

void draw_compose(Draw_Context *draw, Text_GFX *text_gfx, GFX_Texture *output, rect_f32 output_rect)
{
	Assert(draw);
	Assert(output);
	Assert(draw->frame_active);
	Assert(!draw->pass_active);
	Assert(!draw->commands_composed);
	Assert(draw->z_stack_count == 0);
	Assert(draw->list_clip_stack_count == 0);
	Assert(draw->unclipped_scope_count == 0);
	Assert(draw->emission_stack_count == 0);

	PROF_BLOCK("draw_compose")
	{
		u32 run_count = draw->frame.run_count;
		u64 runs_size = (u64)sizeof(Draw_Run *) * run_count;
		if (runs_size) draw__require_arena_capacity(draw, &draw->command_arena, runs_size, ARENA_DEFAULT_ALIGNMENT, "composition run index");
		Draw_Run **runs = run_count ? arena_push(&draw->command_arena, runs_size) : 0;
		u32 run_index = 0;
		for (Draw_Run *run = draw->frame.first; run; run = run->next) {
			runs[run_index++] = run;
		}
		Assert(run_index == run_count);
		PROF_BLOCK("draw__sort_runs_by_z") draw__sort_runs_by_z(runs, run_count);

		PROF_BLOCK("draw__slices")
		for (u32 slice_begin = 0; slice_begin < run_count;)
		{
			u32 slice_end = slice_begin;
			b32 has_backdrops = false;
			b32 has_emission = false;
			while (slice_end < run_count && runs[slice_end]->state.z == runs[slice_begin]->state.z)
			{
				has_backdrops |= runs[slice_end]->has_backdrops;
				has_emission |= runs[slice_end]->has_emission;
				slice_end++;
			}

			u32 slice_run_count = slice_end - slice_begin;
			GFX_Texture *backdrop = has_backdrops ? draw__backdrop_blur_pass(draw, output) : 0;
			draw_begin_pass(draw, (GFX_PassDesc) { .output = output });
			draw__replay_runs(draw, text_gfx, runs + slice_begin, slice_run_count, backdrop, false);
			draw_end_pass(draw);
			draw__bloom_pass(draw, text_gfx, runs + slice_begin, slice_run_count, has_emission, output, output_rect);
			slice_begin = slice_end;
		}
	}

	draw->commands_composed = true;
}
