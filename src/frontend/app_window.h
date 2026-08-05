#ifndef ORBITER_APP_WINDOW_H
#define ORBITER_APP_WINDOW_H

#include "actions.h"
#include "ui.h"

typedef struct App App;
typedef struct Tabula_Context Tabula_Context;
typedef struct Tabula_Table Tabula_Table;

typedef struct
{
	const char *title;
	UI_Theme theme;
}
App_WindowDesc;

typedef struct
{
	const App_Action *actions;
	u32 action_count;
	App_GameInput keyboard_input[2];
	b32 keyboard_captured;
	UI_Feedback feedback;
}
App_WindowOutput;

typedef struct App_Window App_Window;

App_Window *app_window_create(Arena *owner, App *app, App_WindowDesc desc);
void app_window_destroy(App_Window *window);
b32 app_window_is_open(const App_Window *window);
void app_window_set_library_visible(App_Window *window, b32 visible);
Tabula_Table *app_window_state_to_table(const App_Window *window, Tabula_Context *context);
b32 app_window_state_from_table(App_Window *window, const Tabula_Table *table);

App_WindowOutput app_window_begin_frame(App_Window *window, App_KeyMap key_map);
void app_window_emit_action(App_Window *window, App_Action action);
void app_window_render(App_Window *window);

#endif
