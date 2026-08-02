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

typedef enum
{
	UI_FEEDBACK_NONE  = 0,
	UI_FEEDBACK_PRESS = 1 << 0,
}
UI_Feedback;

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

enum
{
	UI_Z_CONTENT = 0,
	UI_Z_HEADER = 100,
	UI_Z_OVERLAY = 1000,
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

typedef struct UI_Box UI_Box;
typedef struct UI_BoxState UI_BoxState;
typedef struct UI_Builder UI_Builder;
typedef struct UI_Context UI_Context;
struct UI_Context
{
	Arena         *owner;
	OS_Window     *window;
	Text_Context  *text;
	Draw_Context  *draw;
	Arena          frame_arena;
	UI_Theme       theme;
	UI_BoxState  **box_state_slots;
	UI_BoxState   *free_box_states;
	u32            box_state_slot_count;
	UI_Builder *builder;
	UI_Box        *root;
	UI_Box        *overlay_root;
	UI_Box        *tooltip_box;
	b32            tooltip_open;
	u64            frame_index;
	u64            layout_generation;
	Seconds        previous_frame_time;
	f32            frame_elapsed;
	vec2i          previous_window_size;
	b32            mouse_wheel_consumed;
	u32            feedback;
	UI_Id          hot;
	UI_Id          active;
	vec2           mouse;
	vec2           active_press_mouse;
	f32            active_start_value;
};

UI_Palette ui_default_palette(void);
UI_Theme ui_default_theme(Font_Handle code_font);
UI_Context *ui_create(Arena *owner, OS_Window *window, Text_Context *text, Draw_Context *draw, UI_Theme theme);

void ui_begin_frame(UI_Context *ui);
void ui_end_frame(UI_Context *ui);
UI_BoxState *ui_box_state_get(UI_Context *ui, UI_Id id);
void ui_box_state_forget(UI_Context *ui, UI_Id id);
void ui_invalidate_layout(UI_Context *ui);
void ui_push_z(UI_Context *ui, i32 z);
void ui_pop_z(UI_Context *ui);

b32 ui_is_hot(UI_Context *ui, UI_Id id);
b32 ui_is_active(UI_Context *ui, UI_Id id);
void ui_feedback_emit(UI_Context *ui, UI_Feedback feedback);
UI_Feedback ui_feedback_take(UI_Context *ui);

UI_Response ui_interact(UI_Context *ui, UI_Id id, rect_f32 rect);
UI_Response ui_signal_from_box(UI_Box *box);
void ui_push_clip(UI_Context *ui, rect_f32 rect);
void ui_pop_clip(UI_Context *ui);
void ui_push_unclipped(UI_Context *ui);
void ui_pop_unclipped(UI_Context *ui);
void ui_push_emission(UI_Context *ui, f32 emission);
void ui_pop_emission(UI_Context *ui);
Draw_Command *ui_draw_rect(UI_Context *ui, rect_f32 rect, Color_SRGBA color);
void ui_draw_rect_outline(UI_Context *ui, rect_f32 rect, f32 thickness, Color_SRGBA color);
void ui_draw_inset_shadow(UI_Context *ui, rect_f32 rect, f32 strength);
void ui_draw_backdrop(UI_Context *ui, rect_f32 rect, f32 roundness);

void ui_draw_image(UI_Context *ui, Draw_TextureParams params);
vec2 ui_measure_text(UI_Context *ui, UI_TextStyle style, Str text);
vec2 ui_draw_text(UI_Context *ui, rect_f32 rect, UI_TextStyle style, Str text);
UI_Response ui_scrollbar(UI_Context *ui, UI_Id id, rect_f32 track, f32 viewport_height, f32 *position, f32 content_height);

#endif
