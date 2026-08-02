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
	UI_BOX_OVERFLOW_VISIBLE,
	UI_BOX_OVERFLOW_CLIP,
}
UI_BoxOverflow;

typedef enum
{
	// The parent layout chooses the position. Linear layouts require this.
	UI_BOX_POSITION_AUTO,
	// Exact parent-viewport-relative border position, interpreted by frame
	// layouts. Margins do not alter a positioned axis.
	UI_BOX_POSITION_PARENT,
	// Normalized alignment within the parent's viewport, interpreted by frame
	// layouts.
	UI_BOX_POSITION_ALIGN,
}
UI_BoxPositionKind;

typedef struct UI_LayoutHooks UI_LayoutHooks;

typedef enum
{
	UI_BOX_SIZE_CONTENT,
	UI_BOX_SIZE_PIXELS,
	UI_BOX_SIZE_FILL,
}
UI_BoxSizeKind;

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
	UI_BoxOverflow overflow[2];
	const UI_LayoutHooks *layout;
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
typedef struct UI_Builder UI_Builder;
typedef struct UI_Context UI_Context;
typedef vec2 UI_BoxMeasure(UI_Box *box, UI_BoxConstraints constraints);
typedef vec2 UI_BoxMeasureChildren(UI_Box *box, UI_BoxConstraints constraints);
typedef void UI_BoxPrepareLayout(UI_Box *box);
// Runs after the box rectangle and viewport are established. A custom layout
// owns child arrangement; the core still finishes and commits the box state.
typedef void UI_BoxLayoutChildren(UI_Box *box, rect_f32 clip);
// Runs after the box and all of its children have current rectangles.
typedef void UI_BoxFinishLayout(UI_Box *box);
typedef void UI_BoxPaint(UI_Box *box);

typedef struct
{
	UI_BoxMeasure *measure;
	UI_BoxPrepareLayout *prepare_layout;
	UI_BoxFinishLayout *finish_layout;
	UI_BoxPaint *paint;
}
UI_BoxHooks;

struct UI_LayoutHooks
{
	UI_BoxMeasureChildren *measure_children;
	UI_BoxLayoutChildren *layout_children;
};

extern const UI_LayoutHooks ui_layout_linear;
extern const UI_LayoutHooks ui_layout_frame;

// These are exposed so specialized layouts can reuse one half of the linear
// strategy without coupling element behavior to child arrangement.
vec2 ui_linear_measure_children(UI_Box *box, UI_BoxConstraints constraints);
void ui_linear_layout_children(UI_Box *box, rect_f32 clip);

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
	// Widget-owned persistent values. The core box layout does not interpret
	// these; widgets such as scroll boxes may use them across frames.
	vec2 view_offset;
	vec2 view_target;
	f32 hot_t;
	f32 active_t;
	f32 focus_t;
};

struct UI_Box
{
	UI_Context     *ui;
	UI_BoxState *state;
	UI_Id           id;
	UI_Key         key;
	Str           name;

	// The state contains geometry from the immediately preceding compatible layout.
	b32      has_previous;
	b32    hit_passthrough;

	UI_BoxDesc       desc;
	UI_BoxPaintDesc paint;
	UI_Box        *parent;
	UI_Box         *first;
	UI_Box          *last;
	UI_Box          *next;
	UI_Box          *prev;
	u32       child_count;
	//
	vec2   intrinsic_size;
	vec2    measured_size;
	vec2    arranged_size;
	//
	rect_f32           rect;
	rect_f32      clip_rect;
	rect_f32       viewport;
	rect_f32 content_bounds;
	vec2       content_size;

	const UI_BoxHooks *hooks;
	void            *content;
	void               *user;
};

struct UI_Builder
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
	i32 box_z_stack[UI_BOX_MAX_DEPTH];
	u32 parent_count;
	u32 id_count;
	u32 desc_count;
	u32 box_z_count;
};

UI_BoxSize ui_wrap(void);
UI_BoxSize ui_fixed(f32 value);
UI_BoxSize ui_grow(f32 grow);
UI_BoxSize ui_flex(f32 grow, f32 shrink);
static inline UI_BoxSize ui_fill() {
	return ui_grow(1.f);
}
UI_BoxDesc ui_defaults(void);
UI_BoxPaintDesc ui_default_paint(void);

// A high-level UI scene accepts a linear or frame content root and adds its
// structural overlay frame. Stateful custom roots use ui_box_builder_begin.
UI_Box *ui_build_begin(UI_Context *ui, UI_Key root_key, Str root_name, UI_BoxDesc root_desc);
UI_Box *ui_build_end(UI_Context *ui);
UI_Box *ui_box_make(UI_Context *ui, UI_Key key, Str name);

void ui_box_end(UI_Context *ui);
UI_Box *ui_box_begin(UI_Context *ui, UI_Key key, Str name);

UI_Box *ui_box_make_desc(UI_Context *ui, UI_Key key, Str name, UI_BoxDesc desc);
UI_Box *ui_box_begin_desc(UI_Context *ui, UI_Key key, Str name, UI_BoxDesc desc);

void ui_box_push_id(UI_Context *ui, UI_Key key);
void ui_box_pop_id(UI_Context *ui);

void ui_push_box_z(UI_Context *ui, i32 z);
void ui_pop_box_z(UI_Context *ui);

void ui_push(UI_Context *ui);
void ui_pop(UI_Context *ui);
void ui_clean(UI_Context *ui);
void ui_size(UI_Context *ui, AXIS axis, UI_BoxSize size);
void ui_layout(UI_Context *ui, const UI_LayoutHooks *layout);
void ui_position(UI_Context *ui, AXIS axis, f32 position);
void ui_align(UI_Context *ui, AXIS axis, f32 alignment);
void ui_rect(UI_Context *ui, rect_f32 rect);
void ui_min_size(UI_Context *ui, AXIS axis, f32 size);
void ui_max_size(UI_Context *ui, AXIS axis, f32 size);
void ui_margin(UI_Context *ui, AXIS axis, f32 before, f32 after);
void ui_padd(UI_Context *ui, AXIS axis, f32 before, f32 after);
void ui_axis(UI_Context *ui, AXIS axis);
void ui_gap(UI_Context *ui, f32 gap);
void ui_overflow(UI_Context *ui, AXIS axis, UI_BoxOverflow overflow);
void ui_background(UI_Context *ui, Color_SRGBA color);
void ui_border(UI_Context *ui, Color_SRGBA color, f32 width);
void ui_backdrop(UI_Context *ui, f32 roundness);
void ui_roundness(UI_Context *ui, f32 roundness);
void ui_edge_softness(UI_Context *ui, f32 edge_softness);
void ui_inset_shadow(UI_Context *ui, f32 strength);
void ui_emission(UI_Context *ui, f32 emission);
void ui_paint_z(UI_Context *ui, i32 z);

// Low-level builder API used by the box implementation and its focused tests.
UI_Box *ui_box_builder_begin(UI_Builder *builder, Arena *arena, UI_Context *ui, UI_Key root_key, Str root_name, UI_BoxDesc root_desc);
UI_Box *ui_box_builder_end(UI_Builder *builder);
void ui_box_clear_children(UI_Box *box);
void ui_box_layout_clipped(UI_Box *box, rect_f32 rect, rect_f32 clip);
UI_Box *ui_builder_box_make(UI_Builder *builder, UI_Key key, Str name);
UI_Box *ui_builder_box_begin(UI_Builder *builder, UI_Key key, Str name);
UI_Box *ui_builder_box_make_desc(UI_Builder *builder, UI_Key key, Str name, UI_BoxDesc desc);
UI_Box *ui_builder_box_begin_desc(UI_Builder *builder, UI_Key key, Str name, UI_BoxDesc desc);
void ui_builder_box_end(UI_Builder *builder);
void ui_builder_push_id(UI_Builder *builder, UI_Key key);
void ui_builder_pop_id(UI_Builder *builder);
void ui_builder_push_box_z(UI_Builder *builder, i32 z);
void ui_builder_pop_box_z(UI_Builder *builder);
void ui_builder_push(UI_Builder *builder);
void ui_builder_pop(UI_Builder *builder);
void ui_builder_size(UI_Builder *builder, AXIS axis, UI_BoxSize size);
void ui_builder_layout(UI_Builder *builder, const UI_LayoutHooks *layout);
void ui_builder_position(UI_Builder *builder, AXIS axis, f32 position);
void ui_builder_align(UI_Builder *builder, AXIS axis, f32 alignment);
void ui_builder_rect(UI_Builder *builder, rect_f32 rect);
void ui_builder_min_size(UI_Builder *builder, AXIS axis, f32 size);
void ui_builder_max_size(UI_Builder *builder, AXIS axis, f32 size);
void ui_builder_margin(UI_Builder *builder, AXIS axis, f32 before, f32 after);
void ui_builder_padd(UI_Builder *builder, AXIS axis, f32 before, f32 after);
void ui_builder_axis(UI_Builder *builder, AXIS axis);
void ui_builder_gap(UI_Builder *builder, f32 gap);
void ui_builder_overflow(UI_Builder *builder, AXIS axis, UI_BoxOverflow overflow);
void ui_builder_background(UI_Builder *builder, Color_SRGBA color);
void ui_builder_border(UI_Builder *builder, Color_SRGBA color, f32 width);
void ui_builder_backdrop(UI_Builder *builder, f32 roundness);
void ui_builder_roundness(UI_Builder *builder, f32 roundness);
void ui_builder_edge_softness(UI_Builder *builder, f32 edge_softness);
void ui_builder_inset_shadow(UI_Builder *builder, f32 strength);
void ui_builder_emission(UI_Builder *builder, f32 emission);
void ui_builder_paint_z(UI_Builder *builder, i32 z);

vec2 ui_box_measure(UI_Box *box, UI_BoxConstraints constraints);
void ui_box_layout(UI_Box *box, rect_f32 rect);
UI_Box *ui_box_find_deepest(UI_Box *box, vec2 point);

#endif
