#include "ui_box.h"
#include "ui.h"

static f32 ui_box__clamp(f32 value, f32 minimum, f32 maximum)
{
	return Max(minimum, Min(value, maximum));
}

static f32 ui_box__local_min(const UI_Box *box, AXIS axis)
{
	return Max(0.f, box->desc.min_size.xy[axis]);
}

static f32 ui_box__local_max(const UI_Box *box, AXIS axis)
{
	return Max(ui_box__local_min(box, axis), box->desc.max_size.xy[axis]);
}

static b32 ui_box__is_absolute(const UI_Box *box, AXIS axis)
{
	return box->desc.position[axis].kind == UI_BOX_POSITION_ABSOLUTE;
}

UI_BoxSize ui_wrap(void)
{
	return (UI_BoxSize) { .kind = UI_BOX_SIZE_CONTENT };
}

UI_BoxSize ui_fixed(f32 value)
{
	return (UI_BoxSize) { .kind = UI_BOX_SIZE_PIXELS, .value = Max(0.f, value) };
}

UI_BoxSize ui_grow(f32 grow)
{
	return (UI_BoxSize) { .kind = UI_BOX_SIZE_FILL, .grow = Max(0.f, grow) };
}

UI_BoxSize ui_flex(f32 grow, f32 shrink)
{
	return (UI_BoxSize) {
		.kind = UI_BOX_SIZE_CONTENT,
		.grow = Max(0.f, grow),
		.shrink = Max(0.f, shrink),
	};
}

UI_BoxDesc ui_defaults(void)
{
	return (UI_BoxDesc) {
		.size = { { .kind = UI_BOX_SIZE_CONTENT }, { .kind = UI_BOX_SIZE_CONTENT } },
		.max_size = v2(UI_BOX_INFINITY, UI_BOX_INFINITY),
		.axis = AXIS_Y,
	};
}

UI_BoxPaintDesc ui_default_paint(void)
{
	return (UI_BoxPaintDesc) {
		.border_width = 1.f,
		.edge_softness = 0.5f,
	};
}

static UI_Box *ui_box__allocate_box(UI_BoxBuilder *builder, UI_Id id, UI_Key key, String name, UI_BoxDesc desc)
{
	UI_Box *box = arena_push_zero(builder->arena, sizeof(*box));
	box->id = id;
	box->key = key;
	box->name = name;
	box->desc = desc;
	box->paint = builder->paint;
	box->ui = builder->ui;
	if (box->ui)
	{
		box->state = ui_box_state_get(box->ui, box->id);
		box->has_previous = box->state->last_layout_frame + 1 == box->ui->frame_index && box->state->layout_generation == box->ui->layout_generation;
		box->scroll_offset = box->state->view_offset;
	}
	return box;
}

static void ui_box__append_child(UI_Box *parent, UI_Box *child)
{
	Assert(parent);
	Assert(child);
	Assert(!child->parent);
	Assert(!child->next);
	Assert(!child->prev);
	child->parent = parent;
	child->prev = parent->last;
	if (parent->last) parent->last->next = child;
	else parent->first = child;
	parent->last = child;
	parent->child_count++;
}

void ui_box_clear_children(UI_Box *parent)
{
	Assert(parent);
	for (UI_Box *child = parent->first, *next; child; child = next)
	{
		next = child->next;
		child->parent = 0;
		child->next = 0;
		child->prev = 0;
	}
	parent->first = 0;
	parent->last = 0;
	parent->child_count = 0;
}

UI_Box *ui_box_builder_begin(UI_BoxBuilder *builder, Arena *arena, UI_Context *ui, UI_Key root_key, String root_name, UI_BoxDesc root_desc)
{
	Assert(builder);
	Assert(arena);
	memory_zero(builder, sizeof(*builder));
	builder->arena = arena;
	builder->ui = ui;
	builder->desc = ui_defaults();
	builder->paint = ui_default_paint();
	builder->id = ui_id_child(UI_ID_NONE, root_key);
	builder->root = ui_box__allocate_box(builder, builder->id, root_key, root_name, root_desc);
	builder->parent = builder->root;
	return builder->root;
}

UI_Box *ui_builder_box_make_desc(UI_BoxBuilder *builder, UI_Key key, String name, UI_BoxDesc desc)
{
	Assert(builder);
	Assert(builder->parent);
	UI_Id id = ui_id_child(builder->id, key);
	for (UI_Box *sibling = builder->parent->first; sibling; sibling = sibling->next) {
		Assert(!ui_id_equal(sibling->id, id));
	}
	UI_Box *box = ui_box__allocate_box(builder, id, key, name, desc);
	ui_box__append_child(builder->parent, box);
	return box;
}

UI_Box *ui_builder_box_make(UI_BoxBuilder *builder, UI_Key key, String name)
{
	return ui_builder_box_make_desc(builder, key, name, builder->desc);
}

UI_Box *ui_builder_box_begin_desc(UI_BoxBuilder *builder, UI_Key key, String name, UI_BoxDesc desc)
{
	UI_Box *box = ui_builder_box_make_desc(builder, key, name, desc);
	Assert(builder->parent_count < ArrayCount(builder->parent_id_stack));
	builder->parent_id_stack[builder->parent_count++] = builder->id;
	builder->parent = box;
	builder->id = box->id;
	return box;
}

UI_Box *ui_builder_box_begin(UI_BoxBuilder *builder, UI_Key key, String name)
{
	return ui_builder_box_begin_desc(builder, key, name, builder->desc);
}

void ui_builder_box_end(UI_BoxBuilder *builder)
{
	Assert(builder);
	Assert(builder->parent_count);
	builder->parent_count--;
	builder->parent = builder->parent->parent;
	builder->id = builder->parent_id_stack[builder->parent_count];
}

UI_Box *ui_box_builder_end(UI_BoxBuilder *builder)
{
	Assert(builder);
	Assert(builder->root);
	Assert(builder->parent == builder->root);
	Assert(builder->parent_count == 0);
	Assert(builder->id_count == 0);
	Assert(builder->desc_count == 0);
	Assert(ui_id_equal(builder->id, builder->root->id));
	return builder->root;
}

void ui_builder_push_id(UI_BoxBuilder *builder, UI_Key key)
{
	Assert(builder);
	Assert(builder->id_count < ArrayCount(builder->id_stack));
	builder->id_stack[builder->id_count++] = builder->id;
	builder->id = ui_id_child(builder->id, key);
}

void ui_builder_pop_id(UI_BoxBuilder *builder)
{
	Assert(builder);
	Assert(builder->id_count);
	builder->id = builder->id_stack[--builder->id_count];
}

void ui_builder_push(UI_BoxBuilder *builder)
{
	Assert(builder);
	Assert(builder->desc_count < ArrayCount(builder->desc_stack));
	builder->desc_stack[builder->desc_count] = builder->desc;
	builder->paint_stack[builder->desc_count++] = builder->paint;
}

void ui_builder_pop(UI_BoxBuilder *builder)
{
	Assert(builder);
	Assert(builder->desc_count);
	builder->desc_count--;
	builder->desc = builder->desc_stack[builder->desc_count];
	builder->paint = builder->paint_stack[builder->desc_count];
}

void ui_builder_size(UI_BoxBuilder *builder, AXIS axis, UI_BoxSize size)
{
	Assert(builder);
	builder->desc.size[axis] = size;
}

void ui_builder_position(UI_BoxBuilder *builder, AXIS axis, f32 position)
{
	Assert(builder);
	builder->desc.position[axis] = (UI_BoxPosition) {
		.kind = UI_BOX_POSITION_ABSOLUTE,
		.value = position,
	};
}

void ui_builder_rect(UI_BoxBuilder *builder, rect_f32 rect)
{
	Assert(builder);
	ui_builder_position(builder, AXIS_X, rect.x);
	ui_builder_position(builder, AXIS_Y, rect.y);
	ui_builder_size(builder, AXIS_X, ui_fixed(rect.w));
	ui_builder_size(builder, AXIS_Y, ui_fixed(rect.h));
}

void ui_builder_min_size(UI_BoxBuilder *builder, AXIS axis, f32 size)
{
	Assert(builder);
	builder->desc.min_size.xy[axis] = size;
}

void ui_builder_max_size(UI_BoxBuilder *builder, AXIS axis, f32 size)
{
	Assert(builder);
	builder->desc.max_size.xy[axis] = size;
}

void ui_builder_margin(UI_BoxBuilder *builder, AXIS axis, f32 before, f32 after)
{
	Assert(builder);
	builder->desc.margin[axis][0] = before;
	builder->desc.margin[axis][1] = after;
}

void ui_builder_padd(UI_BoxBuilder *builder, AXIS axis, f32 before, f32 after)
{
	Assert(builder);
	builder->desc.padd[axis][0] = before;
	builder->desc.padd[axis][1] = after;
}

void ui_builder_axis(UI_BoxBuilder *builder, AXIS axis)
{
	Assert(builder);
	builder->desc.axis = axis;
}

void ui_builder_gap(UI_BoxBuilder *builder, f32 gap)
{
	Assert(builder);
	builder->desc.gap = gap;
}

void ui_builder_perp_align(UI_BoxBuilder *builder, f32 align)
{
	Assert(builder);
	builder->desc.perp_align = align;
}

void ui_builder_overflow(UI_BoxBuilder *builder, AXIS axis, UI_BoxOverflow overflow)
{
	Assert(builder);
	builder->desc.overflow[axis] = overflow;
}

void ui_builder_background(UI_BoxBuilder *builder, Color_SRGBA color)
{
	Assert(builder);
	builder->paint.flags |= UI_BOX_DRAW_BACKGROUND;
	builder->paint.background = color;
}

void ui_builder_border(UI_BoxBuilder *builder, Color_SRGBA color, f32 width)
{
	Assert(builder);
	builder->paint.flags |= UI_BOX_DRAW_BORDER;
	builder->paint.border = color;
	builder->paint.border_width = Max(0.f, width);
}

void ui_builder_backdrop(UI_BoxBuilder *builder, f32 roundness)
{
	Assert(builder);
	builder->paint.flags |= UI_BOX_DRAW_BACKDROP;
	builder->paint.roundness = Max(0.f, roundness);
}

void ui_builder_roundness(UI_BoxBuilder *builder, f32 roundness)
{
	Assert(builder);
	builder->paint.roundness = Max(0.f, roundness);
}

void ui_builder_edge_softness(UI_BoxBuilder *builder, f32 edge_softness)
{
	Assert(builder);
	builder->paint.edge_softness = Max(0.f, edge_softness);
}

void ui_builder_inset_shadow(UI_BoxBuilder *builder, f32 strength)
{
	Assert(builder);
	builder->paint.flags |= UI_BOX_DRAW_INSET_SHADOW;
	builder->paint.inset_shadow = Max(0.f, strength);
}

void ui_builder_emission(UI_BoxBuilder *builder, f32 emission)
{
	Assert(builder);
	builder->paint.emission = Max(0.f, emission);
}

void ui_builder_paint_z(UI_BoxBuilder *builder, i32 z)
{
	Assert(builder);
	builder->paint.z = z;
}

static UI_BoxBuilder *ui_box__builder(UI_Context *ui)
{
	Assert(ui);
	Assert(ui->builder);
	Assert(ui->builder->ui == ui);
	return ui->builder;
}

UI_Box *ui_build_begin(UI_Context *ui, UI_Key root_key, String root_name, UI_BoxDesc root_desc)
{
	static const UI_Key overlay_root_key = 0x4F5645524C415900ull;
	Assert(ui);
	Assert(!ui->builder);
	Assert(!ui->root);
	Assert(!ui->overlay_root);
	UI_BoxBuilder *builder = arena_push_zero(&ui->frame_arena, sizeof(*builder));
	ui->builder = builder;
	UI_Box *root = ui_box_builder_begin(builder, &ui->frame_arena, ui, root_key, root_name, root_desc);
	ui->root = root;

	UI_BoxDesc overlay_desc = ui_defaults();
	overlay_desc.position[AXIS_X] = (UI_BoxPosition) { .kind = UI_BOX_POSITION_ABSOLUTE };
	overlay_desc.position[AXIS_Y] = (UI_BoxPosition) { .kind = UI_BOX_POSITION_ABSOLUTE };
	UI_Id overlay_id = ui_id_child(root->id, overlay_root_key);
	ui->overlay_root = ui_box__allocate_box(builder, overlay_id, overlay_root_key, LIT("UI overlay root"), overlay_desc);
	return root;
}

UI_Box *ui_build_end(UI_Context *ui)
{
	UI_BoxBuilder *builder = ui_box__builder(ui);
	Assert(!ui->tooltip_open);
	UI_Box *root = ui_box_builder_end(builder);
	for (UI_Box *child = root->first; child; child = child->next) {
		Assert(!ui_id_equal(child->id, ui->overlay_root->id));
	}
	ui_box__append_child(root, ui->overlay_root);
	ui->builder = 0;
	return root;
}

UI_Box *ui_box_make(UI_Context *ui, UI_Key key, String name)
{
	return ui_builder_box_make(ui_box__builder(ui), key, name);
}

UI_Box *ui_box_begin(UI_Context *ui, UI_Key key, String name)
{
	return ui_builder_box_begin(ui_box__builder(ui), key, name);
}

UI_Box *ui_box_make_desc(UI_Context *ui, UI_Key key, String name, UI_BoxDesc desc)
{
	return ui_builder_box_make_desc(ui_box__builder(ui), key, name, desc);
}

UI_Box *ui_box_begin_desc(UI_Context *ui, UI_Key key, String name, UI_BoxDesc desc)
{
	return ui_builder_box_begin_desc(ui_box__builder(ui), key, name, desc);
}

void ui_box_end(UI_Context *ui)
{
	ui_builder_box_end(ui_box__builder(ui));
}

void ui_box_push_id(UI_Context *ui, UI_Key key)
{
	ui_builder_push_id(ui_box__builder(ui), key);
}

void ui_box_pop_id(UI_Context *ui)
{
	ui_builder_pop_id(ui_box__builder(ui));
}

void ui_push(UI_Context *ui)
{
	ui_builder_push(ui_box__builder(ui));
}

void ui_pop(UI_Context *ui)
{
	ui_builder_pop(ui_box__builder(ui));
}

void ui_size(UI_Context *ui, AXIS axis, UI_BoxSize size)
{
	ui_builder_size(ui_box__builder(ui), axis, size);
}

void ui_position(UI_Context *ui, AXIS axis, f32 position)
{
	ui_builder_position(ui_box__builder(ui), axis, position);
}

void ui_rect(UI_Context *ui, rect_f32 rect)
{
	ui_builder_rect(ui_box__builder(ui), rect);
}

void ui_min_size(UI_Context *ui, AXIS axis, f32 size)
{
	ui_builder_min_size(ui_box__builder(ui), axis, size);
}

void ui_max_size(UI_Context *ui, AXIS axis, f32 size)
{
	ui_builder_max_size(ui_box__builder(ui), axis, size);
}

void ui_margin(UI_Context *ui, AXIS axis, f32 before, f32 after)
{
	ui_builder_margin(ui_box__builder(ui), axis, before, after);
}

void ui_padd(UI_Context *ui, AXIS axis, f32 before, f32 after)
{
	ui_builder_padd(ui_box__builder(ui), axis, before, after);
}

void ui_axis(UI_Context *ui, AXIS axis)
{
	ui_builder_axis(ui_box__builder(ui), axis);
}

void ui_gap(UI_Context *ui, f32 gap)
{
	ui_builder_gap(ui_box__builder(ui), gap);
}

void ui_perp_align(UI_Context *ui, f32 align)
{
	ui_builder_perp_align(ui_box__builder(ui), align);
}

void ui_overflow(UI_Context *ui, AXIS axis, UI_BoxOverflow overflow)
{
	ui_builder_overflow(ui_box__builder(ui), axis, overflow);
}

void ui_background(UI_Context *ui, Color_SRGBA color)
{
	ui_builder_background(ui_box__builder(ui), color);
}

void ui_border(UI_Context *ui, Color_SRGBA color, f32 width)
{
	ui_builder_border(ui_box__builder(ui), color, width);
}

void ui_backdrop(UI_Context *ui, f32 roundness)
{
	ui_builder_backdrop(ui_box__builder(ui), roundness);
}

void ui_roundness(UI_Context *ui, f32 roundness)
{
	ui_builder_roundness(ui_box__builder(ui), roundness);
}

void ui_edge_softness(UI_Context *ui, f32 edge_softness)
{
	ui_builder_edge_softness(ui_box__builder(ui), edge_softness);
}

void ui_inset_shadow(UI_Context *ui, f32 strength)
{
	ui_builder_inset_shadow(ui_box__builder(ui), strength);
}

void ui_emission(UI_Context *ui, f32 emission)
{
	ui_builder_emission(ui_box__builder(ui), emission);
}

void ui_paint_z(UI_Context *ui, i32 z)
{
	ui_builder_paint_z(ui_box__builder(ui), z);
}

vec2 ui_box_measure(UI_Box *box, UI_BoxConstraints constraints)
{
	Assert(box);
	for (AXIS axis = AXIS_X; axis <= AXIS_Y; ++axis)
	{
		constraints.min.xy[axis] = Max(0.f, constraints.min.xy[axis]);
		constraints.max.xy[axis] = Max(constraints.min.xy[axis], constraints.max.xy[axis]);
	}

	vec2 natural = box->intrinsic_size;
	vec2 content = box->intrinsic_size;
	vec2 padd = v2(box->desc.horz_padd[0] + box->desc.horz_padd[1], box->desc.vert_padd[0] + box->desc.vert_padd[1]);
	UI_BoxConstraints content_constraints = {
		.min = v2(Max(0.f, constraints.min.x - padd.x), Max(0.f, constraints.min.y - padd.y)),
		.max = v2(Max(0.f, constraints.max.x - padd.x), Max(0.f, constraints.max.y - padd.y)),
	};
	if (box->ops && box->ops->measure)
	{
		vec2 measured_content = box->ops->measure(box, content_constraints);
		natural.x = Max(natural.x, measured_content.x);
		natural.y = Max(natural.y, measured_content.y);
	}
	if (box->ops && box->ops->measure_children)
	{
		content = box->ops->measure_children(box, content_constraints);
		natural.x = Max(natural.x, content.x);
		natural.y = Max(natural.y, content.y);
	}
	else if (box->child_count)
	{
		AXIS main_axis = box->desc.axis;
		AXIS perp_axis = !main_axis;

		vec2 available = v2(Max(0.f, constraints.max.x - box->desc.horz_padd[0] - box->desc.horz_padd[1]), Max(0.f, constraints.max.y - box->desc.vert_padd[0] - box->desc.vert_padd[1]));

		content = v2(0.f, 0.f);
		u32 flow_child_count = 0;
		for (UI_Box *child = box->first; child; child = child->next) {
			flow_child_count += !ui_box__is_absolute(child, main_axis);
		}
		if (flow_child_count) content.xy[main_axis] = box->desc.gap * (flow_child_count - 1);

		for (UI_Box *child = box->first; child; child = child->next)
		{
			UI_BoxConstraints child_constraints = { .max = available, };
			child_constraints.max.xy[main_axis] = UI_BOX_INFINITY;

			vec2 child_size = ui_box_measure(child, child_constraints);
			child_size.xy[main_axis] += child->desc.margin[main_axis][0] + child->desc.margin[main_axis][1];
			child_size.xy[perp_axis] += child->desc.margin[perp_axis][0] + child->desc.margin[perp_axis][1];

			if (!ui_box__is_absolute(child, main_axis)) content.xy[main_axis] += child_size.xy[main_axis];
			if (!ui_box__is_absolute(child, perp_axis)) content.xy[perp_axis] = Max(content.xy[perp_axis], child_size.xy[perp_axis]);
		}
		natural.x = Max(natural.x, content.x);
		natural.y = Max(natural.y, content.y);
	}
	natural.x += box->desc.horz_padd[0] + box->desc.horz_padd[1];
	natural.y += box->desc.vert_padd[0] + box->desc.vert_padd[1];

	vec2 measured = {};
	for (AXIS axis = AXIS_X; axis <= AXIS_Y; ++axis)
	{
		UI_BoxSize spec = box->desc.size[axis];
		f32 desired = natural.xy[axis];
		if (spec.kind == UI_BOX_SIZE_PIXELS) {
			desired = spec.value;
		}
		else if (spec.kind == UI_BOX_SIZE_FILL) {
			desired = ui_box__local_min(box, axis);
		}
		desired = ui_box__clamp(desired, ui_box__local_min(box, axis), ui_box__local_max(box, axis));
		measured.xy[axis] = ui_box__clamp(desired, constraints.min.xy[axis], constraints.max.xy[axis]);
	}
	box->measured_size = measured;
	box->arranged_size = measured;
	return measured;
}

static f32 ui_box__distribute(UI_Box *box, AXIS axis, f32 free_size)
{
	b32 growing = free_size > 0.f;
	for (u32 pass = 0; pass < box->child_count && fabsf(free_size) > 0.001f; pass ++)
	{
		f32 total_weight = 0.f;
		for (UI_Box *child = box->first; child; child = child->next)
		{
			if (ui_box__is_absolute(child, axis)) continue;
			f32 size = child->arranged_size.xy[axis];
			f32 weight = growing ? child->desc.size[axis].grow : child->desc.size[axis].shrink;
			f32 bound = growing ? ui_box__local_max(child, axis) : ui_box__local_min(child, axis);
			if (weight > 0.f && (fabsf(bound - size) > 0.001f)) {
				total_weight += weight;
			}
		}
		if (total_weight <= 0.f) {
			break;
		}

		f32 unit = free_size / total_weight;
		f32 correction = 0.f;
		for (UI_Box *child = box->first; child; child = child->next)
		{
			if (ui_box__is_absolute(child, axis)) continue;
			f32 weight = growing ? child->desc.size[axis].grow : child->desc.size[axis].shrink;
			if (weight <= 0.f) continue;
			f32 prev = child->arranged_size.xy[axis];
			Assert(prev >= ui_box__local_min(child, axis));
			Assert(prev <= ui_box__local_max(child, axis));
			f32 next = ui_box__clamp(prev + unit * weight, ui_box__local_min(child, axis), ui_box__local_max(child, axis));
			child->arranged_size.xy[axis] = next;
			correction += next - prev;
		}
		free_size -= correction;
		if (fabsf(correction) <= 0.001f) break;
	}
	return free_size;
}

static void ui_box__clip_axis(rect_f32 *clip, rect_f32 viewport, AXIS axis)
{
	f32 minimum = Max(clip->xy[axis], viewport.xy[axis]);
	f32 maximum = Min(clip->xy[axis] + clip->wh[axis], viewport.xy[axis] + viewport.wh[axis]);
	clip->xy[axis] = minimum;
	clip->wh[axis] = Max(0.f, maximum - minimum);
}

static rect_f32 ui_box__child_clip(UI_Box *box, rect_f32 clip)
{
	for (AXIS axis = AXIS_X; axis <= AXIS_Y; axis ++) {
		if (box->desc.overflow[axis] != UI_BOX_OVERFLOW_VISIBLE && box->content_size.xy[axis] > box->viewport.wh[axis] + 0.001f) ui_box__clip_axis(&clip, box->viewport, axis);
	}
	return clip;
}

static rect_f32 ui_box__intersect(rect_f32 a, rect_f32 b)
{
	f32 minimum_x = Max(a.x, b.x);
	f32 minimum_y = Max(a.y, b.y);
	f32 maximum_x = Min(a.x + a.w, b.x + b.w);
	f32 maximum_y = Min(a.y + a.h, b.y + b.h);
	return (rect_f32) {
		.x = minimum_x,
		.y = minimum_y,
		.w = Max(0.f, maximum_x - minimum_x),
		.h = Max(0.f, maximum_y - minimum_y),
	};
}

static void ui_box__commit_state(UI_Box *box)
{
	if (!box->state) return;
	box->state->last_layout_frame = box->ui->frame_index;
	box->state->layout_generation = box->ui->layout_generation;
	box->state->rect = box->rect;
	box->state->hit_rect = ui_box__intersect(box->rect, box->clip_rect);
	box->state->viewport = box->viewport;
	box->state->content_size = box->content_size;
	box->state->scroll_min = box->scroll_min;
	box->state->scroll_max = box->scroll_max;
	box->state->view_offset = box->scroll_offset;
}

static void ui_box__finish_layout(UI_Box *box)
{
	if (box->ops && box->ops->finish_layout) {
		box->ops->finish_layout(box);
	}
	ui_box__commit_state(box);
}

void ui_box_layout_clipped(UI_Box *box, rect_f32 rect, rect_f32 clip)
{
	box->rect = rect;
	box->arranged_size = rect.size;
	box->clip_rect = clip;
	box->viewport = (rect_f32) {
		.x = rect.x + box->desc.horz_padd[0],
		.y = rect.y + box->desc.vert_padd[0],
		.w = Max(0.f, rect.w - box->desc.horz_padd[0] - box->desc.horz_padd[1]),
		.h = Max(0.f, rect.h - box->desc.vert_padd[0] - box->desc.vert_padd[1]),
	};
	if (box->ops && box->ops->prepare_layout) {
		box->ops->prepare_layout(box);
	}

	for (AXIS axis = AXIS_X; axis <= AXIS_Y; axis ++)
	{
		if (box->desc.overflow[axis] != UI_BOX_OVERFLOW_SCROLL) {
			box->scroll_offset.xy[axis] = 0.f;
		}
	}
	if (box->ops && box->ops->layout)
	{
		box->ops->layout(box, clip);
		ui_box__finish_layout(box);
		return;
	}
	if (!box->child_count)
	{
		ui_box__finish_layout(box);
		return;
	}

	AXIS main_axis = box->desc.axis;
	AXIS perp_axis = !main_axis;
	u32 flow_child_count = 0;
	for (UI_Box *child = box->first; child; child = child->next) {
		flow_child_count += !ui_box__is_absolute(child, main_axis);
	}
	f32 occupied = flow_child_count ? box->desc.gap * (flow_child_count - 1) : 0.f;

	for (UI_Box *child = box->first; child; child = child->next)
	{
		child->arranged_size = child->measured_size;
		if (!ui_box__is_absolute(child, main_axis)) occupied += child->arranged_size.xy[main_axis] + child->desc.margin[main_axis][0] + child->desc.margin[main_axis][1];
	}

	f32 free_space = box->viewport.wh[main_axis] - occupied;
	if (fabsf(free_space) > 0.001f) {
		free_space = ui_box__distribute(box, main_axis, free_space);
	}

	box->content_size.xy[perp_axis] = 0.f;
	for (UI_Box *child = box->first; child; child = child->next)
	{
		f32 perp_before = child->desc.margin[perp_axis][0];
		f32 perp_after = child->desc.margin[perp_axis][1];
		f32 perp_available = Max(0.f, box->viewport.wh[perp_axis] - perp_before - perp_after);
		f32 perp_size = child->measured_size.xy[perp_axis];
		if (child->desc.size[perp_axis].kind == UI_BOX_SIZE_FILL) {
			perp_size = perp_available;
		}
		perp_size = ui_box__clamp(perp_size, ui_box__local_min(child, perp_axis), ui_box__local_max(child, perp_axis));
		child->arranged_size.xy[perp_axis] = perp_size;
		if (!ui_box__is_absolute(child, perp_axis)) box->content_size.xy[perp_axis] = Max(box->content_size.xy[perp_axis], perp_size + perp_before + perp_after);
	}

	box->content_size.xy[main_axis] = Max(0.f, box->viewport.wh[main_axis] - free_space);
	box->content_bounds = (rect_f32) { .pos = box->viewport.pos, .size = box->content_size };

	for (AXIS axis = AXIS_X; axis <= AXIS_Y; axis ++)
	{
		box->scroll_min.xy[axis] = 0.f;
		box->scroll_max.xy[axis] = Max(box->content_size.xy[axis] - box->viewport.wh[axis], 0.f);
		if (box->desc.overflow[axis] == UI_BOX_OVERFLOW_SCROLL) {
			box->scroll_offset.xy[axis] = ui_box__clamp(box->scroll_offset.xy[axis], box->scroll_min.xy[axis], box->scroll_max.xy[axis]);
		}
	}
	rect_f32 child_clip = ui_box__child_clip(box, clip);

	f32 cursor = box->viewport.xy[main_axis] - box->scroll_offset.xy[main_axis];
	for (UI_Box *child = box->first; child; child = child->next)
	{
		f32 main_before = child->desc.margin[main_axis][0];
		f32 main_after = child->desc.margin[main_axis][1];
		f32 perp_before = child->desc.margin[perp_axis][0];
		f32 perp_after = child->desc.margin[perp_axis][1];
		f32 perp_available = Max(0.f, box->viewport.wh[perp_axis] - perp_before - perp_after);
		f32 perp_size = child->arranged_size.xy[perp_axis];
		rect_f32 child_rect = {};
		child_rect.xy[main_axis] = ui_box__is_absolute(child, main_axis) ? box->viewport.xy[main_axis] + child->desc.position[main_axis].value - box->scroll_offset.xy[main_axis] : cursor + main_before;
		child_rect.wh[main_axis] = child->arranged_size.xy[main_axis];
		child_rect.xy[perp_axis] = ui_box__is_absolute(child, perp_axis) ? box->viewport.xy[perp_axis] + child->desc.position[perp_axis].value - box->scroll_offset.xy[perp_axis] : box->viewport.xy[perp_axis] - box->scroll_offset.xy[perp_axis] + perp_before + (perp_available - perp_size) * ui_box__clamp(child->desc.perp_align, 0.f, 1.f);
		child_rect.wh[perp_axis] = perp_size;
		ui_box_layout_clipped(child, child_rect, child_clip);
		if (!ui_box__is_absolute(child, main_axis)) cursor += main_before + child_rect.wh[main_axis] + main_after + box->desc.gap;
	}
	ui_box__finish_layout(box);
}

void ui_box_layout(UI_Box *box, rect_f32 rect)
{
	Assert(box);
	ui_box_layout_clipped(box, rect, rect);
}

void ui_box_relayout(UI_Box *box)
{
	Assert(box);
	ui_box_layout_clipped(box, box->rect, box->clip_rect);
}

UI_Box *ui_box_find_deepest(UI_Box *box, vec2 point)
{
	if (!box || !rect_f32_contains(box->clip_rect, point)) {
		return 0;
	}
	for (UI_Box *child = box->last; child; child = child->prev)
	{
		UI_Box *result = ui_box_find_deepest(child, point);
		if (result) {
			return result;
		}
	}
	return rect_f32_contains(box->rect, point) ? box : 0;
}
