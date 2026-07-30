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

UI_BoxSize ui_box_content(void)
{
	return (UI_BoxSize) { .kind = UI_BOX_SIZE_CONTENT };
}

UI_BoxSize ui_box_pixels(f32 value)
{
	return (UI_BoxSize) { .kind = UI_BOX_SIZE_PIXELS, .value = Max(0.f, value) };
}

UI_BoxSize ui_box_fill(f32 grow)
{
	return (UI_BoxSize) { .kind = UI_BOX_SIZE_FILL, .grow = Max(0.f, grow) };
}

UI_BoxSize ui_box_flex(f32 grow, f32 shrink)
{
	return (UI_BoxSize) {
		.kind = UI_BOX_SIZE_CONTENT,
		.grow = Max(0.f, grow),
		.shrink = Max(0.f, shrink),
	};
}

UI_BoxDesc ui_box_desc(void)
{
	return (UI_BoxDesc) {
		.size = { { .kind = UI_BOX_SIZE_CONTENT }, { .kind = UI_BOX_SIZE_CONTENT } },
		.max_size = v2(UI_BOX_INFINITY, UI_BOX_INFINITY),
		.axis = AXIS_Y,
	};
}

static UI_BoxPaintDesc ui_box__paint_desc(void)
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

static void ui_box__finish_children(UI_BoxBuilder *builder, UI_Box *parent)
{
	Assert(parent->child_count <= builder->child_count);
	builder->child_count -= parent->child_count;
	if (parent->child_count)
	{
		u64 size = sizeof(*parent->children) * parent->child_count;
		parent->children = arena_push_copy(builder->arena, size, builder->child_stack + builder->child_count);
	}
}

UI_Box *ui_box_builder_begin(UI_BoxBuilder *builder, Arena *arena, UI_Context *ui, UI_Key root_key, String root_name, UI_BoxDesc root_desc)
{
	Assert(builder);
	Assert(arena);
	memory_zero(builder, sizeof(*builder));
	builder->arena = arena;
	builder->ui = ui;
	builder->desc = ui_box_desc();
	builder->paint = ui_box__paint_desc();
	builder->id = ui_id_child(UI_ID_NONE, root_key);
	builder->root = ui_box__allocate_box(builder, builder->id, root_key, root_name, root_desc);
	builder->parent = builder->root;
	return builder->root;
}

UI_Box *ui_box_make_desc(UI_BoxBuilder *builder, UI_Key key, String name, UI_BoxDesc desc)
{
	Assert(builder);
	Assert(builder->parent);
	Assert(builder->child_count < ArrayCount(builder->child_stack));
	UI_Id id = ui_id_child(builder->id, key);
	u32 sibling_begin = builder->child_count - builder->parent->child_count;
	for (u32 sibling_index = sibling_begin; sibling_index < builder->child_count; sibling_index ++) {
		Assert(!ui_id_equal(builder->child_stack[sibling_index]->id, id));
	}
	UI_Box *box = ui_box__allocate_box(builder, id, key, name, desc);
	builder->parent->child_count++;
	builder->child_stack[builder->child_count++] = box;
	return box;
}

UI_Box *ui_box_make(UI_BoxBuilder *builder, UI_Key key, String name)
{
	return ui_box_make_desc(builder, key, name, builder->desc);
}

UI_Box *ui_box_begin_desc(UI_BoxBuilder *builder, UI_Key key, String name, UI_BoxDesc desc)
{
	UI_Box *box = ui_box_make_desc(builder, key, name, desc);
	Assert(builder->parent_count < ArrayCount(builder->parent_stack));
	builder->parent_stack[builder->parent_count] = builder->parent;
	builder->parent_id_stack[builder->parent_count++] = builder->id;
	builder->parent = box;
	builder->id = box->id;
	return box;
}

UI_Box *ui_box_begin(UI_BoxBuilder *builder, UI_Key key, String name)
{
	return ui_box_begin_desc(builder, key, name, builder->desc);
}

void ui_box_end(UI_BoxBuilder *builder)
{
	Assert(builder);
	Assert(builder->parent_count);
	ui_box__finish_children(builder, builder->parent);
	builder->parent_count--;
	builder->parent = builder->parent_stack[builder->parent_count];
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
	ui_box__finish_children(builder, builder->root);
	Assert(builder->child_count == 0);
	return builder->root;
}

void ui_box_push_id(UI_BoxBuilder *builder, UI_Key key)
{
	Assert(builder);
	Assert(builder->id_count < ArrayCount(builder->id_stack));
	builder->id_stack[builder->id_count++] = builder->id;
	builder->id = ui_id_child(builder->id, key);
}

void ui_box_pop_id(UI_BoxBuilder *builder)
{
	Assert(builder);
	Assert(builder->id_count);
	builder->id = builder->id_stack[--builder->id_count];
}

UI_Box *ui_box_make_virtual_list_desc(UI_BoxBuilder *builder, UI_Key key, String name, UI_BoxDesc desc, UI_BoxVirtualListDesc list)
{
	Assert(builder);
	Assert(list.build_item);
	desc.overflow[desc.axis] = UI_BOX_OVERFLOW_SCROLL;
	UI_Box *box = ui_box_begin_desc(builder, key, name, desc);
	box->virtual_list.arena = builder->arena;
	box->virtual_list.build_item = list.build_item;
	box->virtual_list.user = list.user;
	box->virtual_list.item_count = list.item_count;
	if (list.item_count)
	{
		ui_box_push_id(builder, 0);
		list.build_item(builder, 0, list.user);
		ui_box_pop_id(builder);
		Assert(box->child_count == 1);
	}
	ui_box_end(builder);
	if (list.item_count) {
		box->virtual_list.sizing_item = box->children[0];
	}
	box->children = 0;
	box->child_count = 0;
	return box;
}

UI_Box *ui_box_make_virtual_list(UI_BoxBuilder *builder, UI_Key key, String name, UI_BoxVirtualListDesc list)
{
	return ui_box_make_virtual_list_desc(builder, key, name, builder->desc, list);
}

void ui_push(UI_BoxBuilder *builder)
{
	Assert(builder);
	Assert(builder->desc_count < ArrayCount(builder->desc_stack));
	builder->desc_stack[builder->desc_count] = builder->desc;
	builder->paint_stack[builder->desc_count++] = builder->paint;
}

void ui_pop(UI_BoxBuilder *builder)
{
	Assert(builder);
	Assert(builder->desc_count);
	builder->desc_count--;
	builder->desc = builder->desc_stack[builder->desc_count];
	builder->paint = builder->paint_stack[builder->desc_count];
}

void ui_size(UI_BoxBuilder *builder, AXIS axis, UI_BoxSize size)
{
	Assert(builder);
	builder->desc.size[axis] = size;
}

void ui_min_size(UI_BoxBuilder *builder, AXIS axis, f32 size)
{
	Assert(builder);
	builder->desc.min_size.xy[axis] = size;
}

void ui_max_size(UI_BoxBuilder *builder, AXIS axis, f32 size)
{
	Assert(builder);
	builder->desc.max_size.xy[axis] = size;
}

void ui_margin(UI_BoxBuilder *builder, AXIS axis, f32 before, f32 after)
{
	Assert(builder);
	builder->desc.margin[axis][0] = before;
	builder->desc.margin[axis][1] = after;
}

void ui_padd(UI_BoxBuilder *builder, AXIS axis, f32 before, f32 after)
{
	Assert(builder);
	builder->desc.padd[axis][0] = before;
	builder->desc.padd[axis][1] = after;
}

void ui_axis(UI_BoxBuilder *builder, AXIS axis)
{
	Assert(builder);
	builder->desc.axis = axis;
}

void ui_gap(UI_BoxBuilder *builder, f32 gap)
{
	Assert(builder);
	builder->desc.gap = gap;
}

void ui_perp_align(UI_BoxBuilder *builder, f32 align)
{
	Assert(builder);
	builder->desc.perp_align = align;
}

void ui_overflow(UI_BoxBuilder *builder, AXIS axis, UI_BoxOverflow overflow)
{
	Assert(builder);
	builder->desc.overflow[axis] = overflow;
}

void ui_background(UI_BoxBuilder *builder, Color_SRGBA color)
{
	Assert(builder);
	builder->paint.flags |= UI_BOX_DRAW_BACKGROUND;
	builder->paint.background = color;
}

void ui_border(UI_BoxBuilder *builder, Color_SRGBA color, f32 width)
{
	Assert(builder);
	builder->paint.flags |= UI_BOX_DRAW_BORDER;
	builder->paint.border = color;
	builder->paint.border_width = Max(0.f, width);
}

void ui_roundness(UI_BoxBuilder *builder, f32 roundness)
{
	Assert(builder);
	builder->paint.roundness = Max(0.f, roundness);
}

void ui_edge_softness(UI_BoxBuilder *builder, f32 edge_softness)
{
	Assert(builder);
	builder->paint.edge_softness = Max(0.f, edge_softness);
}

void ui_emission(UI_BoxBuilder *builder, f32 emission)
{
	Assert(builder);
	builder->paint.emission = Max(0.f, emission);
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
	if (box->virtual_list.build_item)
	{
		AXIS main_axis = box->desc.axis;
		AXIS perp_axis = !main_axis;
		content = v2(0.f, 0.f);
		box->virtual_list.item_extent = 0.f;
		if (box->virtual_list.item_count)
		{
			vec2 available = v2(Max(0.f, constraints.max.x - box->desc.horz_padd[0] - box->desc.horz_padd[1]), Max(0.f, constraints.max.y - box->desc.vert_padd[0] - box->desc.vert_padd[1]));
			UI_BoxConstraints item_constraints = { .max = available };
			item_constraints.max.xy[main_axis] = UI_BOX_INFINITY;
			UI_Box *item = box->virtual_list.sizing_item;
			vec2 item_size = ui_box_measure(item, item_constraints);
			item_size.xy[main_axis] += item->desc.margin[main_axis][0] + item->desc.margin[main_axis][1];
			item_size.xy[perp_axis] += item->desc.margin[perp_axis][0] + item->desc.margin[perp_axis][1];
			box->virtual_list.item_extent = item_size.xy[main_axis];
			content.xy[main_axis] = box->virtual_list.item_extent * box->virtual_list.item_count + box->desc.gap * (box->virtual_list.item_count - 1);
			content.xy[perp_axis] = item_size.xy[perp_axis];
		}
		natural.x = Max(natural.x, content.x);
		natural.y = Max(natural.y, content.y);
	}
	else if (box->ops && box->ops->measure_children)
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
		content.xy[main_axis] = box->desc.gap * (box->child_count - 1);

		for (u32 child_index = 0; child_index < box->child_count; child_index ++)
		{
			UI_Box *child = box->children[child_index];

			UI_BoxConstraints child_constraints = { .max = available, };
			child_constraints.max.xy[main_axis] = UI_BOX_INFINITY;

			vec2 child_size = ui_box_measure(child, child_constraints);
			child_size.xy[main_axis] += child->desc.margin[main_axis][0] + child->desc.margin[main_axis][1];
			child_size.xy[perp_axis] += child->desc.margin[perp_axis][0] + child->desc.margin[perp_axis][1];

			content.xy[main_axis] += child_size.xy[main_axis];
			content.xy[perp_axis] = Max(content.xy[perp_axis], child_size.xy[perp_axis]);
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
		for (u32 child_index = 0; child_index < box->child_count; child_index ++)
		{
			UI_Box *child = box->children[child_index];
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
		for (u32 child_index = 0; child_index < box->child_count; child_index ++)
		{
			UI_Box *child = box->children[child_index];
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

static void ui_box__layout(UI_Box *box, rect_f32 rect, rect_f32 clip);

static void ui_box__materialize_virtual_list(UI_Box *box, u32 first_item, u32 one_past_item)
{
	UI_BoxBuilder builder = {
		.arena = box->virtual_list.arena,
		.ui = box->ui,
		.root = box,
		.parent = box,
		.id = box->id,
		.desc = ui_box_desc(),
	};
	box->children = 0;
	box->child_count = 0;
	for (u32 item_index = first_item; item_index < one_past_item; item_index ++)
	{
		u32 child_count = box->child_count;
		ui_box_push_id(&builder, item_index);
		box->virtual_list.build_item(&builder, item_index, box->virtual_list.user);
		ui_box_pop_id(&builder);
		Assert(builder.parent == box);
		Assert(builder.parent_count == 0);
		Assert(box->child_count == child_count + 1);
	}
	ui_box_builder_end(&builder);
	for (u32 child_index = 0; child_index < box->child_count; child_index ++) {
		box->children[child_index]->virtual_index = first_item + child_index;
	}
	box->virtual_list.first_item = first_item;
	box->virtual_list.one_past_item = one_past_item;
}

static void ui_box__layout_virtual_list(UI_Box *box, rect_f32 clip)
{
	AXIS main_axis = box->desc.axis;
	AXIS perp_axis = !main_axis;
	u32 item_count = box->virtual_list.item_count;
	f32 item_extent = box->virtual_list.item_extent;
	f32 stride = item_extent + box->desc.gap;
	box->content_size = v2(0.f, 0.f);
	if (item_count)
	{
		UI_Box *item = box->virtual_list.sizing_item;
		f32 perp_before = item->desc.margin[perp_axis][0];
		f32 perp_after = item->desc.margin[perp_axis][1];
		f32 perp_size = item->measured_size.xy[perp_axis];
		if (item->desc.size[perp_axis].kind == UI_BOX_SIZE_FILL) {
			perp_size = Max(0.f, box->viewport.wh[perp_axis] - perp_before - perp_after);
		}
		box->content_size.xy[main_axis] = item_extent * item_count + box->desc.gap * (item_count - 1);
		box->content_size.xy[perp_axis] = perp_size + perp_before + perp_after;
	}
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

	u32 first_item = 0;
	u32 one_past_item = 0;
	if (item_count && stride > 0.001f && box->viewport.wh[main_axis] > 0.f)
	{
		first_item = Min((u32)(box->scroll_offset.xy[main_axis] / stride), item_count);
		one_past_item = Min((u32)ceilf((box->scroll_offset.xy[main_axis] + box->viewport.wh[main_axis]) / stride), item_count);
		first_item = first_item > 2 ? first_item - 2 : 0;
		one_past_item = Min(one_past_item + 2, item_count);
	}
	ui_box__materialize_virtual_list(box, first_item, one_past_item);

	vec2 available = box->viewport.size;
	for (u32 child_index = 0; child_index < box->child_count; child_index ++)
	{
		UI_Box *child = box->children[child_index];
		UI_BoxConstraints constraints = { .max = available };
		constraints.max.xy[main_axis] = item_extent;
		ui_box_measure(child, constraints);

		f32 main_before = child->desc.margin[main_axis][0];
		f32 main_after = child->desc.margin[main_axis][1];
		f32 perp_before = child->desc.margin[perp_axis][0];
		f32 perp_after = child->desc.margin[perp_axis][1];
		f32 perp_available = Max(0.f, box->viewport.wh[perp_axis] - perp_before - perp_after);
		f32 perp_size = child->measured_size.xy[perp_axis];
		if (child->desc.size[perp_axis].kind == UI_BOX_SIZE_FILL) {
			perp_size = perp_available;
		}
		perp_size = ui_box__clamp(perp_size, ui_box__local_min(child, perp_axis), ui_box__local_max(child, perp_axis));

		rect_f32 child_rect = {};
		child_rect.xy[main_axis] = box->viewport.xy[main_axis] - box->scroll_offset.xy[main_axis] + child->virtual_index * stride + main_before;
		child_rect.wh[main_axis] = Max(0.f, item_extent - main_before - main_after);
		child_rect.xy[perp_axis] = box->viewport.xy[perp_axis] - box->scroll_offset.xy[perp_axis] + perp_before + (perp_available - perp_size) * ui_box__clamp(child->desc.perp_align, 0.f, 1.f);
		child_rect.wh[perp_axis] = perp_size;
		ui_box__layout(child, child_rect, child_clip);
	}
}

static void ui_box__layout(UI_Box *box, rect_f32 rect, rect_f32 clip)
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
	if (box->virtual_list.build_item)
	{
		ui_box__layout_virtual_list(box, clip);
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
	f32 occupied = box->desc.gap * (box->child_count - 1);

	for (u32 child_index = 0; child_index < box->child_count; child_index ++)
	{
		UI_Box *child = box->children[child_index];
		child->arranged_size = child->measured_size;
		occupied += child->arranged_size.xy[main_axis] + child->desc.margin[main_axis][0] + child->desc.margin[main_axis][1];
	}

	f32 free_space = box->viewport.wh[main_axis] - occupied;
	if (fabsf(free_space) > 0.001f) {
		free_space = ui_box__distribute(box, main_axis, free_space);
	}

	box->content_size.xy[perp_axis] = 0.f;
	for (u32 child_index = 0; child_index < box->child_count; child_index ++)
	{
		UI_Box *child = box->children[child_index];
		f32 perp_before = child->desc.margin[perp_axis][0];
		f32 perp_after = child->desc.margin[perp_axis][1];
		f32 perp_available = Max(0.f, box->viewport.wh[perp_axis] - perp_before - perp_after);
		f32 perp_size = child->measured_size.xy[perp_axis];
		if (child->desc.size[perp_axis].kind == UI_BOX_SIZE_FILL) {
			perp_size = perp_available;
		}
		perp_size = ui_box__clamp(perp_size, ui_box__local_min(child, perp_axis), ui_box__local_max(child, perp_axis));
		child->arranged_size.xy[perp_axis] = perp_size;
		box->content_size.xy[perp_axis] = Max(box->content_size.xy[perp_axis], perp_size + perp_before + perp_after);
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
	for (u32 child_index = 0; child_index < box->child_count; child_index ++)
	{
		UI_Box *child = box->children[child_index];
		f32 main_before = child->desc.margin[main_axis][0];
		f32 main_after = child->desc.margin[main_axis][1];
		f32 perp_before = child->desc.margin[perp_axis][0];
		f32 perp_after = child->desc.margin[perp_axis][1];
		f32 perp_available = Max(0.f, box->viewport.wh[perp_axis] - perp_before - perp_after);
		f32 perp_size = child->arranged_size.xy[perp_axis];
		rect_f32 child_rect = {};
		child_rect.xy[main_axis] = cursor + main_before;
		child_rect.wh[main_axis] = child->arranged_size.xy[main_axis];
		child_rect.xy[perp_axis] = box->viewport.xy[perp_axis] - box->scroll_offset.xy[perp_axis] + perp_before + (perp_available - perp_size) * ui_box__clamp(child->desc.perp_align, 0.f, 1.f);
		child_rect.wh[perp_axis] = perp_size;
		ui_box__layout(child, child_rect, child_clip);
		cursor += main_before + child_rect.wh[main_axis] + main_after + box->desc.gap;
	}
	ui_box__finish_layout(box);
}

void ui_box_layout(UI_Box *box, rect_f32 rect)
{
	Assert(box);
	ui_box__layout(box, rect, rect);
}

void ui_box_relayout(UI_Box *box)
{
	Assert(box);
	ui_box__layout(box, box->rect, box->clip_rect);
}

UI_Box *ui_box_find_deepest(UI_Box *box, vec2 point)
{
	if (!box || !rect_f32_contains(box->clip_rect, point)) {
		return 0;
	}
	for (u32 child_index = box->child_count; child_index > 0; --child_index)
	{
		UI_Box *result = ui_box_find_deepest(box->children[child_index - 1], point);
		if (result) {
			return result;
		}
	}
	return rect_f32_contains(box->rect, point) ? box : 0;
}
