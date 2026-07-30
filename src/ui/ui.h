#ifndef UI_H
#define UI_H

#include "graphics.h"
#include "os_graphical.h"
#include "ui_id.h"
#include "text.h"

typedef struct
{
	b32  hovered;
	b32  pressed;
	b32  released;
	b32  held;
	vec2 drag_delta;
}
UI_Response;

typedef struct
{
	Font_Handle font;
	// Ascender-to-descender line height in pixels.
	i32         size;
	Color_SRGBA color;
	vec2        align;
}
UI_TextStyle;

enum
{
	UI_CODE_FONT_SIZE_DEFAULT = 24,
	UI_CODE_FONT_SIZE_MIN = 12,
	UI_CODE_FONT_SIZE_MAX = 48,
};

typedef struct
{
	Color_SRGBA void_background;
	Color_SRGBA background;
	Color_SRGBA panel;
	Color_SRGBA overlay;
	Color_SRGBA raised;
	Color_SRGBA divider;
	Color_SRGBA text;
	Color_SRGBA text_muted;
	Color_SRGBA teal;
	Color_SRGBA cyan;
	Color_SRGBA blue;
	Color_SRGBA violet;
	Color_SRGBA amber;
	Color_SRGBA error;
	f32 emission_faint;
	f32 emission_low;
	f32 emission_medium;
	f32 emission_high;
}
UI_Palette;

typedef struct
{
	UI_Palette palette;
	Color_SRGBA background;
	Color_SRGBA panel_background;
	Color_SRGBA panel_outline;
	Color_SRGBA panel_outline_focused;
	Color_SRGBA splitter;
	Color_SRGBA splitter_hot;
	Color_SRGBA slider_track;
	Color_SRGBA slider_thumb;
	Color_SRGBA text_neutral;
	Color_SRGBA text_subtle;
	Color_SRGBA text_vibrant;
	Color_SRGBA program_bridge;
	Color_SRGBA program_counter;

	UI_TextStyle code;
}
UI_Theme;

typedef struct
{
	rect_f32     rect;
	GFX_Texture *texture;
	rect_i32     region;
	GFX_Sampler  sampler;
	GFX_Blender  blender;
	GFX_Shader   shader;
}
UI_ImageParams;

typedef enum
{
	UI_LAYER_CONTENT,
	UI_LAYER_HEADER,
	UI_LAYER_OVERLAY,
	UI_LAYER_COUNT,
}
UI_LayerKind;

typedef enum
{
	UI_DRAW_COMMAND_RECT,
	UI_DRAW_COMMAND_IMAGE,
	UI_DRAW_COMMAND_TEXT,
	UI_DRAW_COMMAND_INSET_SHADOW,
	UI_DRAW_COMMAND_BACKDROP,
}
UI_DrawCommandKind;

typedef struct UI_DrawCommand UI_DrawCommand;
struct UI_DrawCommand
{
	UI_DrawCommand *next;
	UI_DrawCommandKind kind;
	f32 emission;
	b32 has_clip;
	rect_f32 clip;
	union
	{
		struct
		{
			rect_f32 rect;
			Color_SRGBA color;
			f32 roundness;
			f32 edge_softness;
		}
		rect;
		struct
		{
			UI_ImageParams params;
		}
		image;
		struct
		{
			Text_DrawRun run;
			vec2 position;
			Color_SRGBA color;
		}
		text;
		struct
		{
			rect_f32 rect;
			f32 strength;
		}
		inset_shadow;
		struct
		{
			rect_f32 rect;
			f32 corner_radius;
			f32 distortion;
			f32 distortion_width;
			f32 saturation;
			Color_SRGBA tint;
			f32 grain;
			f32 highlight;
			f32 shadow;
		}
		backdrop;
	};
};

typedef struct
{
	UI_DrawCommand *first;
	UI_DrawCommand *last;
	u32 command_count;
	b32 has_backdrops;
	b32 has_emission;
}
UI_Layer;

typedef struct
{
	UI_Layer layers[UI_LAYER_COUNT];
}
UI_Frame;

typedef struct UI_Box UI_Box;
typedef struct UI_BoxState UI_BoxState;
typedef struct UI_BoxBuilder UI_BoxBuilder;
typedef struct UI_Context UI_Context;
struct UI_Context
{
	Arena      *owner;
	OS_Window *window;
	Text_Context *text;
	Arena      frame_arena;
	UI_Frame   frame;
	UI_Theme   theme;
	UI_BoxState **box_state_slots;
	UI_BoxState *free_box_states;
	u32        box_state_slot_count;
	UI_BoxBuilder *builder;
	u64        frame_index;
	u64        layout_generation;
	Seconds    previous_frame_time;
	f32        frame_elapsed;
	vec2i      previous_window_size;
	b32        mouse_wheel_consumed;
	UI_Id      hot;
	UI_Id      active;
	vec2       mouse;
	vec2       active_press_mouse;
	f32        active_start_value;
	UI_LayerKind layer;
	UI_LayerKind layer_stack[8];
	u32 layer_stack_count;
	rect_f32 clip_stack[16];
	u32 clip_stack_count;
	u32 unclipped_scope_count;
	f32 emission;
	f32 emission_stack[8];
	u32 emission_stack_count;
};

typedef enum
{
	UI_TABLE_COLUMN_CONTENT,
	UI_TABLE_COLUMN_FIXED,
	UI_TABLE_COLUMN_FLEX,
}
UI_TableColumnKind;

typedef struct
{
	UI_TableColumnKind kind;
	f32                value;
}
UI_TableColumnSpec;

typedef struct
{
	String       text;
	UI_TextStyle style;
}
UI_TableCell;

// Tables are transient current-frame layout data. They measure a small set of
// cells without introducing persistent nodes or a deferred UI-wide layout pass.
typedef struct
{
	UI_Context        *ui;
	rect_f32           rect;
	f32                row_height;
	f32                column_gap;
	vec2               cell_padding;
	u32                row_count;
	u32                column_count;
	UI_TableColumnSpec *columns;
	UI_TableCell       *cells;
	f32                *resolved_widths;
	b32                 is_laid_out;
}
UI_Table;

UI_Palette ui_default_palette(void);
UI_Theme ui_default_theme(Font_Handle code_font);
UI_Context *ui_create(Arena *owner, OS_Window *window, Text_Context *text, UI_Theme theme);

void ui_begin_frame(UI_Context *ui);
void ui_end_frame(UI_Context *ui);
const UI_Frame *ui_frame(const UI_Context *ui);
UI_BoxState *ui_box_state_get(UI_Context *ui, UI_Id id);
void ui_box_state_forget(UI_Context *ui, UI_Id id);
void ui_invalidate_layout(UI_Context *ui);
void ui_push_layer(UI_Context *ui, UI_LayerKind layer);
void ui_pop_layer(UI_Context *ui);

b32 ui_is_hot(UI_Context *ui, UI_Id id);
b32 ui_is_active(UI_Context *ui, UI_Id id);

UI_Response ui_interact(UI_Context *ui, UI_Id id, rect_f32 rect);
UI_Response ui_signal_from_box(UI_Box *box);
void ui_push_clip(UI_Context *ui, rect_f32 rect);
void ui_pop_clip(UI_Context *ui);
void ui_push_unclipped(UI_Context *ui);
void ui_pop_unclipped(UI_Context *ui);
void ui_push_emission(UI_Context *ui, f32 emission);
void ui_pop_emission(UI_Context *ui);
UI_DrawCommand *ui_draw_rect(UI_Context *ui, rect_f32 rect, Color_SRGBA color);
void ui_draw_rect_outline(UI_Context *ui, rect_f32 rect, f32 thickness, Color_SRGBA color);
void ui_draw_inset_shadow(UI_Context *ui, rect_f32 rect, f32 strength);
void ui_draw_backdrop(UI_Context *ui, rect_f32 rect);

void ui_draw_panel(UI_Context *ui, rect_f32 rect, b32 focused);
void ui_draw_splitter(UI_Context *ui, rect_f32 rect, UI_Id id);
void ui_draw_image(UI_Context *ui, UI_ImageParams params);
vec2 ui_measure_text(UI_Context *ui, UI_TextStyle style, String text);
vec2 ui_draw_text(UI_Context *ui, rect_f32 rect, UI_TextStyle style, String text);
UI_Response ui_scrollbar(UI_Context *ui, UI_Id id, rect_f32 track, f32 viewport_height, f32 *position, f32 content_height);

UI_TableColumnSpec ui_table_column_content(void);
UI_TableColumnSpec ui_table_column_fixed(f32 width);
UI_TableColumnSpec ui_table_column_flex(f32 weight);
UI_Table ui_table_begin(UI_Context *ui, Arena *arena, rect_f32 rect, u32 row_count, u32 column_count, f32 row_height);
void ui_table_set_column(UI_Table *table, u32 column, UI_TableColumnSpec spec);
void ui_table_set_text(UI_Table *table, u32 row, u32 column, UI_TextStyle style, String text);
void ui_table_layout(UI_Table *table);
rect_f32 ui_table_cell_rect(const UI_Table *table, u32 row, u32 column);
void ui_table_draw(UI_Table *table);

#endif
