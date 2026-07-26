#ifndef ORBITER_UI_PLAYGROUND_LAYOUT_H
#define ORBITER_UI_PLAYGROUND_LAYOUT_H

#include "base.h"

enum
{
	UIP_MAX_DEPTH = 64,
	UIP_MAX_PENDING_CHILDREN = 4096,
};

#define UIP_INFINITY 1000000000.f

typedef enum
{
	UIP_SIZE_CONTENT,
	UIP_SIZE_PIXELS,
	UIP_SIZE_FILL,
}
UIP_SizeKind;

typedef enum
{
	UIP_OVERFLOW_VISIBLE,
	UIP_OVERFLOW_CLIP,
	UIP_OVERFLOW_SCROLL,
}
UIP_Overflow;

typedef struct
{
	UIP_SizeKind kind;
	f32 value;
	f32 grow;
	f32 shrink;
}
UIP_Size;

typedef struct
{
	UIP_Size size[2];
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
	UIP_Overflow overflow[2];
}
UIP_BoxDesc;

typedef struct
{
	vec2 min;
	vec2 max;
}
UIP_Constraints;

typedef struct UIP_Box UIP_Box;
typedef struct UIP_Builder UIP_Builder;
typedef struct UIP_Context UIP_Context;
typedef void UIP_VirtualBuildItem(UIP_Builder *builder, u32 item_index, void *user);
typedef vec2 UIP_BoxMeasure(UIP_Box *box, UIP_Constraints constraints);
typedef void UIP_BoxPaint(UIP_Box *box);

typedef struct
{
	UIP_BoxMeasure *measure;
	UIP_BoxPaint *paint;
}
UIP_BoxOps;

typedef struct
{
	u32 item_count;
	void *user;
	UIP_VirtualBuildItem *build_item;
}
UIP_VirtualListDesc;

struct UIP_Box
{
	String name;
	UIP_BoxDesc desc;
	UIP_Box **children;
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
		UIP_Box *sizing_item;
		UIP_VirtualBuildItem *build_item;
		void *user;
		u32 item_count;
		u32 first_item;
		u32 one_past_item;
		f32 item_extent;
	}
	virtual_list;
	u32 virtual_index;
	UIP_Context *ui;
	const UIP_BoxOps *ops;
	void *content;
	void *user;
};

struct UIP_Builder
{
	Arena *arena;
	UIP_Context *ui;
	UIP_Box *root;
	UIP_Box *parent;
	UIP_Box *parent_stack[UIP_MAX_DEPTH];
	UIP_Box *child_stack[UIP_MAX_PENDING_CHILDREN];
	u32 parent_count;
	u32 child_count;
};

UIP_Size uip_content(void);
UIP_Size uip_pixels(f32 value);
UIP_Size uip_fill(f32 grow);
UIP_Size uip_flex(f32 grow, f32 shrink);
UIP_BoxDesc uip_box_desc(void);

UIP_Box *uip_builder_begin(UIP_Builder *builder, Arena *arena, UIP_Context *ui, String root_name, UIP_BoxDesc root_desc);
UIP_Box *uip_make_box(UIP_Builder *builder, String name, UIP_BoxDesc desc);
UIP_Box *uip_begin_box(UIP_Builder *builder, String name, UIP_BoxDesc desc);
UIP_Box *uip_make_virtual_list(UIP_Builder *builder, String name, UIP_BoxDesc desc, UIP_VirtualListDesc list);
void uip_end_box(UIP_Builder *builder);
UIP_Box *uip_builder_end(UIP_Builder *builder);

vec2 uip_measure(UIP_Box *box, UIP_Constraints constraints);
void uip_layout(UIP_Box *box, rect_f32 rect);
void uip_relayout(UIP_Box *box);
UIP_Box *uip_find_deepest(UIP_Box *box, vec2 point);

#endif
