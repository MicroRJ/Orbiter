#ifndef ORBITER_APP_ACTIONS_H
#define ORBITER_APP_ACTIONS_H

#include "base.h"
#include "os_graphical.h"

typedef enum
{
	APP_ACTION_NONE,

	// Window actions
	APP_ACTION_TOGGLE_LIBRARY_OVERLAY,
	APP_ACTION_SPLIT_PANEL,
	APP_ACTION_CLOSE_PANEL,
	APP_ACTION_OPEN_VIEW,
	APP_ACTION_TOGGLE_FULLSCREEN,
	APP_ACTION_TOGGLE_PPU_FULLSCREEN,
	APP_ACTION_EXIT_PPU_FULLSCREEN,
	APP_ACTION_TAKE_APP_SCREENSHOT,
	APP_ACTION_TOGGLE_APP_CAPTURE,
	APP_ACTION_TOGGLE_CRT,
	APP_ACTION_TOGGLE_UI_DEBUG_BOUNDS,
	APP_ACTION_ADJUST_UI_FONT_SIZE,
	APP_ACTION_RESET_UI_FONT_SIZE,

	// Application actions
	APP_ACTION_OPEN_ROM,
	APP_ACTION_OPEN_LIBRARY_GAME,
	APP_ACTION_RESET,
	APP_ACTION_SAVE_STATE,
	APP_ACTION_RESTORE_STATE,
	APP_ACTION_DUMP_PROGRAM,
	APP_ACTION_TOGGLE_RUNNING,
	APP_ACTION_STEP,
	APP_ACTION_SCRUB,
	APP_ACTION_TAKE_PPU_SCREENSHOT,
	APP_ACTION_TOGGLE_PPU_CAPTURE,
	APP_ACTION_ADJUST_VOLUME,
	APP_ACTION_MUTE,
}
App_ActionKind;

typedef struct
{
	App_ActionKind kind;
	union
	{
		struct { AXIS axis; } split_panel;
		struct { u32 index; } open_view;
		struct { u32 index; } open_library_game;
		struct { i32 direction; } scrub;
		struct { i32 pixels; } ui_font;
		struct { f32 delta; } volume;
	};
}
App_Action;

typedef enum
{
	APP_KEY_CHORD_ON_PRESS,
	APP_KEY_CHORD_ON_RELEASE,
	APP_KEY_CHORD_WHILE_DOWN,
}
App_KeyChordActivation;

typedef struct
{
	App_KeyChordActivation activation;
	OS_Key key;
	u32 modifiers;
}
App_KeyChord;

typedef struct
{
	App_Action action;
	App_KeyChord key_chord;
	b32 allow_repeat;
}
App_KeyBinding;

typedef struct
{
	const App_KeyBinding *bindings;
	u32 count;
}
App_KeyMap;

typedef u32 App_GameInput;
enum
{
	APP_GAME_INPUT_UP     = 1 << 0,
	APP_GAME_INPUT_DOWN   = 1 << 1,
	APP_GAME_INPUT_LEFT   = 1 << 2,
	APP_GAME_INPUT_RIGHT  = 1 << 3,
	APP_GAME_INPUT_A      = 1 << 4,
	APP_GAME_INPUT_B      = 1 << 5,
	APP_GAME_INPUT_START  = 1 << 6,
	APP_GAME_INPUT_SELECT = 1 << 7,
};

typedef enum
{
	APP_TRANSPORT_PAUSED = 0,
	APP_TRANSPORT_RUNNING,
	APP_TRANSPORT_SCRUBBING,
}
App_TransportState;

typedef struct
{
	App_TransportState state;
	App_TransportState return_state;
	i32 direction;
}
App_Transport;

#endif
