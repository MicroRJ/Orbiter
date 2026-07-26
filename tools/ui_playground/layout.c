#include "layout.h"

static f32 uip__clamp(f32 value, f32 minimum, f32 maximum)
{
	return Max(minimum, Min(value, maximum));
}

static f32 uip__local_min(const UIP_Box *box, AXIS axis)
{
	return Max(0.f, box->desc.min_size.xy[axis]);
}

static f32 uip__local_max(const UIP_Box *box, AXIS axis)
{
	return Max(uip__local_min(box, axis), box->desc.max_size.xy[axis]);
}

UIP_Size uip_content(void)
{
	return (UIP_Size) { .kind = UIP_SIZE_CONTENT };
}

UIP_Size uip_pixels(f32 value)
{
	return (UIP_Size) { .kind = UIP_SIZE_PIXELS, .value = Max(0.f, value) };
}

UIP_Size uip_fill(f32 grow)
{
	return (UIP_Size) { .kind = UIP_SIZE_FILL, .grow = Max(0.f, grow) };
}

UIP_Size uip_flex(f32 grow, f32 shrink)
{
	return (UIP_Size) {
		.kind = UIP_SIZE_CONTENT,
		.grow = Max(0.f, grow),
		.shrink = Max(0.f, shrink),
	};
}

UIP_BoxDesc uip_box_desc(void)
{
	return (UIP_BoxDesc) {
		.size = { { .kind = UIP_SIZE_CONTENT }, { .kind = UIP_SIZE_CONTENT } },
		.max_size = v2(UIP_INFINITY, UIP_INFINITY),
		.axis = AXIS_Y,
	};
}

static UIP_Box *uip__allocate_box(UIP_Builder *builder, UI_Id id, String name, UIP_BoxDesc desc)
{
	UIP_Box *box = arena_push_zero(builder->arena, sizeof(*box));
	box->id = id;
	box->name = name;
	box->desc = desc;
	box->ui = builder->ui;
	return box;
}

static void uip__finish_children(UIP_Builder *builder, UIP_Box *parent)
{
	Assert(parent->child_count <= builder->child_count);
	builder->child_count -= parent->child_count;
	if (parent->child_count)
	{
		u64 size = sizeof(*parent->children) * parent->child_count;
		parent->children = arena_push_copy(builder->arena, size, builder->child_stack + builder->child_count);
	}
}

UIP_Box *uip_builder_begin(UIP_Builder *builder, Arena *arena, UIP_Context *ui, u64 root_key, String root_name, UIP_BoxDesc root_desc)
{
	Assert(builder);
	Assert(arena);
	memory_zero(builder, sizeof(*builder));
	builder->arena = arena;
	builder->ui = ui;
	builder->id = ui_id_child(UI_ID_NONE, root_key);
	builder->root = uip__allocate_box(builder, builder->id, root_name, root_desc);
	builder->parent = builder->root;
	return builder->root;
}

UIP_Box *uip_make_box(UIP_Builder *builder, u64 key, String name, UIP_BoxDesc desc)
{
	Assert(builder);
	Assert(builder->parent);
	Assert(builder->child_count < ArrayCount(builder->child_stack));
	UI_Id id = ui_id_child(builder->id, key);
	u32 sibling_begin = builder->child_count - builder->parent->child_count;
	for (u32 sibling_index = sibling_begin; sibling_index < builder->child_count; sibling_index ++) {
		Assert(!ui_id_equal(builder->child_stack[sibling_index]->id, id));
	}
	UIP_Box *box = uip__allocate_box(builder, id, name, desc);
	builder->parent->child_count++;
	builder->child_stack[builder->child_count++] = box;
	return box;
}

UIP_Box *uip_begin_box(UIP_Builder *builder, u64 key, String name, UIP_BoxDesc desc)
{
	UIP_Box *box = uip_make_box(builder, key, name, desc);
	Assert(builder->parent_count < ArrayCount(builder->parent_stack));
	builder->parent_stack[builder->parent_count] = builder->parent;
	builder->parent_id_stack[builder->parent_count++] = builder->id;
	builder->parent = box;
	builder->id = box->id;
	return box;
}

void uip_end_box(UIP_Builder *builder)
{
	Assert(builder);
	Assert(builder->parent_count);
	uip__finish_children(builder, builder->parent);
	builder->parent_count--;
	builder->parent = builder->parent_stack[builder->parent_count];
	builder->id = builder->parent_id_stack[builder->parent_count];
}

UIP_Box *uip_builder_end(UIP_Builder *builder)
{
	Assert(builder);
	Assert(builder->root);
	Assert(builder->parent == builder->root);
	Assert(builder->parent_count == 0);
	Assert(builder->id_count == 0);
	Assert(ui_id_equal(builder->id, builder->root->id));
	uip__finish_children(builder, builder->root);
	Assert(builder->child_count == 0);
	return builder->root;
}

void uip_push_id(UIP_Builder *builder, u64 key)
{
	Assert(builder);
	Assert(builder->id_count < ArrayCount(builder->id_stack));
	builder->id_stack[builder->id_count++] = builder->id;
	builder->id = ui_id_child(builder->id, key);
}

void uip_pop_id(UIP_Builder *builder)
{
	Assert(builder);
	Assert(builder->id_count);
	builder->id = builder->id_stack[--builder->id_count];
}

UIP_Box *uip_make_virtual_list(UIP_Builder *builder, u64 key, String name, UIP_BoxDesc desc, UIP_VirtualListDesc list)
{
	Assert(builder);
	Assert(list.build_item);
	desc.overflow[desc.axis] = UIP_OVERFLOW_SCROLL;
	UIP_Box *box = uip_begin_box(builder, key, name, desc);
	box->virtual_list.arena = builder->arena;
	box->virtual_list.build_item = list.build_item;
	box->virtual_list.user = list.user;
	box->virtual_list.item_count = list.item_count;
	if (list.item_count)
	{
		uip_push_id(builder, 0);
		list.build_item(builder, 0, list.user);
		uip_pop_id(builder);
		Assert(box->child_count == 1);
	}
	uip_end_box(builder);
	if (list.item_count) {
		box->virtual_list.sizing_item = box->children[0];
	}
	box->children = 0;
	box->child_count = 0;
	return box;
}

vec2 uip_measure(UIP_Box *box, UIP_Constraints constraints)
{
	Assert(box);
	for (AXIS axis = AXIS_X; axis <= AXIS_Y; ++axis)
	{
		constraints.min.xy[axis] = Max(0.f, constraints.min.xy[axis]);
		constraints.max.xy[axis] = Max(constraints.min.xy[axis], constraints.max.xy[axis]);
	}

	vec2 natural = box->intrinsic_size;
	vec2 content = box->intrinsic_size;
	if (box->ops && box->ops->measure)
	{
		vec2 padd = v2(box->desc.horz_padd[0] + box->desc.horz_padd[1], box->desc.vert_padd[0] + box->desc.vert_padd[1]);
		UIP_Constraints content_constraints = { .max = v2(Max(0.f, constraints.max.x - padd.x), Max(0.f, constraints.max.y - padd.y)) };
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
			UIP_Constraints item_constraints = { .max = available };
			item_constraints.max.xy[main_axis] = UIP_INFINITY;
			UIP_Box *item = box->virtual_list.sizing_item;
			vec2 item_size = uip_measure(item, item_constraints);
			item_size.xy[main_axis] += item->desc.margin[main_axis][0] + item->desc.margin[main_axis][1];
			item_size.xy[perp_axis] += item->desc.margin[perp_axis][0] + item->desc.margin[perp_axis][1];
			box->virtual_list.item_extent = item_size.xy[main_axis];
			content.xy[main_axis] = box->virtual_list.item_extent * box->virtual_list.item_count + box->desc.gap * (box->virtual_list.item_count - 1);
			content.xy[perp_axis] = item_size.xy[perp_axis];
		}
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
			UIP_Box *child = box->children[child_index];

			UIP_Constraints child_constraints = { .max = available, };
			child_constraints.max.xy[main_axis] = UIP_INFINITY;

			vec2 child_size = uip_measure(child, child_constraints);
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
		UIP_Size spec = box->desc.size[axis];
		f32 desired = natural.xy[axis];
		if (spec.kind == UIP_SIZE_PIXELS) {
			desired = spec.value;
		}
		else if (spec.kind == UIP_SIZE_FILL) {
			desired = uip__local_min(box, axis);
		}
		desired = uip__clamp(desired, uip__local_min(box, axis), uip__local_max(box, axis));
		measured.xy[axis] = uip__clamp(desired, constraints.min.xy[axis], constraints.max.xy[axis]);
	}
	box->measured_size = measured;
	box->arranged_size = measured;
	return measured;
}

static f32 uip__distribute(UIP_Box *box, AXIS axis, f32 free_size)
{
	b32 growing = free_size > 0.f;
	for (u32 pass = 0; pass < box->child_count && fabsf(free_size) > 0.001f; pass ++)
	{
		f32 total_weight = 0.f;
		for (u32 child_index = 0; child_index < box->child_count; child_index ++)
		{
			UIP_Box *child = box->children[child_index];
			f32 size = child->arranged_size.xy[axis];
			f32 weight = growing ? child->desc.size[axis].grow : child->desc.size[axis].shrink;
			f32 bound = growing ? uip__local_max(child, axis) : uip__local_min(child, axis);
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
			UIP_Box *child = box->children[child_index];
			f32 weight = growing ? child->desc.size[axis].grow : child->desc.size[axis].shrink;
			if (weight <= 0.f) continue;
			f32 prev = child->arranged_size.xy[axis];
			Assert(prev >= uip__local_min(child, axis));
			Assert(prev <= uip__local_max(child, axis));
			f32 next = uip__clamp(prev + unit * weight, uip__local_min(child, axis), uip__local_max(child, axis));
			child->arranged_size.xy[axis] = next;
			correction += next - prev;
		}
		free_size -= correction;
		if (fabsf(correction) <= 0.001f) break;
	}
	return free_size;
}

static void uip__clip_axis(rect_f32 *clip, rect_f32 viewport, AXIS axis)
{
	f32 minimum = Max(clip->xy[axis], viewport.xy[axis]);
	f32 maximum = Min(clip->xy[axis] + clip->wh[axis], viewport.xy[axis] + viewport.wh[axis]);
	clip->xy[axis] = minimum;
	clip->wh[axis] = Max(0.f, maximum - minimum);
}

static void uip__layout(UIP_Box *box, rect_f32 rect, rect_f32 clip);

static void uip__materialize_virtual_list(UIP_Box *box, u32 first_item, u32 one_past_item)
{
	UIP_Builder builder = {
		.arena = box->virtual_list.arena,
		.ui = box->ui,
		.root = box,
		.parent = box,
		.id = box->id,
	};
	box->children = 0;
	box->child_count = 0;
	for (u32 item_index = first_item; item_index < one_past_item; item_index ++)
	{
		u32 child_count = box->child_count;
		uip_push_id(&builder, item_index);
		box->virtual_list.build_item(&builder, item_index, box->virtual_list.user);
		uip_pop_id(&builder);
		Assert(builder.parent == box);
		Assert(builder.parent_count == 0);
		Assert(box->child_count == child_count + 1);
	}
	uip_builder_end(&builder);
	for (u32 child_index = 0; child_index < box->child_count; child_index ++) {
		box->children[child_index]->virtual_index = first_item + child_index;
	}
	box->virtual_list.first_item = first_item;
	box->virtual_list.one_past_item = one_past_item;
}

static void uip__layout_virtual_list(UIP_Box *box, rect_f32 child_clip)
{
	AXIS main_axis = box->desc.axis;
	AXIS perp_axis = !main_axis;
	u32 item_count = box->virtual_list.item_count;
	f32 item_extent = box->virtual_list.item_extent;
	f32 stride = item_extent + box->desc.gap;
	box->content_size = v2(0.f, 0.f);
	if (item_count)
	{
		UIP_Box *item = box->virtual_list.sizing_item;
		f32 perp_before = item->desc.margin[perp_axis][0];
		f32 perp_after = item->desc.margin[perp_axis][1];
		f32 perp_size = item->measured_size.xy[perp_axis];
		if (item->desc.size[perp_axis].kind == UIP_SIZE_FILL) {
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
		if (box->desc.overflow[axis] == UIP_OVERFLOW_SCROLL) {
			box->scroll_offset.xy[axis] = uip__clamp(box->scroll_offset.xy[axis], box->scroll_min.xy[axis], box->scroll_max.xy[axis]);
		}
	}

	u32 first_item = 0;
	u32 one_past_item = 0;
	if (item_count && stride > 0.001f && box->viewport.wh[main_axis] > 0.f)
	{
		first_item = Min((u32)(box->scroll_offset.xy[main_axis] / stride), item_count);
		one_past_item = Min((u32)ceilf((box->scroll_offset.xy[main_axis] + box->viewport.wh[main_axis]) / stride), item_count);
		first_item = first_item > 2 ? first_item - 2 : 0;
		one_past_item = Min(one_past_item + 2, item_count);
	}
	uip__materialize_virtual_list(box, first_item, one_past_item);

	vec2 available = box->viewport.size;
	for (u32 child_index = 0; child_index < box->child_count; child_index ++)
	{
		UIP_Box *child = box->children[child_index];
		UIP_Constraints constraints = { .max = available };
		constraints.max.xy[main_axis] = item_extent;
		uip_measure(child, constraints);

		f32 main_before = child->desc.margin[main_axis][0];
		f32 main_after = child->desc.margin[main_axis][1];
		f32 perp_before = child->desc.margin[perp_axis][0];
		f32 perp_after = child->desc.margin[perp_axis][1];
		f32 perp_available = Max(0.f, box->viewport.wh[perp_axis] - perp_before - perp_after);
		f32 perp_size = child->measured_size.xy[perp_axis];
		if (child->desc.size[perp_axis].kind == UIP_SIZE_FILL) {
			perp_size = perp_available;
		}
		perp_size = uip__clamp(perp_size, uip__local_min(child, perp_axis), uip__local_max(child, perp_axis));

		rect_f32 child_rect = {};
		child_rect.xy[main_axis] = box->viewport.xy[main_axis] - box->scroll_offset.xy[main_axis] + child->virtual_index * stride + main_before;
		child_rect.wh[main_axis] = Max(0.f, item_extent - main_before - main_after);
		child_rect.xy[perp_axis] = box->viewport.xy[perp_axis] - box->scroll_offset.xy[perp_axis] + perp_before + (perp_available - perp_size) * uip__clamp(child->desc.perp_align, 0.f, 1.f);
		child_rect.wh[perp_axis] = perp_size;
		uip__layout(child, child_rect, child_clip);
	}
}

static void uip__layout(UIP_Box *box, rect_f32 rect, rect_f32 clip)
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

	rect_f32 child_clip = clip;
	for (AXIS axis = AXIS_X; axis <= AXIS_Y; axis ++)
	{
		if (box->desc.overflow[axis] != UIP_OVERFLOW_VISIBLE) {
			uip__clip_axis(&child_clip, box->viewport, axis);
		}
		if (box->desc.overflow[axis] != UIP_OVERFLOW_SCROLL) {
			box->scroll_offset.xy[axis] = 0.f;
		}
	}
	if (box->virtual_list.build_item)
	{
		uip__layout_virtual_list(box, child_clip);
		return;
	}
	if (!box->child_count) {
		return;
	}

	AXIS main_axis = box->desc.axis;
	AXIS perp_axis = !main_axis;
	f32 occupied = box->desc.gap * (box->child_count - 1);

	for (u32 child_index = 0; child_index < box->child_count; child_index ++)
	{
		UIP_Box *child = box->children[child_index];
		child->arranged_size = child->measured_size;
		occupied += child->arranged_size.xy[main_axis] + child->desc.margin[main_axis][0] + child->desc.margin[main_axis][1];
	}

	f32 free_space = box->viewport.wh[main_axis] - occupied;
	if (fabsf(free_space) > 0.001f) {
		free_space = uip__distribute(box, main_axis, free_space);
	}

	box->content_size.xy[perp_axis] = 0.f;
	for (u32 child_index = 0; child_index < box->child_count; child_index ++)
	{
		UIP_Box *child = box->children[child_index];
		f32 perp_before = child->desc.margin[perp_axis][0];
		f32 perp_after = child->desc.margin[perp_axis][1];
		f32 perp_available = Max(0.f, box->viewport.wh[perp_axis] - perp_before - perp_after);
		f32 perp_size = child->measured_size.xy[perp_axis];
		if (child->desc.size[perp_axis].kind == UIP_SIZE_FILL) {
			perp_size = perp_available;
		}
		perp_size = uip__clamp(perp_size, uip__local_min(child, perp_axis), uip__local_max(child, perp_axis));
		child->arranged_size.xy[perp_axis] = perp_size;
		box->content_size.xy[perp_axis] = Max(box->content_size.xy[perp_axis], perp_size + perp_before + perp_after);
	}

	box->content_size.xy[main_axis] = Max(0.f, box->viewport.wh[main_axis] - free_space);
	box->content_bounds = (rect_f32) { .pos = box->viewport.pos, .size = box->content_size };

	for (AXIS axis = AXIS_X; axis <= AXIS_Y; axis ++)
	{
		box->scroll_min.xy[axis] = 0.f;
		box->scroll_max.xy[axis] = Max(box->content_size.xy[axis] - box->viewport.wh[axis], 0.f);
		if (box->desc.overflow[axis] == UIP_OVERFLOW_SCROLL) {
			box->scroll_offset.xy[axis] = uip__clamp(box->scroll_offset.xy[axis], box->scroll_min.xy[axis], box->scroll_max.xy[axis]);
		}
	}

	f32 cursor = box->viewport.xy[main_axis] - box->scroll_offset.xy[main_axis];
	for (u32 child_index = 0; child_index < box->child_count; child_index ++)
	{
		UIP_Box *child = box->children[child_index];
		f32 main_before = child->desc.margin[main_axis][0];
		f32 main_after = child->desc.margin[main_axis][1];
		f32 perp_before = child->desc.margin[perp_axis][0];
		f32 perp_after = child->desc.margin[perp_axis][1];
		f32 perp_available = Max(0.f, box->viewport.wh[perp_axis] - perp_before - perp_after);
		f32 perp_size = child->arranged_size.xy[perp_axis];
		rect_f32 child_rect = {};
		child_rect.xy[main_axis] = cursor + main_before;
		child_rect.wh[main_axis] = child->arranged_size.xy[main_axis];
		child_rect.xy[perp_axis] = box->viewport.xy[perp_axis] - box->scroll_offset.xy[perp_axis] + perp_before + (perp_available - perp_size) * uip__clamp(child->desc.perp_align, 0.f, 1.f);
		child_rect.wh[perp_axis] = perp_size;
		uip__layout(child, child_rect, child_clip);
		cursor += main_before + child_rect.wh[main_axis] + main_after + box->desc.gap;
	}
}

void uip_layout(UIP_Box *box, rect_f32 rect)
{
	Assert(box);
	uip__layout(box, rect, rect);
}

void uip_relayout(UIP_Box *box)
{
	Assert(box);
	uip__layout(box, box->rect, box->clip_rect);
}

UIP_Box *uip_find_deepest(UIP_Box *box, vec2 point)
{
	if (!box || !rect_f32_contains(box->clip_rect, point)) {
		return 0;
	}
	for (u32 child_index = box->child_count; child_index > 0; --child_index)
	{
		UIP_Box *result = uip_find_deepest(box->children[child_index - 1], point);
		if (result) {
			return result;
		}
	}
	return rect_f32_contains(box->rect, point) ? box : 0;
}
