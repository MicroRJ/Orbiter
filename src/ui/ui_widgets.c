#include "ui_widgets.h"

typedef struct
{
	Str string;
	Str sizing_string;
	UI_TextStyle style;
}
UI_TextBoxData;

typedef struct
{
	GFX_Texture *texture;
	rect_i32 region;
	UI_ImageStyle style;
}
UI_ImageBoxData;

typedef struct
{
	UI_BoxTableColumn *columns;
	f32 *natural_widths;
	f32 *resolved_widths;
	u32 column_count;
	f32 row_height;
	f32 column_gap;
}
UI_BoxTableData;

typedef struct
{
	UI_Box *previous_parent;
	UI_Id previous_id;
	UI_Id overlay_id;
	u32 parent_count;
	u32 id_count;
	vec2 anchor;
	vec2 offset;
	f32 margin;
}
UI_TooltipData;

typedef struct
{
	Arena *arena;
	UI_Box *sizing_item;
	UI_VirtualListBuildItem *build_item;
	void *user;
	u32 item_count;
	f32 item_extent;
}
UI_VirtualListData;

static vec2 ui_box__measure_text(UI_Box *box, UI_BoxConstraints constraints)
{
	(void)constraints;
	UI_TextBoxData *text = box->content;
	Str measured_string = text->sizing_string.size ? text->sizing_string : text->string;
	return ui_measure_text(box->ui, text->style, measured_string);
}

static void ui_box__paint_text(UI_Box *box)
{
	UI_TextBoxData *text = box->content;
	vec2 text_size = ui_measure_text(box->ui, text->style, text->string);
	vec2 remaining = v2(Max(0.f, box->viewport.w - text_size.x), Max(0.f, box->viewport.h - text_size.y));
	vec2 position = v2_add(box->viewport.pos, v2_mul(remaining, text->style.align));
	rect_f32 text_rect = { .pos = position, .size = text_size };
	b32 clip_to_viewport = text_rect.x < box->viewport.x || text_rect.y < box->viewport.y || text_rect.x + text_rect.w > box->viewport.x + box->viewport.w || text_rect.y + text_rect.h > box->viewport.y + box->viewport.h;
	ui_push_clip(box->ui, box->clip_rect);
	if (clip_to_viewport) ui_push_clip(box->ui, box->viewport);
	ui_draw_text(box->ui, text_rect, text->style, text->string);
	if (clip_to_viewport) ui_pop_clip(box->ui);
	ui_pop_clip(box->ui);
}

static const UI_BoxHooks ui_box__text_ops = {
	.measure = ui_box__measure_text,
	.paint = ui_box__paint_text,
};

void ui_box_paint(UI_Box *box)
{
	Assert(box);
	UI_Context *ui = box->ui;
	UI_BoxPaintDesc *paint = &box->paint;
	b32 has_paint = paint->flags || box->hooks && box->hooks->paint;
	if (!has_paint) return;

	b32 pushed_z = paint->z != UI_Z_CONTENT;
	if (pushed_z) ui_push_z(ui, paint->z);
	if (paint->emission > 0.f) ui_push_emission(ui, paint->emission);
	ui_push_clip(ui, box->clip_rect);
	if (paint->flags & UI_BOX_DRAW_BACKDROP) {
		ui_draw_backdrop(ui, box->rect, paint->roundness);
	}
	if (paint->flags & UI_BOX_DRAW_BACKGROUND)
	{
		ui_draw_rect(ui, (Draw_RectParams) {
			.rect = box->rect,
			.color = paint->background,
			.corner_radii = draw_corner_radii_all(paint->roundness),
			.edge_softness = paint->edge_softness,
		});
	}
	if (paint->flags & UI_BOX_DRAW_INSET_SHADOW && paint->inset_shadow > 0.f) {
		ui_draw_inset_shadow(ui, box->rect, paint->inset_shadow);
	}
	if (paint->flags & UI_BOX_DRAW_BORDER && paint->border_width > 0.f) {
		ui_draw_rect_outline(ui, box->rect, paint->border_width, paint->border);
	}
	if (box->hooks && box->hooks->paint) box->hooks->paint(box);
	ui_pop_clip(ui);
	if (paint->emission > 0.f) ui_pop_emission(ui);
	if (pushed_z) ui_pop_z(ui);
}

static UI_Box *ui_box__make_text(UI_Context *ui, UI_Key key, UI_TextStyle style, Str sizing_string, Str string)
{
	UI_Builder *builder = ui->builder;
	Assert(builder);
	Assert(ui->text);
	Assert(style.font);
	Assert(style.size > 0);
	UI_TextBoxData *text = arena_push_zero(builder->arena, sizeof(*text));
	text->string = string;
	text->sizing_string = sizing_string;
	text->style = style;
	UI_Box *box = ui_box_make(ui, key, string);
	box->hooks = &ui_box__text_ops;
	box->content = text;
	return box;
}

UI_Box *ui_text_box(UI_Context *ui, UI_Key key, UI_TextStyle style, const char *format, ...)
{
	Assert(ui);
	Assert(ui->builder);
	va_list arguments;
	va_start(arguments, format);
	Str string = str_push_copy_v(ui->builder->arena, format, arguments);
	va_end(arguments);
	return ui_box__make_text(ui, key, style, (Str) {}, string);
}

UI_Box *ui_text_sized_f(UI_Context *ui, UI_Key key, UI_TextStyle style, Str sizing_string, const char *format, ...)
{
	Assert(ui);
	Assert(ui->builder);
	va_list arguments;
	va_start(arguments, format);
	Str string = str_push_copy_v(ui->builder->arena, format, arguments);
	va_end(arguments);
	return ui_box__make_text(ui, key, style, sizing_string, string);
}

UI_Box *ui_text(UI_Context *ui, UI_Key key, UI_TextStyle style, Str string)
{
	return ui_box__make_text(ui, key, style, (Str) {}, string);
}

UI_Box *ui_text_sized(UI_Context *ui, UI_Key key, UI_TextStyle style, Str sizing_string, Str string)
{
	return ui_box__make_text(ui, key, style, sizing_string, string);
}

UI_ImageStyle ui_default_image_style(void)
{
	return (UI_ImageStyle) {
		.tint = COLOR_WHITE,
		.align = v2(0.5f, 0.5f),
		.fit = UI_IMAGE_FIT_CONTAIN,
	};
}

static vec2 ui_box__measure_image(UI_Box *box, UI_BoxConstraints constraints)
{
	(void)constraints;
	UI_ImageBoxData *image = box->content;
	return v2((f32)image->region.w, (f32)image->region.h);
}

static void ui_box__paint_image(UI_Box *box)
{
	UI_ImageBoxData *image = box->content;
	if (box->viewport.w <= 0.f || box->viewport.h <= 0.f || image->region.w <= 0 || image->region.h <= 0) return;

	rect_f32 image_rect = box->viewport;
	rect_f32 source_region = rect_f32_from_i32(image->region);
	vec2 align = v2(CLAMP(image->style.align.x, 0.f, 1.f), CLAMP(image->style.align.y, 0.f, 1.f));
	if (image->style.fit == UI_IMAGE_FIT_CONTAIN)
	{
		f32 scale = Min(box->viewport.w / source_region.w, box->viewport.h / source_region.h);
		image_rect = rect_f32_align(box->viewport, v2_mul(source_region.size, v2(scale, scale)), align);
	}
	else if (image->style.fit == UI_IMAGE_FIT_COVER)
	{
		f32 source_aspect = source_region.w / source_region.h;
		f32 destination_aspect = box->viewport.w / box->viewport.h;
		if (source_aspect > destination_aspect)
		{
			f32 width = source_region.w * destination_aspect / source_aspect;
			source_region.x += (source_region.w - width) * align.x;
			source_region.w = width;
		}
		else if (source_aspect < destination_aspect)
		{
			f32 height = source_region.h * source_aspect / destination_aspect;
			source_region.y += (source_region.h - height) * align.y;
			source_region.h = height;
		}
	}

	ui_draw_rect(box->ui, (Draw_RectParams) {
		.rect = image_rect,
		.texture = image->texture,
		.texture_region = source_region,
		.color = image->style.tint,
		.sampler = image->style.sampler,
		.corner_radii = draw_corner_radii_all(box->paint.roundness),
		.edge_softness = box->paint.edge_softness,
	});
}

static const UI_BoxHooks ui_box__image_ops = {
	.measure = ui_box__measure_image,
	.paint = ui_box__paint_image,
};

static UI_Box *ui_box__make_image(UI_Context *ui, UI_Key key, UI_ImageStyle style, GFX_Texture *texture)
{
	Assert(ui);
	Assert(ui->builder);
	// Assert(texture);
	vec2i texture_size = texture ? gfx_texture_size(texture) : v2i(1, 1);
	rect_i32 region = texture ? style.region : (rect_i32) { 0, 0, 1, 1 };
	if (!region.w) region.w = texture_size.x - region.x;
	if (!region.h) region.h = texture_size.y - region.y;
	Assert(region.x >= 0 && region.w > 0 && region.x + region.w <= texture_size.x);
	Assert(region.y >= 0 && region.h > 0 && region.y + region.h <= texture_size.y);
	Assert(style.fit >= UI_IMAGE_FIT_CONTAIN && style.fit <= UI_IMAGE_FIT_STRETCH);

	UI_ImageBoxData *image = arena_push_zero(ui->builder->arena, sizeof(*image));
	image->texture = texture;
	image->region = region;
	image->style = style;
	UI_Box *box = ui_box_make(ui, key, LIT("image"));
	box->hooks = &ui_box__image_ops;
	box->content = image;
	return box;
}

UI_Box *ui_image_box(UI_Context *ui, UI_Key key, UI_ImageStyle style, GFX_Texture *texture)
{
	return ui_box__make_image(ui, key, style, texture);
}

UI_Response ui_button(UI_Context *ui, UI_Key key, Str text)
{
	Assert(ui);
	Assert(ui->builder);

	UI_TextStyle style = ui->theme.code;
	style.align = v2(0.5f, 0.5f);

	ui_padd(ui, AXIS_X, 10.f, 10.f);
	ui_padd(ui, AXIS_Y, 6.f, 6.f);
	UI_Box *box = ui_text(ui, key, style, text);
	UI_Response response = ui_signal_from_box(box);
	if (response.pressed) ui_feedback_emit(ui, UI_FEEDBACK_PRESS);

	UI_Palette *palette = &ui->theme.palette;
	i32 paint_z = box->paint.z;
	box->paint = (UI_BoxPaintDesc) {
		.flags = UI_BOX_DRAW_BACKGROUND | UI_BOX_DRAW_BORDER,
		.background = palette->raised,
		.border = palette->divider,
		.border_width = 1.f,
		.roundness = 3.f,
		.edge_softness = 0.5f,
		.z = paint_z,
	};
	if (response.hovered)
	{
		box->paint.background = color_srgba_mix(palette->raised, palette->cyan, 0.20f);
		box->paint.border = palette->cyan;
	}
	if (response.held)
	{
		box->paint.background = color_srgba_mix(palette->raised, palette->teal, 0.45f);
		box->paint.border = palette->teal;
	}
	return response;
}

static const u32 UI_VIRTUAL_LIST_OVERSCAN = 2;

static f32 ui_virtual_list__local_min(const UI_Box *box, AXIS axis)
{
	return Max(0.f, box->desc.min_size.xy[axis]);
}

static f32 ui_virtual_list__local_max(const UI_Box *box, AXIS axis)
{
	return Max(ui_virtual_list__local_min(box, axis), box->desc.max_size.xy[axis]);
}

static rect_f32 ui_virtual_list__child_clip(UI_Box *box, rect_f32 clip)
{
	for (AXIS axis = AXIS_X; axis <= AXIS_Y; axis ++)
	{
		if (box->desc.overflow[axis] != UI_BOX_OVERFLOW_CLIP) continue;
		f32 minimum = Max(clip.xy[axis], box->viewport.xy[axis]);
		f32 maximum = Min(clip.xy[axis] + clip.wh[axis], box->viewport.xy[axis] + box->viewport.wh[axis]);
		clip.xy[axis] = minimum;
		clip.wh[axis] = Max(0.f, maximum - minimum);
	}
	return clip;
}

static vec2 ui_virtual_list__measure_children(UI_Box *box, UI_BoxConstraints constraints)
{
	UI_VirtualListData *list = box->content;
	Assert(list);
	AXIS axis = box->desc.axis;
	AXIS perp = !axis;
	vec2 content = {};
	list->item_extent = 0.f;
	if (list->item_count)
	{
		UI_BoxConstraints item_constraints = { .max = constraints.max };
		item_constraints.max.xy[axis] = UI_BOX_INFINITY;
		vec2 item_size = ui_box_measure(list->sizing_item, item_constraints);
		item_size.xy[axis] += list->sizing_item->desc.margin[axis][0] + list->sizing_item->desc.margin[axis][1];
		item_size.xy[perp] += list->sizing_item->desc.margin[perp][0] + list->sizing_item->desc.margin[perp][1];
		list->item_extent = item_size.xy[axis];
		content.xy[axis] = list->item_extent * list->item_count + box->desc.gap * (list->item_count - 1);
		content.xy[perp] = item_size.xy[perp];
	}
	return content;
}

static void ui_virtual_list__materialize(UI_Box *box, u32 first_item, u32 one_past_item)
{
	UI_VirtualListData *list = box->content;
	Assert(list);
	UI_Builder builder = {
		.arena = list->arena,
		.ui = box->ui,
		.root = box,
		.parent = box,
		.id = box->id,
		.desc = ui_defaults(),
		.paint = ui_default_paint(),
	};
	UI_Builder *previous_builder = box->ui->builder;
	box->ui->builder = &builder;
	ui_box_clear_children(box);
	ui_builder_push_box_z(&builder, box->paint.z);
	for (u32 item_index = first_item; item_index < one_past_item; item_index ++)
	{
		ui_builder_clean(&builder);
		u32 child_count = box->child_count;
		ui_box_push_id(box->ui, item_index);
		list->build_item(box->ui, item_index, list->user);
		ui_box_pop_id(box->ui);
		Assert(builder.parent == box);
		Assert(builder.parent_count == 0);
		Assert(box->child_count == child_count + 1);
	}
	ui_builder_clean(&builder);
	ui_builder_pop_box_z(&builder);
	ui_box_builder_end(&builder);
	box->ui->builder = previous_builder;
}

static void ui_virtual_list__layout(UI_Box *box, rect_f32 clip)
{
	UI_VirtualListData *list = box->content;
	Assert(list);
	AXIS axis = box->desc.axis;
	AXIS perp = !axis;
	f32 stride = list->item_extent + box->desc.gap;
	box->content_size = v2(0.f, 0.f);
	if (list->item_count)
	{
		UI_Box *item = list->sizing_item;
		f32 perp_before = item->desc.margin[perp][0];
		f32 perp_after = item->desc.margin[perp][1];
		f32 perp_size = item->measured_size.xy[perp];
		if (item->desc.size[perp].kind == UI_BOX_SIZE_FILL) {
			perp_size = Max(0.f, box->viewport.wh[perp] - perp_before - perp_after);
		}
		box->content_size.xy[axis] = list->item_extent * list->item_count + box->desc.gap * (list->item_count - 1);
		box->content_size.xy[perp] = perp_size + perp_before + perp_after;
	}
	box->content_bounds = (rect_f32) { .pos = box->viewport.pos, .size = box->content_size };

	rect_f32 child_clip = ui_virtual_list__child_clip(box, clip);

	u32 first_item = 0;
	u32 one_past_item = 0;
	f32 content_extent = box->content_size.xy[axis];
	f32 origin = box->viewport.xy[axis];
	f32 clip_min = child_clip.xy[axis];
	f32 clip_max = clip_min + Max(child_clip.wh[axis], 0.f);
	f32 visible_min = CLAMP(clip_min - origin, 0.f, content_extent);
	f32 visible_max = CLAMP(clip_max - origin, 0.f, content_extent);
	if (list->item_count && stride > 0.001f && child_clip.w > 0.f && child_clip.h > 0.f && visible_max > visible_min)
	{
		first_item = Min((u32)floorf(visible_min / stride), list->item_count);
		one_past_item = Min((u32)ceilf(visible_max / stride), list->item_count);
		first_item = first_item > UI_VIRTUAL_LIST_OVERSCAN ? first_item - UI_VIRTUAL_LIST_OVERSCAN : 0;
		one_past_item = list->item_count - one_past_item < UI_VIRTUAL_LIST_OVERSCAN ? list->item_count : one_past_item + UI_VIRTUAL_LIST_OVERSCAN;
	}
	ui_virtual_list__materialize(box, first_item, one_past_item);

	vec2 available = box->viewport.size;
	u32 item_index = first_item;
	for (UI_Box *item = box->first; item; item = item->next, item_index ++)
	{
		UI_BoxConstraints constraints = { .max = available };
		constraints.max.xy[axis] = list->item_extent;
		ui_box_measure(item, constraints);

		f32 main_before = item->desc.margin[axis][0];
		f32 main_after = item->desc.margin[axis][1];
		f32 perp_before = item->desc.margin[perp][0];
		f32 perp_after = item->desc.margin[perp][1];
		f32 perp_available = Max(0.f, box->viewport.wh[perp] - perp_before - perp_after);
		f32 perp_size = item->measured_size.xy[perp];
		if (item->desc.size[perp].kind == UI_BOX_SIZE_FILL) {
			perp_size = perp_available;
		}
		perp_size = CLAMP(perp_size, ui_virtual_list__local_min(item, perp), ui_virtual_list__local_max(item, perp));

		rect_f32 item_rect = {};
		item_rect.xy[axis] = box->viewport.xy[axis] + item_index * stride + main_before;
		item_rect.wh[axis] = Max(0.f, list->item_extent - main_before - main_after);
		item_rect.xy[perp] = box->viewport.xy[perp] + perp_before;
		item_rect.wh[perp] = perp_size;
		ui_box_layout_clipped(item, item_rect, child_clip);
	}
}

static const UI_LayoutHooks ui_virtual_list__layout_hooks = {
	.measure_children = ui_virtual_list__measure_children,
	.layout_children = ui_virtual_list__layout,
};

UI_Box *ui_virtual_list(UI_Context *ui, UI_Key key, Str name, UI_VirtualListDesc list)
{
	Assert(ui);
	Assert(ui->builder);
	Assert(list.build_item);
	UI_VirtualListData *data = arena_push_zero(ui->builder->arena, sizeof(*data));
	data->arena = ui->builder->arena;
	data->build_item = list.build_item;
	data->user = list.user;
	data->item_count = list.item_count;

	ui_layout(ui, &ui_virtual_list__layout_hooks);
	UI_Box *box = ui_box_begin(ui, key, name);
	box->content = data;
	ui_clean(ui);
	if (list.item_count)
	{
		ui_push_box_z(ui, box->paint.z);
		ui_box_push_id(ui, 0);
		list.build_item(ui, 0, list.user);
		ui_box_pop_id(ui);
		ui_pop_box_z(ui);
		Assert(box->child_count == 1);
		data->sizing_item = box->first;
		ui_box_clear_children(box);
	}
	ui_clean(ui);
	ui_box_end(ui);
	return box;
}

static void ui_tooltip__prepare_layout(UI_Box *box)
{
	UI_TooltipData *tooltip = box->content;
	Assert(tooltip);

	rect_f32 bounds = box->clip_rect;
	vec2 preferred = v2_add(tooltip->anchor, tooltip->offset);
	vec2 position = {};
	for (AXIS axis = AXIS_X; axis <= AXIS_Y; ++axis)
	{
		f32 margin = Min(tooltip->margin, Max(0.f, bounds.wh[axis]) * 0.5f);
		f32 minimum = bounds.xy[axis] + margin;
		f32 maximum = bounds.xy[axis] + bounds.wh[axis] - margin - box->rect.wh[axis];
		position.xy[axis] = CLAMP(preferred.xy[axis], minimum, Max(minimum, maximum));
	}

	vec2 delta = v2_sub(position, box->rect.pos);
	box->rect.pos = position;
	box->viewport.pos = v2_add(box->viewport.pos, delta);
}

static const UI_BoxHooks ui_tooltip__ops = {
	.prepare_layout = ui_tooltip__prepare_layout,
};

static void ui_tooltip__raise_subtree(UI_Box *box)
{
	box->paint.z = Max(box->paint.z, UI_Z_OVERLAY);
	for (UI_Box *child = box->first; child; child = child->next) {
		ui_tooltip__raise_subtree(child);
	}
}

UI_Box *ui_tooltip_begin(UI_Context *ui, UI_Key owner_key, vec2 screen_anchor)
{
	Assert(ui);
	Assert(ui->builder);
	Assert(ui->overlay_root);
	if (ui->builder->root != ui->root) return 0;
	if (ui->tooltip_box) return 0;
	Assert(!ui->tooltip_open);

	UI_Builder *builder = ui->builder;
	UI_TooltipData *tooltip = arena_push_zero(builder->arena, sizeof(*tooltip));
	tooltip->previous_parent = builder->parent;
	tooltip->previous_id = builder->id;
	tooltip->overlay_id = ui_id_child(ui->overlay_root->id, tooltip->previous_id.value);
	tooltip->parent_count = builder->parent_count;
	tooltip->id_count = builder->id_count;
	tooltip->anchor = screen_anchor;
	tooltip->offset = v2(14.f, 18.f);
	tooltip->margin = 8.f;

	builder->parent = ui->overlay_root;
	builder->id = tooltip->overlay_id;
	ui_builder_clean(builder);
	builder->paint.z = UI_Z_OVERLAY;

	vec2 window_size = v2_from_v2i(ui->window->size);
	ui_builder_position(builder, AXIS_X, 0.f);
	ui_builder_position(builder, AXIS_Y, 0.f);
	ui_builder_max_size(builder, AXIS_X, Max(0.f, window_size.x - tooltip->margin * 2.f));
	ui_builder_max_size(builder, AXIS_Y, Max(0.f, window_size.y - tooltip->margin * 2.f));
	ui_builder_padd(builder, AXIS_X, 8.f, 8.f);
	ui_builder_padd(builder, AXIS_Y, 6.f, 6.f);
	ui_builder_gap(builder, 2.f);
	ui_builder_overflow(builder, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_builder_overflow(builder, AXIS_Y, UI_BOX_OVERFLOW_CLIP);

	UI_Box *box = ui_builder_box_begin(builder, owner_key, LIT("tooltip"));
	box->hooks = &ui_tooltip__ops;
	box->content = tooltip;
	box->paint.flags = UI_BOX_DRAW_BACKDROP;
	box->paint.roundness = 5.f;
	ui_builder_clean(builder);

	ui->tooltip_box = box;
	ui->tooltip_open = true;
	return box;
}

void ui_tooltip_end(UI_Context *ui)
{
	Assert(ui);
	Assert(ui->builder);
	Assert(ui->tooltip_box);
	Assert(ui->tooltip_open);

	UI_Builder *builder = ui->builder;
	UI_Box *box = ui->tooltip_box;
	UI_TooltipData *tooltip = box->content;
	Assert(tooltip);
	Assert(builder->parent == box);
	Assert(builder->parent_count == tooltip->parent_count + 1);
	Assert(builder->id_count == tooltip->id_count);

	ui_builder_box_end(builder);
	Assert(builder->parent == ui->overlay_root);
	Assert(builder->parent_count == tooltip->parent_count);
	Assert(ui_id_equal(builder->id, tooltip->overlay_id));
	ui_tooltip__raise_subtree(box);
	ui_builder_clean(builder);
	builder->parent = tooltip->previous_parent;
	builder->id = tooltip->previous_id;

	ui->tooltip_open = false;
}

static const u64 UI_SCROLL_BOX_VIEWPORT_KEY = 0x5343524F4C4C5650ull;
static const u64 UI_SCROLL_BOX_TRACK_KEY = 0x5343524F4C4C4241ull;
static const u64 UI_SCROLL_BOX_SPACE_BEFORE_KEY = 1;
static const u64 UI_SCROLL_BOX_THUMB_KEY = 2;
static const u64 UI_SCROLL_BOX_SPACE_AFTER_KEY = 3;

static f32 ui_scroll_box__smooth(f32 offset, f32 target, f32 elapsed)
{
	f32 half_life = 0.055f;
	return target + (offset - target) * exp2f(-Max(elapsed, 0.f) / half_life);
}

static f32 ui_scroll_box__local_min(const UI_Box *box, AXIS axis)
{
	return Max(0.f, box->desc.min_size.xy[axis]);
}

static f32 ui_scroll_box__local_max(const UI_Box *box, AXIS axis)
{
	return Max(ui_scroll_box__local_min(box, axis), box->desc.max_size.xy[axis]);
}

static void ui_scroll_box__size_bar(UI_ScrollBox *scroll, f32 track_extent, f32 viewport_extent, f32 content_extent, f32 scroll_max, f32 scroll_offset)
{
	AXIS axis = scroll->axis;
	f32 thumb_extent = track_extent;
	f32 thumb_offset = 0.f;
	if (scroll_max > 0.001f && content_extent > 0.f)
	{
		thumb_extent = Min(Max(24.f, track_extent * viewport_extent / content_extent), track_extent);
		f32 travel = Max(0.f, track_extent - thumb_extent);
		f32 ratio = scroll_offset / scroll_max;
		thumb_offset = travel * CLAMP(ratio, 0.f, 1.f);
	}

	scroll->space_before->desc.size[axis] = ui_grow(thumb_offset);
	scroll->thumb->desc.size[axis] = ui_grow(thumb_extent);
	scroll->space_after->desc.size[axis] = ui_grow(Max(track_extent - thumb_offset - thumb_extent, 0.f));
}

static vec2 ui_scroll_box__measure_content(UI_Box *box, UI_BoxConstraints constraints)
{
	UI_ScrollBox *scroll = box->content;
	Assert(scroll);
	Assert(box == scroll->viewport);
	Assert(scroll->content);
	AXIS axis = scroll->axis;
	UI_Box *content = scroll->content;
	UI_BoxConstraints content_constraints = { .max = constraints.max };
	for (AXIS constraint_axis = AXIS_X; constraint_axis <= AXIS_Y; constraint_axis ++) {
		f32 margins = content->desc.margin[constraint_axis][0] + content->desc.margin[constraint_axis][1];
		content_constraints.max.xy[constraint_axis] = Max(0.f, content_constraints.max.xy[constraint_axis] - margins);
	}
	content_constraints.max.xy[axis] = UI_BOX_INFINITY;

	UI_BoxSize scroll_size = content->desc.size[axis];
	if (scroll_size.kind == UI_BOX_SIZE_FILL) content->desc.size[axis] = ui_wrap();
	vec2 content_size = ui_box_measure(content, content_constraints);
	content->desc.size[axis] = scroll_size;
	for (AXIS content_axis = AXIS_X; content_axis <= AXIS_Y; content_axis ++) content_size.xy[content_axis] += content->desc.margin[content_axis][0] + content->desc.margin[content_axis][1];
	return content_size;
}

static void ui_scroll_box__layout(UI_Box *box, rect_f32 clip)
{
	UI_ScrollBox *scroll = box->content;
	Assert(scroll);
	Assert(box == scroll->viewport);
	Assert(scroll->content);
	UI_Box *content = scroll->content;
	AXIS axis = scroll->axis;
	AXIS perp = !axis;
	vec2 content_size = content->measured_size;

	for (AXIS content_axis = AXIS_X; content_axis <= AXIS_Y; content_axis ++)
	{
		f32 before = content->desc.margin[content_axis][0];
		f32 after = content->desc.margin[content_axis][1];
		f32 available = Max(0.f, box->viewport.wh[content_axis] - before - after);
		UI_BoxSize size = content->desc.size[content_axis];
		if (content_axis == axis && size.kind == UI_BOX_SIZE_FILL) content_size.xy[content_axis] = Max(content_size.xy[content_axis], available);
		if (content_axis == perp && (size.kind == UI_BOX_SIZE_FILL || (available > content_size.xy[content_axis] && size.grow > 0.f) || (available < content_size.xy[content_axis] && size.shrink > 0.f))) content_size.xy[content_axis] = available;
		content_size.xy[content_axis] = CLAMP(content_size.xy[content_axis], ui_scroll_box__local_min(content, content_axis), ui_scroll_box__local_max(content, content_axis));
	}

	f32 content_extent = content->desc.margin[axis][0] + content_size.xy[axis] + content->desc.margin[axis][1];
	scroll->scroll_max = Max(content_extent - box->viewport.wh[axis], 0.f);
	scroll->offset = CLAMP(scroll->offset, 0.f, scroll->scroll_max);
	scroll->target = CLAMP(scroll->target, 0.f, scroll->scroll_max);
	scroll->root->state->view_offset.xy[axis] = scroll->offset;
	scroll->root->state->view_target.xy[axis] = scroll->target;

	box->content_size = v2(content->desc.margin[AXIS_X][0] + content_size.x + content->desc.margin[AXIS_X][1], content->desc.margin[AXIS_Y][0] + content_size.y + content->desc.margin[AXIS_Y][1]);
	box->content_bounds = (rect_f32) { .pos = box->viewport.pos, .size = box->content_size };
	box->content_bounds.xy[axis] -= scroll->offset;

	rect_f32 content_rect = { .size = content_size };
	content_rect.xy[axis] = box->viewport.xy[axis] + content->desc.margin[axis][0] - scroll->offset;
	content_rect.xy[perp] = box->viewport.xy[perp] + content->desc.margin[perp][0];
	ui_box_layout_clipped(content, content_rect, rect_f32_intersect(clip, box->viewport));
}

static void ui_scroll_box__prepare_track(UI_Box *box)
{
	UI_ScrollBox *scroll = box->content;
	Assert(scroll);
	Assert(box == scroll->track);
	f32 viewport_extent = scroll->viewport->viewport.wh[scroll->axis];
	f32 content_extent = Max(scroll->viewport->content_size.xy[scroll->axis], viewport_extent);
	ui_scroll_box__size_bar(scroll, box->viewport.wh[scroll->axis], viewport_extent, content_extent, scroll->scroll_max, scroll->offset);
}

static void ui_scroll_box__layout_root(UI_Box *box, rect_f32 clip)
{
	UI_ScrollBox *scroll = box->content;
	Assert(scroll);
	Assert(box == scroll->root);
	Assert(scroll->viewport);
	Assert(scroll->track);
	AXIS axis = scroll->axis;
	AXIS perp = !axis;
	f32 track_extent = CLAMP(scroll->track->measured_size.xy[perp], ui_scroll_box__local_min(scroll->track, perp), ui_scroll_box__local_max(scroll->track, perp));
	track_extent = Min(track_extent, box->viewport.wh[perp]);

	rect_f32 viewport_rect = box->viewport;
	viewport_rect.wh[perp] = Max(0.f, box->viewport.wh[perp] - track_extent);
	rect_f32 track_rect = box->viewport;
	track_rect.xy[perp] += viewport_rect.wh[perp];
	track_rect.wh[perp] = track_extent;

	box->content_size = box->viewport.size;
	box->content_bounds = box->viewport;
	ui_box_layout_clipped(scroll->viewport, viewport_rect, clip);
	ui_box_layout_clipped(scroll->track, track_rect, clip);
}

static const UI_LayoutHooks ui_scroll_box__viewport_layout = {
	.measure_children = ui_scroll_box__measure_content,
	.layout_children = ui_scroll_box__layout,
};

static const UI_BoxHooks ui_scroll_box__track_hooks = {
	.prepare_layout = ui_scroll_box__prepare_track,
};

static const UI_LayoutHooks ui_scroll_box__root_layout = {
	.measure_children = ui_linear_measure_children,
	.layout_children = ui_scroll_box__layout_root,
};

UI_ScrollBox *ui_scroll_box_begin(UI_Context *ui, UI_Key key, AXIS axis)
{
	Assert(ui);
	UI_Builder *builder = ui->builder;
	Assert(builder);
	Assert(axis == AXIS_X || axis == AXIS_Y);

	UI_ScrollBox *scroll = arena_push_zero(builder->arena, sizeof(*scroll));
	scroll->ui = ui;
	scroll->axis = axis;
	scroll->parent_count = builder->parent_count;

	ui_axis(ui, !axis);
	ui_gap(ui, 0.f);
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_VISIBLE);
	ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_VISIBLE);
	ui_layout(ui, &ui_scroll_box__root_layout);
	scroll->root = ui_box_begin(ui, key, LIT("scroll box"));
	scroll->root->content = scroll;
	ui_clean(ui);

	ui_size(ui, AXIS_X, ui_flex(1.f, 1.f));
	ui_size(ui, AXIS_Y, ui_flex(1.f, 1.f));
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_CLIP);
	ui_layout(ui, &ui_scroll_box__viewport_layout);
	scroll->viewport = ui_box_begin(ui, UI_SCROLL_BOX_VIEWPORT_KEY, LIT("scroll viewport"));
	// scroll->viewport->paint = (UI_BoxPaintDesc) { .z = scroll->viewport->paint.z };
	scroll->viewport->content = scroll;

	// Keep caller content IDs stable even though the internal viewport is now
	// its physical parent.
	builder->id = scroll->root->id;
	ui_clean(ui);
	scroll->offset = scroll->root->state->view_offset.xy[axis];
	scroll->target = scroll->root->state->view_target.xy[axis];
	return scroll;
}

void ui_scroll_box_reset(UI_ScrollBox *scroll)
{
	Assert(scroll);
	scroll->reset = true;
	scroll->has_previous = false;
	scroll->offset = 0.f;
	scroll->target = 0.f;
	scroll->root->state->view_offset.xy[scroll->axis] = 0.f;
	scroll->root->state->view_target.xy[scroll->axis] = 0.f;
	UI_Id track_id = ui_id_child(scroll->root->id, UI_SCROLL_BOX_TRACK_KEY);
	UI_Id thumb_id = ui_id_child(track_id, UI_SCROLL_BOX_THUMB_KEY);
	if (ui_id_equal(scroll->ui->active, track_id) || ui_id_equal(scroll->ui->active, thumb_id)) scroll->ui->active = UI_ID_NONE;
}

void ui_scroll_box_end(UI_ScrollBox *scroll)
{
	Assert(scroll);
	UI_Context *ui = scroll->ui;
	UI_Builder *builder = ui->builder;
	AXIS axis = scroll->axis;
	AXIS perp = !axis;
	Assert(builder->parent == scroll->viewport);
	Assert(builder->parent_count == scroll->parent_count + 2);
	Assert(scroll->viewport->child_count == 1);

	scroll->content = scroll->viewport->first;
	Assert(scroll->content == scroll->viewport->last);
	Assert(!ui_id_equal(scroll->content->id, scroll->viewport->id));
	ui_box_end(ui);
	Assert(builder->parent == scroll->root);
	ui_clean(ui);

	ui_axis(ui, axis);
	ui_size(ui, axis, ui_grow(1.f));
	ui_size(ui, perp, ui_fixed(12.f));
	ui_padd(ui, axis, 3.f, 3.f);
	ui_padd(ui, perp, 3.f, 3.f);
	ui_background(ui, ui->theme.slider_track);
	ui_roundness(ui, 0.5f);
	ui_edge_softness(ui, 0.5f);
	// ui_paint_z(ui, track_z);
	scroll->track = ui_box_begin(ui, UI_SCROLL_BOX_TRACK_KEY, LIT(""));
	Assert(!ui_id_equal(scroll->content->id, scroll->track->id));

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_size(ui, axis, ui_grow(0.f));
	scroll->space_before = ui_box_make(ui, UI_SCROLL_BOX_SPACE_BEFORE_KEY, LIT(""));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_size(ui, axis, ui_grow(1.f));
	ui_background(ui, ui->theme.slider_thumb);
	ui_roundness(ui, 0.5f);
	ui_edge_softness(ui, 0.5f);
	scroll->thumb = ui_box_make(ui, UI_SCROLL_BOX_THUMB_KEY, LIT(""));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_size(ui, axis, ui_grow(0.f));
	scroll->space_after = ui_box_make(ui, UI_SCROLL_BOX_SPACE_AFTER_KEY, LIT(""));
	ui_box_end(ui);

	UI_BoxState *root_state = scroll->root->state;
	UI_BoxState *viewport_state = scroll->viewport->state;
	UI_BoxState *track_state = scroll->track->state;
	UI_BoxState *thumb_state = scroll->thumb->state;
	Assert(root_state);
	Assert(viewport_state);
	Assert(track_state);
	Assert(thumb_state);

	scroll->has_previous = scroll->root->has_previous && scroll->viewport->has_previous && scroll->track->has_previous && scroll->thumb->has_previous;
	scroll->offset = root_state->view_offset.xy[axis];
	scroll->target = root_state->view_target.xy[axis];
	if (scroll->reset)
	{
		root_state->view_offset.xy[axis] = 0.f;
		root_state->view_target.xy[axis] = 0.f;
		scroll->has_previous = false;
		scroll->offset = 0.f;
		scroll->target = 0.f;
	}

	if (scroll->has_previous)
	{
		f32 scroll_max = Max(viewport_state->content_size.xy[axis] - viewport_state->viewport.wh[axis], 0.f);
		i32 wheel = ui->window->mouse_wheel.xy[axis];
		if (wheel && !ui->mouse_wheel_consumed && rect_f32_contains(viewport_state->hit_rect, ui->mouse))
		{
			b32 can_scroll = wheel > 0 ? scroll->target > 0.f || scroll->offset > 0.f : scroll->target < scroll_max || scroll->offset < scroll_max;
			if (can_scroll)
			{
				scroll->target = CLAMP(scroll->target - wheel * 48.f, 0.f, scroll_max);
				ui->mouse_wheel_consumed = true;
			}
		}

		f32 travel = Max(0.f, track_state->viewport.wh[axis] - thumb_state->rect.wh[axis]);
		UI_Response thumb_response = ui_signal_from_box(scroll->thumb);
		if (thumb_response.pressed) {
			ui->active_start_value = scroll->offset;
		}
		if (thumb_response.held && travel > 0.f && scroll_max > 0.f)
		{
			scroll->offset = CLAMP(ui->active_start_value + thumb_response.drag_delta.xy[axis] * scroll_max / travel, 0.f, scroll_max);
			scroll->target = scroll->offset;
		}

		if (!thumb_response.hovered && !ui_is_active(ui, scroll->thumb->id))
		{
			UI_Response track_response = ui_signal_from_box(scroll->track);
			if (track_response.pressed && scroll_max > 0.f)
			{
				f32 direction = ui->mouse.xy[axis] < thumb_state->rect.xy[axis] ? -1.f : 1.f;
				scroll->target += direction * viewport_state->viewport.wh[axis] * 0.85f;
			}
		}

		scroll->target = CLAMP(scroll->target, 0.f, scroll_max);
		if (!ui_is_active(ui, scroll->thumb->id)) {
			scroll->offset = ui_scroll_box__smooth(scroll->offset, scroll->target, ui->frame_elapsed);
		}
		scroll->offset = CLAMP(scroll->offset, 0.f, scroll_max);
	}
	root_state->view_offset.xy[axis] = scroll->offset;
	root_state->view_target.xy[axis] = scroll->target;

	scroll->track->content = scroll;
	scroll->track->hooks = &ui_scroll_box__track_hooks;

	ui_box_end(ui);
	ui_clean(ui);
}

UI_BoxTableColumn ui_box_table_content(void)
{
	return (UI_BoxTableColumn) { .kind = UI_BOX_TABLE_COLUMN_CONTENT };
}

UI_BoxTableColumn ui_box_table_fixed(f32 width)
{
	return (UI_BoxTableColumn) { .kind = UI_BOX_TABLE_COLUMN_FIXED, .value = Max(0.f, width) };
}

UI_BoxTableColumn ui_box_table_flex(f32 weight)
{
	return (UI_BoxTableColumn) { .kind = UI_BOX_TABLE_COLUMN_FLEX, .value = Max(0.f, weight) };
}

static vec2 ui_box__measure_table(UI_Box *box, UI_BoxConstraints constraints)
{
	(void)constraints;
	UI_BoxTableData *table = box->content;
	Assert(table);
	memory_zero(table->natural_widths, table->column_count * sizeof(*table->natural_widths));

	for (UI_Box *row = box->first; row; row = row->next)
	{
		Assert(row->child_count == table->column_count);
		u32 column = 0;
		for (UI_Box *cell = row->first; cell; cell = cell->next, column++)
		{
			Assert(column < table->column_count);
			cell->desc.size[AXIS_X] = ui_wrap();
			vec2 measured = ui_box_measure(cell, (UI_BoxConstraints) { .max = v2(UI_BOX_INFINITY, table->row_height) });
			table->natural_widths[column] = Max(table->natural_widths[column], measured.x);
		}
		Assert(column == table->column_count);
	}

	f32 table_width = table->column_gap * Max((i32)table->column_count - 1, 0);
	for (u32 column = 0; column < table->column_count; column++)
	{
		UI_BoxTableColumn spec = table->columns[column];
		table->resolved_widths[column] = spec.kind == UI_BOX_TABLE_COLUMN_FIXED ? spec.value : table->natural_widths[column];
		table_width += table->resolved_widths[column];
	}
	for (UI_Box *row = box->first; row; row = row->next)
	{
		row->measured_size = row->arranged_size = v2(table_width, table->row_height);
		u32 column = 0;
		for (UI_Box *cell = row->first; cell; cell = cell->next, column++)
		{
			Assert(column < table->column_count);
			cell->desc.size[AXIS_X] = ui_fixed(table->resolved_widths[column]);
			cell->measured_size.x = cell->arranged_size.x = table->resolved_widths[column];
		}
		Assert(column == table->column_count);
	}

	f32 table_height = table->row_height * box->child_count + box->desc.gap * Max((i32)box->child_count - 1, 0);
	return v2(table_width, table_height);
}

static void ui_box__prepare_table_layout(UI_Box *box)
{
	UI_BoxTableData *table = box->content;
	f32 gap_width = table->column_gap * Max((i32)table->column_count - 1, 0);
	f32 fixed_width = 0.f;
	f32 content_width = 0.f;
	f32 flex_weight = 0.f;
	for (u32 column = 0; column < table->column_count; column++)
	{
		UI_BoxTableColumn spec = table->columns[column];
		if (spec.kind == UI_BOX_TABLE_COLUMN_FIXED) {
			table->resolved_widths[column] = spec.value;
			fixed_width += spec.value;
		}
		else if (spec.kind == UI_BOX_TABLE_COLUMN_CONTENT) {
			table->resolved_widths[column] = table->natural_widths[column];
			content_width += table->natural_widths[column];
		}
		else {
			flex_weight += spec.value;
		}
	}

	// Fixed columns are rigid. Content columns prefer their natural width, but
	// must yield when the table is narrower so their raw cell geometry cannot
	// extend beneath adjacent layout siblings such as a scrollbar.
	f32 content_space = Max(0.f, box->viewport.w - gap_width - fixed_width);
	f32 content_scale = content_width > content_space && content_width > 0.f ? content_space / content_width : 1.f;
	if (content_scale < 1.f)
	{
		for (u32 column = 0; column < table->column_count; column++)
		{
			if (table->columns[column].kind == UI_BOX_TABLE_COLUMN_CONTENT) table->resolved_widths[column] = table->natural_widths[column] * content_scale;
		}
	}
	f32 resolved_content_width = content_width * content_scale;
	f32 flex_space = Max(0.f, box->viewport.w - gap_width - fixed_width - resolved_content_width);
	for (u32 column = 0; column < table->column_count; column++)
	{
		UI_BoxTableColumn spec = table->columns[column];
		if (spec.kind == UI_BOX_TABLE_COLUMN_FLEX) {
			table->resolved_widths[column] = flex_weight > 0.f ? flex_space * spec.value / flex_weight : table->natural_widths[column];
		}
	}

	f32 table_width = table->column_gap * Max((i32)table->column_count - 1, 0);
	for (u32 column = 0; column < table->column_count; column++) {
		table_width += table->resolved_widths[column];
	}
	for (UI_Box *row = box->first; row; row = row->next)
	{
		row->measured_size.x = row->arranged_size.x = table_width;
		u32 column = 0;
		for (UI_Box *cell = row->first; cell; cell = cell->next, column++)
		{
			Assert(column < table->column_count);
			cell->desc.size[AXIS_X] = ui_fixed(table->resolved_widths[column]);
			cell->measured_size.x = cell->arranged_size.x = table->resolved_widths[column];
		}
		Assert(column == table->column_count);
	}
}

static const UI_BoxHooks ui_box__table_ops = {
	.prepare_layout = ui_box__prepare_table_layout,
};

static const UI_LayoutHooks ui_box__table_layout = {
	.measure_children = ui_box__measure_table,
	.layout_children = ui_linear_layout_children,
};

UI_BoxTable ui_box_table_begin(UI_Context *ui, UI_Key key, Str name, UI_BoxTableDesc desc)
{
	Assert(ui);
	UI_Builder *builder = ui->builder;
	Assert(builder);
	Assert(desc.columns);
	Assert(desc.column_count);
	Assert(desc.row_height > 0.f);
	UI_BoxTableData *data = arena_push_zero(builder->arena, sizeof(*data));
	data->columns = arena_push_copy(builder->arena, desc.column_count * sizeof(*data->columns), desc.columns);
	data->natural_widths = arena_push_zero(builder->arena, desc.column_count * sizeof(*data->natural_widths));
	data->resolved_widths = arena_push_zero(builder->arena, desc.column_count * sizeof(*data->resolved_widths));
	data->column_count = desc.column_count;
	data->row_height = desc.row_height;
	data->column_gap = desc.column_gap;

	ui_axis(ui, AXIS_Y);
	ui_gap(ui, desc.row_gap);
	ui_layout(ui, &ui_box__table_layout);
	UI_Box *box = ui_box_begin(ui, key, name);
	box->content = data;
	box->hooks = &ui_box__table_ops;
	ui_clean(ui);
	return (UI_BoxTable) {
		.ui = ui,
		.box = box,
		.desc = desc,
	};
}

UI_Box *ui_box_table_row_begin(UI_BoxTable *table, UI_Key key)
{
	Assert(table);
	Assert(table->ui);
	Assert(!table->row);
	ui_clean(table->ui);
	ui_axis(table->ui, AXIS_X);
	ui_size(table->ui, AXIS_X, ui_grow(1.f));
	ui_size(table->ui, AXIS_Y, ui_fixed(table->desc.row_height));
	ui_gap(table->ui, table->desc.column_gap);
	table->row = ui_box_begin(table->ui, key, LIT("table row"));
	table->column_index = 0;
	return table->row;
}

void ui_box_table_row_end(UI_BoxTable *table)
{
	Assert(table);
	Assert(table->row);
	Assert(!table->cell);
	Assert(table->column_index == table->desc.column_count);
	ui_box_end(table->ui);
	ui_clean(table->ui);
	table->row = 0;
}

UI_Box *ui_box_table_cell_begin(UI_BoxTable *table)
{
	Assert(table);
	Assert(table->row);
	Assert(!table->cell);
	Assert(table->column_index < table->desc.column_count);
	ui_clean(table->ui);
	ui_size(table->ui, AXIS_Y, ui_grow(1.f));
	ui_padd(table->ui, AXIS_X, table->desc.cell_padd.x, table->desc.cell_padd.x);
	ui_padd(table->ui, AXIS_Y, table->desc.cell_padd.y, table->desc.cell_padd.y);
	ui_overflow(table->ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	table->cell = ui_box_begin(table->ui, table->column_index + 1, LIT("table cell"));
	table->column_index++;
	return table->cell;
}

void ui_box_table_cell_end(UI_BoxTable *table)
{
	Assert(table);
	Assert(table->cell);
	ui_box_end(table->ui);
	ui_clean(table->ui);
	table->cell = 0;
}

UI_Box *ui_box_table_end(UI_BoxTable *table)
{
	Assert(table);
	Assert(table->box);
	Assert(!table->row);
	Assert(!table->cell);
	ui_box_end(table->ui);
	ui_clean(table->ui);
	return table->box;
}
