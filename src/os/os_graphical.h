#ifndef OS_GRAPHICAL_H
#define OS_GRAPHICAL_H

#include "base.h"

typedef enum
{
	OS_Key_Null,
	OS_Key_Esc,
	OS_Key_F1, OS_Key_F2, OS_Key_F3, OS_Key_F4, OS_Key_F5, OS_Key_F6, OS_Key_F7, OS_Key_F8, OS_Key_F9,
	OS_Key_F10, OS_Key_F11, OS_Key_F12, OS_Key_F13, OS_Key_F14, OS_Key_F15, OS_Key_F16, OS_Key_F17, OS_Key_F18, OS_Key_F19,
	OS_Key_F20, OS_Key_F21, OS_Key_F22, OS_Key_F23, OS_Key_F24,
	OS_Key_0, OS_Key_1, OS_Key_2, OS_Key_3, OS_Key_4, OS_Key_5, OS_Key_6, OS_Key_7, OS_Key_8, OS_Key_9,
	OS_Key_A, OS_Key_B, OS_Key_C, OS_Key_D, OS_Key_E, OS_Key_F, OS_Key_G, OS_Key_H, OS_Key_I, OS_Key_J, OS_Key_K, OS_Key_L, OS_Key_M,
	OS_Key_N, OS_Key_O, OS_Key_P, OS_Key_Q, OS_Key_R, OS_Key_S, OS_Key_T, OS_Key_U, OS_Key_V, OS_Key_W, OS_Key_X, OS_Key_Y, OS_Key_Z,
	OS_Key_NumPad0, OS_Key_NumPad1, OS_Key_NumPad2, OS_Key_NumPad3, OS_Key_NumPad4,
	OS_Key_NumPad5, OS_Key_NumPad6, OS_Key_NumPad7, OS_Key_NumPad8, OS_Key_NumPad9,
	OS_Key_NumPadMinus, OS_Key_NumPadPlus, OS_Key_NumPadLock, OS_Key_NumPadSlash, OS_Key_NumPadStar, OS_Key_NumPadPeriod,
	OS_Key_MouseLeft, OS_Key_MouseRight, OS_Key_MouseMiddle,
	OS_Key_LeftShift, OS_Key_RightShift, OS_Key_LeftControl, OS_Key_RightControl, OS_Key_LeftAlt, OS_Key_RightAlt,
	OS_Key_LeftBracket, OS_Key_RightBracket, OS_Key_Semicolon, OS_Key_Quote, OS_Key_Comma, OS_Key_Tab,
	OS_Key_Space, OS_Key_Enter, OS_Key_Backspace, OS_Key_Tick, OS_Key_Minus, OS_Key_Equal, OS_Key_BackSlash,
	OS_Key_CapsLock, OS_Key_Period, OS_Key_Slash, OS_Key_Menu, OS_Key_ScrollLock, OS_Key_Pause,
	OS_Key_Insert, OS_Key_Home, OS_Key_PageUp, OS_Key_Delete, OS_Key_End, OS_Key_PageDown,
	OS_Key_Up, OS_Key_Left, OS_Key_Down, OS_Key_Right,
	OS_Key_COUNT,
}
OS_Key;

typedef enum
{
	OS_KEY_DOWN     = 1 << 0,
	OS_KEY_PRESSED  = 1 << 1,
	OS_KEY_RELEASED = 1 << 2,
	OS_KEY_REPEAT   = 1 << 3,
}
OS_KeyStateFlags;

typedef u8 OS_KeyState;

typedef enum
{
	OS_MODIFIER_NONE    = 0,
	OS_MODIFIER_SHIFT   = 1 << 0,
	OS_MODIFIER_CONTROL = 1 << 1,
	OS_MODIFIER_ALT     = 1 << 2,
}
OS_ModifierFlags;

typedef enum
{
	OS_EVENT_NONE,
	OS_EVENT_WINDOW_CLOSE,
	OS_EVENT_WINDOW_RESIZE,
	OS_EVENT_WINDOW_FOCUS_GAINED,
	OS_EVENT_WINDOW_FOCUS_LOST,
	OS_EVENT_KEY_PRESS,
	OS_EVENT_KEY_RELEASE,
	OS_EVENT_TEXT,
	OS_EVENT_MOUSE_MOVE,
	OS_EVENT_MOUSE_WHEEL,
	OS_EVENT_FILE_DROP,
}
OS_EventType;

typedef struct
{
	OS_EventType type;
	OS_Key key;
	u32 modifiers;
	b32 repeat;
	vec2i mouse_position;
	union
	{
		Rune rune;
		vec2i mouse;
		struct { f32 wheel_x, wheel_y; };
		vec2i size;
		Str path;
	};
}
OS_Event;

enum
{
	OS_WINDOW_OPEN     = 1 << 0,
	OS_WINDOW_RESIZING = 1 << 1,
	OS_WINDOW_FOCUSED  = 1 << 2,
	OS_WINDOW_MINIMIZED = 1 << 3,
};

typedef struct OS_Window OS_Window;
struct OS_Window
{
	u32 status;
	vec2i size;
	vec2i mouse_position;
	vec2i mouse_wheel;
	OS_KeyState keys[OS_Key_COUNT];
	OS_Event *events;
	u32 event_count;
	u32 event_capacity;
};

// Events and any Str data they reference remain valid until the next
// os_graphical_poll call. Persistent key and mouse state remains on the window.

typedef struct
{
	b32 enabled;
	b32 dark;
	u32 background_rgb;
	u32 text_rgb;
	u32 border_rgb;
}
OS_WindowTitleBarStyle;

typedef struct
{
	const char *title;
	vec2i size;
	u32 flags;
	OS_WindowTitleBarStyle title_bar;
}
OS_WindowDesc;

enum
{
	OS_WINDOW_CREATE_HIDDEN = 1 << 0,
};

typedef enum
{
	OS_CURSOR_POINTER,
	OS_CURSOR_IBEAM,
	OS_CURSOR_RESIZE_HORIZONTAL,
	OS_CURSOR_RESIZE_VERTICAL,
	OS_CURSOR_RESIZE_DIAGONAL_DOWN,
	OS_CURSOR_RESIZE_DIAGONAL_UP,
	OS_CURSOR_RESIZE_ALL,
	OS_CURSOR_HAND,
	OS_CURSOR_DISABLED,
	OS_CURSOR_COUNT,
}
OS_Cursor;

b32 os_graphical_init(void);
void os_graphical_shutdown(void);
OS_Window *os_window_create(OS_WindowDesc desc);
void os_window_destroy(OS_Window *window);
void os_graphical_poll(void);
b32 os_window_is_open(const OS_Window *window);
u32 os_window_event_count(const OS_Window *window);
const OS_Event *os_window_event(const OS_Window *window, u32 index);
void os_window_set_cursor(OS_Window *window, OS_Cursor cursor);
b32 os_window_set_title_bar_style(OS_Window *window, OS_WindowTitleBarStyle style);
void os_window_set_fullscreen(OS_Window *window, b32 fullscreen);
b32 os_window_is_fullscreen(const OS_Window *window);
void *os_window_native_handle(OS_Window *window);

#endif
