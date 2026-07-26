#ifndef UI_BOX_H
#define UI_BOX_H

#include "base.h"
#include "ui_id.h"

enum
{
	UI_BOX_MAX_DEPTH = 64,
	UI_BOX_MAX_PENDING_CHILDREN = 4096,
};

#define UI_BOX_INFINITY 1000000000.f

typedef enum
{
	UI_BOX_SIZE_CONTENT,
	UI_BOX_SIZE_PIXELS,
	UI_BOX_SIZE_FILL,
}
UI_BoxSizeKind;

typedef enum
{
	UI_BOX_OVERFLOW_VISIBLE,
	UI_BOX_OVERFLOW_CLIP,
	UI_BOX_OVERFLOW_SCROLL,
}
UI_BoxOverflow;

typedef struct
{
	UI_BoxSizeKind kind;
	f32 value;
	f32 grow;
	f32 shrink;
}
UI_BoxSize;

typedef struct
{
	UI_BoxSize size[2];
	vec2 min_size;
	vec2 max_size;
	union
	{
		struct
		{
			f32 horz_margin[2];
			f32 vert_margin[2];
		};
		f32 margin[2][2];
	};
	union
	{
		struct
		{
			f32 horz_padd[2];
			f32 vert_padd[2];
		};
		f32 padd[2][2];
	};
	AXIS axis;
	f32 gap;
	f32 perp_align;
	UI_BoxOverflow overflow[2];
}
UI_BoxDesc;

typedef struct
{
	vec2 min;
	vec2 max;
}
UI_BoxConstraints;

typedef struct UI_Box UI_Box;
typedef struct UI_BoxBuilder UI_BoxBuilder;
typedef struct UI_Context UI_Context;
typedef void UI_BoxVirtualBuildItem(UI_BoxBuilder *builder, u32 item_index, void *user);
typedef vec2 UI_BoxMeasure(UI_Box *box, UI_BoxConstraints constraints);
typedef void UI_BoxPaint(UI_Box *box);

typedef struct
{
	UI_BoxMeasure *measure;
	UI_BoxPaint *paint;
}
UI_BoxOps;

typedef struct
{
	u32 item_count;
	void *user;
	UI_BoxVirtualBuildItem *build_item;
}
UI_BoxVirtualListDesc;

struct UI_Box
{
	UI_Id id;
	String name;
	UI_BoxDesc desc;
	UI_Box **children;
	u32 child_count;
	vec2 intrinsic_size;
	vec2 measured_size;
	vec2 arranged_size;
	rect_f32 rect;
	rect_f32 viewport;
	rect_f32 content_bounds;
	rect_f32 clip_rect;
	vec2 content_size;
	vec2 scroll_offset;
	vec2 scroll_min;
	vec2 scroll_max;
	struct
	{
		Arena *arena;
		UI_Box *sizing_item;
		UI_BoxVirtualBuildItem *build_item;
		void *user;
		u32 item_count;
		u32 first_item;
		u32 one_past_item;
		f32 item_extent;
	}
	virtual_list;
	u32 virtual_index;
	UI_Context *ui;
	const UI_BoxOps *ops;
	void *content;
	void *user;
};

struct UI_BoxBuilder
{
	Arena *arena;
	UI_Context *ui;
	UI_Box *root;
	UI_Box *parent;
	UI_Box *parent_stack[UI_BOX_MAX_DEPTH];
	UI_Id parent_id_stack[UI_BOX_MAX_DEPTH];
	UI_Id id;
	UI_Id id_stack[UI_BOX_MAX_DEPTH];
	UI_Box *child_stack[UI_BOX_MAX_PENDING_CHILDREN];
	UI_BoxDesc desc;
	UI_BoxDesc desc_stack[UI_BOX_MAX_DEPTH];
	u32 parent_count;
	u32 id_count;
	u32 child_count;
	u32 desc_count;
};

UI_BoxSize ui_box_content(void);
UI_BoxSize ui_box_pixels(f32 value);
UI_BoxSize ui_box_fill(f32 grow);
UI_BoxSize ui_box_flex(f32 grow, f32 shrink);
UI_BoxDesc ui_box_desc(void);

UI_Box *ui_box_builder_begin(UI_BoxBuilder *builder, Arena *arena, UI_Context *ui, u64 root_key, String root_name, UI_BoxDesc root_desc);
UI_Box *ui_box_make(UI_BoxBuilder *builder, u64 key, String name);
UI_Box *ui_box_begin(UI_BoxBuilder *builder, u64 key, String name);
UI_Box *ui_box_make_virtual_list(UI_BoxBuilder *builder, u64 key, String name, UI_BoxVirtualListDesc list);
UI_Box *ui_box_make_desc(UI_BoxBuilder *builder, u64 key, String name, UI_BoxDesc desc);
UI_Box *ui_box_begin_desc(UI_BoxBuilder *builder, u64 key, String name, UI_BoxDesc desc);
UI_Box *ui_box_make_virtual_list_desc(UI_BoxBuilder *builder, u64 key, String name, UI_BoxDesc desc, UI_BoxVirtualListDesc list);
void ui_box_end(UI_BoxBuilder *builder);
UI_Box *ui_box_builder_end(UI_BoxBuilder *builder);
void ui_box_push_id(UI_BoxBuilder *builder, u64 key);
void ui_box_pop_id(UI_BoxBuilder *builder);

void ui_push(UI_BoxBuilder *builder);
void ui_pop(UI_BoxBuilder *builder);
void ui_size(UI_BoxBuilder *builder, AXIS axis, UI_BoxSize size);
void ui_min_size(UI_BoxBuilder *builder, AXIS axis, f32 size);
void ui_max_size(UI_BoxBuilder *builder, AXIS axis, f32 size);
void ui_margin(UI_BoxBuilder *builder, AXIS axis, f32 before, f32 after);
void ui_padd(UI_BoxBuilder *builder, AXIS axis, f32 before, f32 after);
void ui_axis(UI_BoxBuilder *builder, AXIS axis);
void ui_gap(UI_BoxBuilder *builder, f32 gap);
void ui_perp_align(UI_BoxBuilder *builder, f32 align);
void ui_overflow(UI_BoxBuilder *builder, AXIS axis, UI_BoxOverflow overflow);

vec2 ui_box_measure(UI_Box *box, UI_BoxConstraints constraints);
void ui_box_layout(UI_Box *box, rect_f32 rect);
void ui_box_relayout(UI_Box *box);
UI_Box *ui_box_find_deepest(UI_Box *box, vec2 point);

#endif
