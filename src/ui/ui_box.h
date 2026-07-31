#ifndef UI_BOX_H
#define UI_BOX_H

#include "base.h"
#include "color.h"
#include "ui_id.h"

enum
{
	UI_BOX_MAX_DEPTH = 64,
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

typedef enum
{
	UI_BOX_POSITION_FLOW,
	UI_BOX_POSITION_ABSOLUTE,
}
UI_BoxPositionKind;

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
	UI_BoxPositionKind kind;
	f32 value;
}
UI_BoxPosition;

typedef struct
{
	UI_BoxSize size[2];
	UI_BoxPosition position[2];
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

typedef u32 UI_BoxDrawFlags;
enum
{
	UI_BOX_DRAW_BACKGROUND   = 1 << 0,
	UI_BOX_DRAW_BORDER       = 1 << 1,
	UI_BOX_DRAW_BACKDROP     = 1 << 2,
	UI_BOX_DRAW_INSET_SHADOW = 1 << 3,
};

typedef struct
{
	UI_BoxDrawFlags flags;
	Color_SRGBA background;
	Color_SRGBA border;
	f32 border_width;
	f32 roundness;
	f32 edge_softness;
	f32 inset_shadow;
	f32 emission;
	i32 z;
}
UI_BoxPaintDesc;

typedef struct
{
	vec2 min;
	vec2 max;
}
UI_BoxConstraints;

typedef struct UI_Box UI_Box;
typedef struct UI_BoxState UI_BoxState;
typedef struct UI_BoxBuilder UI_BoxBuilder;
typedef struct UI_Context UI_Context;
typedef void UI_BoxVirtualBuildItem(UI_Context *ui, u32 item_index, void *user);
typedef vec2 UI_BoxMeasure(UI_Box *box, UI_BoxConstraints constraints);
typedef vec2 UI_BoxMeasureChildren(UI_Box *box, UI_BoxConstraints constraints);
typedef void UI_BoxPrepareLayout(UI_Box *box);
// Runs after the box and all of its children have current rectangles.
typedef void UI_BoxFinishLayout(UI_Box *box);
typedef void UI_BoxPaint(UI_Box *box);

typedef struct
{
	UI_BoxMeasure *measure;
	UI_BoxMeasureChildren *measure_children;
	UI_BoxPrepareLayout *prepare_layout;
	UI_BoxFinishLayout *finish_layout;
	UI_BoxPaint *paint;
}
UI_BoxHooks;

typedef struct
{
	u32 item_count;
	void *user;
	UI_BoxVirtualBuildItem *build_item;
}
UI_BoxVirtualListDesc;

// Context-owned state for a keyed box. Geometry describes the most recently
// completed layout until the current box commits its new geometry.
struct UI_BoxState
{
	UI_BoxState *hash_next;
	UI_Id id;
	u64 last_touched_frame;
	u64 last_layout_frame;
	u64 layout_generation;
	rect_f32 rect;
	rect_f32 hit_rect;
	rect_f32 viewport;
	vec2 content_size;
	vec2 scroll_min;
	vec2 scroll_max;
	vec2 view_offset;
	vec2 view_target;
	f32 hot_t;
	f32 active_t;
	f32 focus_t;
};

struct UI_Box
{
	UI_Id           id;
	UI_Key         key;
	String        name;
	UI_BoxState *state;
	// The state contains geometry from the immediately preceding compatible layout.
	b32 has_previous;
	UI_BoxDesc desc;
	UI_BoxPaintDesc paint;
	UI_Box *parent;
	UI_Box *first;
	UI_Box *last;
	UI_Box *next;
	UI_Box *prev;
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
	const UI_BoxHooks *ops;
	void *content;
	void *user;
};

struct UI_BoxBuilder
{
	Arena *arena;
	UI_Context *ui;
	UI_Box *root;
	UI_Box *parent;
	UI_Id parent_id_stack[UI_BOX_MAX_DEPTH];
	UI_Id id;
	UI_Id id_stack[UI_BOX_MAX_DEPTH];
	UI_BoxDesc desc;
	UI_BoxDesc desc_stack[UI_BOX_MAX_DEPTH];
	UI_BoxPaintDesc paint;
	UI_BoxPaintDesc paint_stack[UI_BOX_MAX_DEPTH];
	u32 parent_count;
	u32 id_count;
	u32 desc_count;
};

UI_BoxSize ui_wrap(void);
UI_BoxSize ui_fixed(f32 value);
UI_BoxSize ui_grow(f32 grow);
UI_BoxSize ui_flex(f32 grow, f32 shrink);
UI_BoxDesc ui_defaults(void);
UI_BoxPaintDesc ui_default_paint(void);

UI_Box *ui_build_begin(UI_Context *ui, UI_Key root_key, String root_name, UI_BoxDesc root_desc);
UI_Box *ui_build_end(UI_Context *ui);
UI_Box *ui_box_make(UI_Context *ui, UI_Key key, String name);

void ui_box_end(UI_Context *ui);
UI_Box *ui_box_begin(UI_Context *ui, UI_Key key, String name);

UI_Box *ui_box_make_virtual_list(UI_Context *ui, UI_Key key, String name, UI_BoxVirtualListDesc list);
UI_Box *ui_box_make_desc(UI_Context *ui, UI_Key key, String name, UI_BoxDesc desc);
UI_Box *ui_box_begin_desc(UI_Context *ui, UI_Key key, String name, UI_BoxDesc desc);
UI_Box *ui_box_make_virtual_list_desc(UI_Context *ui, UI_Key key, String name, UI_BoxDesc desc, UI_BoxVirtualListDesc list);

void ui_box_push_id(UI_Context *ui, UI_Key key);
void ui_box_pop_id(UI_Context *ui);

void ui_push(UI_Context *ui);
void ui_pop(UI_Context *ui);
void ui_size(UI_Context *ui, AXIS axis, UI_BoxSize size);
void ui_position(UI_Context *ui, AXIS axis, f32 position);
void ui_rect(UI_Context *ui, rect_f32 rect);
void ui_min_size(UI_Context *ui, AXIS axis, f32 size);
void ui_max_size(UI_Context *ui, AXIS axis, f32 size);
void ui_margin(UI_Context *ui, AXIS axis, f32 before, f32 after);
void ui_padd(UI_Context *ui, AXIS axis, f32 before, f32 after);
void ui_axis(UI_Context *ui, AXIS axis);
void ui_gap(UI_Context *ui, f32 gap);
void ui_perp_align(UI_Context *ui, f32 align);
void ui_overflow(UI_Context *ui, AXIS axis, UI_BoxOverflow overflow);
void ui_background(UI_Context *ui, Color_SRGBA color);
void ui_border(UI_Context *ui, Color_SRGBA color, f32 width);
void ui_roundness(UI_Context *ui, f32 roundness);
void ui_edge_softness(UI_Context *ui, f32 edge_softness);
void ui_inset_shadow(UI_Context *ui, f32 strength);
void ui_emission(UI_Context *ui, f32 emission);
void ui_paint_z(UI_Context *ui, i32 z);

// Low-level builder API used by the box implementation and its focused tests.
UI_Box *ui_box_builder_begin(UI_BoxBuilder *builder, Arena *arena, UI_Context *ui, UI_Key root_key, String root_name, UI_BoxDesc root_desc);
UI_Box *ui_box_builder_end(UI_BoxBuilder *builder);
UI_Box *ui_builder_box_make(UI_BoxBuilder *builder, UI_Key key, String name);
UI_Box *ui_builder_box_begin(UI_BoxBuilder *builder, UI_Key key, String name);
UI_Box *ui_builder_box_make_virtual_list(UI_BoxBuilder *builder, UI_Key key, String name, UI_BoxVirtualListDesc list);
UI_Box *ui_builder_box_make_desc(UI_BoxBuilder *builder, UI_Key key, String name, UI_BoxDesc desc);
UI_Box *ui_builder_box_begin_desc(UI_BoxBuilder *builder, UI_Key key, String name, UI_BoxDesc desc);
UI_Box *ui_builder_box_make_virtual_list_desc(UI_BoxBuilder *builder, UI_Key key, String name, UI_BoxDesc desc, UI_BoxVirtualListDesc list);
void ui_builder_box_end(UI_BoxBuilder *builder);
void ui_builder_push_id(UI_BoxBuilder *builder, UI_Key key);
void ui_builder_pop_id(UI_BoxBuilder *builder);
void ui_builder_push(UI_BoxBuilder *builder);
void ui_builder_pop(UI_BoxBuilder *builder);
void ui_builder_size(UI_BoxBuilder *builder, AXIS axis, UI_BoxSize size);
void ui_builder_position(UI_BoxBuilder *builder, AXIS axis, f32 position);
void ui_builder_rect(UI_BoxBuilder *builder, rect_f32 rect);
void ui_builder_min_size(UI_BoxBuilder *builder, AXIS axis, f32 size);
void ui_builder_max_size(UI_BoxBuilder *builder, AXIS axis, f32 size);
void ui_builder_margin(UI_BoxBuilder *builder, AXIS axis, f32 before, f32 after);
void ui_builder_padd(UI_BoxBuilder *builder, AXIS axis, f32 before, f32 after);
void ui_builder_axis(UI_BoxBuilder *builder, AXIS axis);
void ui_builder_gap(UI_BoxBuilder *builder, f32 gap);
void ui_builder_perp_align(UI_BoxBuilder *builder, f32 align);
void ui_builder_overflow(UI_BoxBuilder *builder, AXIS axis, UI_BoxOverflow overflow);
void ui_builder_background(UI_BoxBuilder *builder, Color_SRGBA color);
void ui_builder_border(UI_BoxBuilder *builder, Color_SRGBA color, f32 width);
void ui_builder_roundness(UI_BoxBuilder *builder, f32 roundness);
void ui_builder_edge_softness(UI_BoxBuilder *builder, f32 edge_softness);
void ui_builder_inset_shadow(UI_BoxBuilder *builder, f32 strength);
void ui_builder_emission(UI_BoxBuilder *builder, f32 emission);
void ui_builder_paint_z(UI_BoxBuilder *builder, i32 z);

vec2 ui_box_measure(UI_Box *box, UI_BoxConstraints constraints);
void ui_box_layout(UI_Box *box, rect_f32 rect);
void ui_box_relayout(UI_Box *box);
UI_Box *ui_box_find_deepest(UI_Box *box, vec2 point);

#endif
