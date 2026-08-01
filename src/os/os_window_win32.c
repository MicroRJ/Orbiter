#include "os_win32_internal.h"
#include "os_graphical_internal.h"

#pragma comment(lib, "User32")
#pragma comment(lib, "Gdi32")
#pragma comment(lib, "Shell32")
#include <shellapi.h>

#define OS_WIN32_WINDOW_CLASS L"os-graphical-window"

typedef struct OS_Win32Window
{
	OS_Window base;
	struct OS_Win32Window *next;
	HWND      handle;
	OS_Cursor cursor;
	WINDOWPLACEMENT windowed_placement;
	DWORD windowed_style;
	b32 fullscreen;
	u16 pending_high_surrogate;
}
OS_Win32Window;

static OS_Win32Window *os_win32_first_window;

typedef HRESULT (WINAPI *OS_Win32DwmSetWindowAttribute)(HWND window, DWORD attribute, LPCVOID value, DWORD value_size);

static COLORREF os_win32_colorref(u32 rgb)
{
	return RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

static b32 os_win32_set_title_bar_style(HWND window, OS_WindowTitleBarStyle style)
{
	if (!style.enabled) return false;

	HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
	if (!dwm) return false;

	OS_Win32DwmSetWindowAttribute set_attribute = (OS_Win32DwmSetWindowAttribute) GetProcAddress(dwm, "DwmSetWindowAttribute");
	if (!set_attribute)
	{
		FreeLibrary(dwm);
		return false;
	}

	// These numeric attributes allow the application to build against older
	// Windows SDKs. Immersive dark mode is supported on recent Windows 10;
	// explicit border, caption, and text colors are supported on Windows 11.
	const DWORD use_immersive_dark_mode = 20;
	const DWORD use_immersive_dark_mode_legacy = 19;
	const DWORD border_color = 34;
	const DWORD caption_color = 35;
	const DWORD text_color = 36;
	BOOL dark = style.dark;
	HRESULT dark_result = set_attribute(window, use_immersive_dark_mode, &dark, sizeof(dark));
	if (FAILED(dark_result)) {
		dark_result = set_attribute(window, use_immersive_dark_mode_legacy, &dark, sizeof(dark));
	}

	COLORREF background = os_win32_colorref(style.background_rgb);
	COLORREF text = os_win32_colorref(style.text_rgb);
	COLORREF border = os_win32_colorref(style.border_rgb);
	HRESULT background_result = set_attribute(window, caption_color, &background, sizeof(background));
	HRESULT text_result = set_attribute(window, text_color, &text, sizeof(text));
	HRESULT border_result = set_attribute(window, border_color, &border, sizeof(border));
	FreeLibrary(dwm);

	SetWindowPos(window, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	return SUCCEEDED(dark_result) || SUCCEEDED(background_result) || SUCCEEDED(text_result) || SUCCEEDED(border_result);
}

static u32 os_win32_modifiers(const OS_Window *window)
{
	u32 modifiers = OS_MODIFIER_NONE;
	if ((window->keys[OS_Key_LeftShift] | window->keys[OS_Key_RightShift]) & OS_KEY_DOWN) modifiers |= OS_MODIFIER_SHIFT;
	if ((window->keys[OS_Key_LeftControl] | window->keys[OS_Key_RightControl]) & OS_KEY_DOWN) modifiers |= OS_MODIFIER_CONTROL;
	if ((window->keys[OS_Key_LeftAlt] | window->keys[OS_Key_RightAlt]) & OS_KEY_DOWN) modifiers |= OS_MODIFIER_ALT;
	return modifiers;
}

static OS_Event *os_win32_push_event(OS_Window *window, OS_EventType type)
{
	OS_Event *event = os_graphical_push_event(window, type);
	if (!event) return NULL;
	event->modifiers = os_win32_modifiers(window);
	event->mouse_position = window->mouse_position;
	return event;
}

static void os_win32_key_up(OS_Window *window, OS_Key key)
{
	if (key <= OS_Key_Null || key >= OS_Key_COUNT) return;
	if (!(window->keys[key] & OS_KEY_DOWN)) return;
	u32 modifiers = os_win32_modifiers(window);
	window->keys[key] |= OS_KEY_RELEASED;
	window->keys[key] &= ~OS_KEY_DOWN;
	OS_Event *event = os_win32_push_event(window, OS_EVENT_KEY_RELEASE);
	if (event)
	{
		event->key = key;
		event->modifiers = modifiers;
	}
}

static void os_win32_key_down(OS_Window *window, OS_Key key, b32 repeat)
{
	if (key <= OS_Key_Null || key >= OS_Key_COUNT) return;
	if (!(window->keys[key] & OS_KEY_DOWN))
	{
		window->keys[key] |= OS_KEY_PRESSED;
	}
	if (repeat) window->keys[key] |= OS_KEY_REPEAT;
	window->keys[key] |= OS_KEY_DOWN;
	OS_Event *event = os_win32_push_event(window, OS_EVENT_KEY_PRESS);
	if (event)
	{
		event->key = key;
		event->repeat = repeat;
		event->modifiers = os_win32_modifiers(window);
	}
}

static void os_win32_map_keys(void)
{
	for (u32 index = 0; index < 10; ++index)
	{
		os_win32.key_map['0' + index] = OS_Key_0 + index;
		os_win32.key_map[VK_NUMPAD0 + index] = OS_Key_NumPad0 + index;
	}
	for (u32 index = 0; index < 26; ++index)
	{
		os_win32.key_map['A' + index] = OS_Key_A + index;
	}
	for (u32 index = 0; index < 24; ++index)
	{
		os_win32.key_map[VK_F1 + index] = OS_Key_F1 + index;
	}

#define MAP(win32_key, os_key) os_win32.key_map[win32_key] = os_key
	MAP(VK_SPACE, OS_Key_Space);
	MAP(VK_OEM_3, OS_Key_Tick);
	MAP(VK_OEM_MINUS, OS_Key_Minus);
	MAP(VK_OEM_PLUS, OS_Key_Equal);
	MAP(VK_OEM_4, OS_Key_LeftBracket);
	MAP(VK_OEM_6, OS_Key_RightBracket);
	MAP(VK_OEM_1, OS_Key_Semicolon);
	MAP(VK_OEM_7, OS_Key_Quote);
	MAP(VK_OEM_COMMA, OS_Key_Comma);
	MAP(VK_OEM_PERIOD, OS_Key_Period);
	MAP(VK_OEM_2, OS_Key_Slash);
	MAP(VK_OEM_5, OS_Key_BackSlash);
	MAP(VK_TAB, OS_Key_Tab);
	MAP(VK_PAUSE, OS_Key_Pause);
	MAP(VK_ESCAPE, OS_Key_Esc);
	MAP(VK_UP, OS_Key_Up);
	MAP(VK_LEFT, OS_Key_Left);
	MAP(VK_DOWN, OS_Key_Down);
	MAP(VK_RIGHT, OS_Key_Right);
	MAP(VK_BACK, OS_Key_Backspace);
	MAP(VK_RETURN, OS_Key_Enter);
	MAP(VK_DELETE, OS_Key_Delete);
	MAP(VK_INSERT, OS_Key_Insert);
	MAP(VK_PRIOR, OS_Key_PageUp);
	MAP(VK_NEXT, OS_Key_PageDown);
	MAP(VK_HOME, OS_Key_Home);
	MAP(VK_END, OS_Key_End);
	MAP(VK_CAPITAL, OS_Key_CapsLock);
	MAP(VK_NUMLOCK, OS_Key_NumPadLock);
	MAP(VK_SCROLL, OS_Key_ScrollLock);
	MAP(VK_APPS, OS_Key_Menu);
	MAP(VK_CONTROL, OS_Key_LeftControl);
	MAP(VK_LCONTROL, OS_Key_LeftControl);
	MAP(VK_RCONTROL, OS_Key_RightControl);
	MAP(VK_SHIFT, OS_Key_LeftShift);
	MAP(VK_LSHIFT, OS_Key_LeftShift);
	MAP(VK_RSHIFT, OS_Key_RightShift);
	MAP(VK_MENU, OS_Key_LeftAlt);
	MAP(VK_LMENU, OS_Key_LeftAlt);
	MAP(VK_RMENU, OS_Key_RightAlt);
	MAP(VK_DIVIDE, OS_Key_NumPadSlash);
	MAP(VK_MULTIPLY, OS_Key_NumPadStar);
	MAP(VK_SUBTRACT, OS_Key_NumPadMinus);
	MAP(VK_ADD, OS_Key_NumPadPlus);
	MAP(VK_DECIMAL, OS_Key_NumPadPeriod);
#undef MAP
}

static OS_Key os_win32_key_from_message(WPARAM wparam, LPARAM lparam)
{
	if (wparam == VK_SHIFT)
	{
		UINT scan_code = (UINT)((lparam >> 16) & 0xff);
		return MapVirtualKeyW(scan_code, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT ? OS_Key_RightShift : OS_Key_LeftShift;
	}
	if (wparam == VK_CONTROL) return lparam & (1ll << 24) ? OS_Key_RightControl : OS_Key_LeftControl;
	if (wparam == VK_MENU) return lparam & (1ll << 24) ? OS_Key_RightAlt : OS_Key_LeftAlt;
	return wparam < ArrayCount(os_win32.key_map) ? os_win32.key_map[wparam] : OS_Key_Null;
}

static b32 os_win32_window_init(void)
{
	typedef BOOL SetProcessDpiAwarenessContextProc(HANDLE value);
	HMODULE user32 = GetModuleHandleW(L"user32.dll");
	SetProcessDpiAwarenessContextProc *set_dpi_awareness = user32 ? (SetProcessDpiAwarenessContextProc *)GetProcAddress(user32, "SetProcessDpiAwarenessContext") : NULL;
	if (set_dpi_awareness) set_dpi_awareness((HANDLE)-4);
	os_win32_map_keys();
	os_win32.cursors[OS_CURSOR_POINTER] = LoadCursorA(0, IDC_ARROW);
	os_win32.cursors[OS_CURSOR_IBEAM] = LoadCursorA(0, IDC_IBEAM);
	os_win32.cursors[OS_CURSOR_RESIZE_HORIZONTAL] = LoadCursorA(0, IDC_SIZEWE);
	os_win32.cursors[OS_CURSOR_RESIZE_VERTICAL] = LoadCursorA(0, IDC_SIZENS);
	os_win32.cursors[OS_CURSOR_RESIZE_DIAGONAL_DOWN] = LoadCursorA(0, IDC_SIZENWSE);
	os_win32.cursors[OS_CURSOR_RESIZE_DIAGONAL_UP] = LoadCursorA(0, IDC_SIZENESW);
	os_win32.cursors[OS_CURSOR_RESIZE_ALL] = LoadCursorA(0, IDC_SIZEALL);
	os_win32.cursors[OS_CURSOR_HAND] = LoadCursorA(0, IDC_HAND);
	os_win32.cursors[OS_CURSOR_DISABLED] = LoadCursorA(0, IDC_NO);

	WNDCLASSEXW window_class = {
		.cbSize = sizeof(window_class),
		.lpfnWndProc = os_win32_window_proc,
		.hInstance = GetModuleHandleW(0),
		.lpszClassName = OS_WIN32_WINDOW_CLASS,
		.hCursor = os_win32.cursors[OS_CURSOR_POINTER],
		.hIcon = LoadIconA(NULL, IDI_APPLICATION),
	};
	if (!RegisterClassExW(&window_class))
	{
		os_win32_report_last_error("RegisterClassExW");
		return false;
	}
	return true;
}

b32 os_graphical_init(void)
{
	return os_win32_window_init();
}

static void os_win32_window_shutdown(void)
{
	UnregisterClassW(OS_WIN32_WINDOW_CLASS, GetModuleHandleW(0));
}

void os_graphical_shutdown(void)
{
	Assert(!os_win32_first_window);
	os_win32_window_shutdown();
}

OS_Window *os_window_create(OS_WindowDesc desc)
{
	OS_Win32Window *window = calloc(1, sizeof(*window));
	if (!window) return 0;

	if (desc.size.x <= 0) desc.size.x = GetSystemMetrics(SM_CXSCREEN);
	if (desc.size.y <= 0) desc.size.y = GetSystemMetrics(SM_CYSCREEN);
	if (!desc.title) desc.title = "application";
	wchar_t title[256];
	MultiByteToWideChar(CP_UTF8, 0, desc.title, -1, title, _countof(title));

	window->handle = CreateWindowExW(0, OS_WIN32_WINDOW_CLASS, title,
		WS_OVERLAPPEDWINDOW | WS_THICKFRAME, CW_USEDEFAULT, CW_USEDEFAULT,
		desc.size.x, desc.size.y, 0, 0, GetModuleHandleW(0), window);
	if (!window->handle)
	{
		os_win32_report_last_error("CreateWindowExW");
		free(window);
		return 0;
	}
	os_win32_set_title_bar_style(window->handle, desc.title_bar);
	window->base.status = OS_WINDOW_OPEN;
	window->next = os_win32_first_window;
	os_win32_first_window = window;
	DragAcceptFiles(window->handle, TRUE);
	if (!(desc.flags & OS_WINDOW_CREATE_HIDDEN)) ShowWindow(window->handle, SW_SHOW);
	RECT client;
	GetClientRect(window->handle, &client);
	window->base.size = v2i(client.right - client.left, client.bottom - client.top);
	return &window->base;
}

void os_window_destroy(OS_Window *base)
{
	if (!base) return;
	OS_Win32Window *window = (OS_Win32Window *)base;
	OS_Win32Window **link = &os_win32_first_window;
	while (*link && *link != window) {
		link = &(*link)->next;
	}
	if (*link) {
		*link = window->next;
	}
	if (window->handle) DestroyWindow(window->handle);
	os_graphical_free_events(base);
	free(window);
}

void os_graphical_poll(void)
{
	for (OS_Win32Window *window = os_win32_first_window; window; window = window->next)
	{
		OS_Window *base = &window->base;
		os_graphical_reset_events(base);
		for (u32 key = 0; key < OS_Key_COUNT; ++key) {
			base->keys[key] &= OS_KEY_DOWN;
		}
		base->mouse_wheel = v2i(0, 0);
	}

	MSG message;
	while (PeekMessageW(&message, 0, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
}

b32 os_window_is_open(const OS_Window *window)
{
	return !!(window->status & OS_WINDOW_OPEN);
}

u32 os_window_event_count(const OS_Window *window)
{
	return window->event_count;
}

const OS_Event *os_window_event(const OS_Window *window, u32 index)
{
	Assert(index < window->event_count);
	return &window->events[index];
}

void os_window_set_cursor(OS_Window *window, OS_Cursor cursor)
{
	OS_Win32Window *win32_window = (OS_Win32Window *)window;
	Assert(cursor >= 0 && cursor < OS_CURSOR_COUNT);
	win32_window->cursor = cursor;
	if (win32_window->cursor != cursor) {
		SetCursor(os_win32.cursors[cursor]);
	}
}

b32 os_window_set_title_bar_style(OS_Window *base, OS_WindowTitleBarStyle style)
{
	Assert(base);
	OS_Win32Window *window = (OS_Win32Window *)base;
	return os_win32_set_title_bar_style(window->handle, style);
}

void os_window_set_fullscreen(OS_Window *base, b32 fullscreen)
{
	OS_Win32Window *window = (OS_Win32Window *)base;
	fullscreen = !!fullscreen;
	if (window->fullscreen == fullscreen) {
		return;
	}
	if (fullscreen)
	{
		window->windowed_style = (DWORD)GetWindowLongPtrW(window->handle, GWL_STYLE);
		window->windowed_placement = (WINDOWPLACEMENT) { .length = sizeof(window->windowed_placement) };
		GetWindowPlacement(window->handle, &window->windowed_placement);
		MONITORINFO monitor = { .cbSize = sizeof(monitor) };
		GetMonitorInfoW(MonitorFromWindow(window->handle, MONITOR_DEFAULTTONEAREST), &monitor);
		SetWindowLongPtrW(window->handle, GWL_STYLE, window->windowed_style & ~WS_OVERLAPPEDWINDOW);
		SetWindowPos(window->handle, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
			monitor.rcMonitor.right - monitor.rcMonitor.left,
			monitor.rcMonitor.bottom - monitor.rcMonitor.top,
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	}
	else
	{
		SetWindowLongPtrW(window->handle, GWL_STYLE, window->windowed_style);
		SetWindowPlacement(window->handle, &window->windowed_placement);
		SetWindowPos(window->handle, 0, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	}
	window->fullscreen = fullscreen;
}

b32 os_window_is_fullscreen(const OS_Window *base)
{
	return ((const OS_Win32Window *)base)->fullscreen;
}

void *os_window_native_handle(OS_Window *base)
{
	return ((OS_Win32Window *)base)->handle;
}

LRESULT CALLBACK os_win32_window_proc(HWND handle, UINT message, WPARAM wparam, LPARAM lparam)
{
	OS_Win32Window *window = (OS_Win32Window *)GetWindowLongPtrW(handle, GWLP_USERDATA);
	if (message == WM_NCCREATE)
	{
		CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
		window = create->lpCreateParams;
		SetWindowLongPtrW(handle, GWLP_USERDATA, (LONG_PTR)window);
	}
	if (!window) return DefWindowProcW(handle, message, wparam, lparam);
	OS_Window *base = &window->base;

	switch (message)
	{
		case WM_SETCURSOR:
		{
			if (LOWORD(lparam) == HTCLIENT)
			{
				SetCursor(os_win32.cursors[window->cursor]);
				return true;
			}
		}
		break;
		case WM_SETFOCUS:
		{
			base->status |= OS_WINDOW_FOCUSED;
			os_win32_push_event(base, OS_EVENT_WINDOW_FOCUS_GAINED);
			return 0;
		}
		break;
		case WM_KILLFOCUS:
		{
			base->status &= ~OS_WINDOW_FOCUSED;
			for (u32 key = 0; key < OS_Key_COUNT; ++key)
			{
				if (base->keys[key] & OS_KEY_DOWN) os_win32_key_up(base, (OS_Key)key);
			}
			os_win32_push_event(base, OS_EVENT_WINDOW_FOCUS_LOST);
			return 0;
		}
		break;
		case WM_ENTERSIZEMOVE: base->status |= OS_WINDOW_RESIZING; break;
		case WM_EXITSIZEMOVE: base->status &= ~OS_WINDOW_RESIZING; break;
		case WM_CLOSE:
		{
			base->status &= ~OS_WINDOW_OPEN;
			os_win32_push_event(base, OS_EVENT_WINDOW_CLOSE);
			return 0;
		}
		break;
		case WM_SIZE:
		{
			if (wparam == SIZE_MINIMIZED) {
				base->status |= OS_WINDOW_MINIMIZED;
			} else {
				base->status &= ~OS_WINDOW_MINIMIZED;
			}
			vec2i size = v2i((i32)LOWORD(lparam), (i32)HIWORD(lparam));
			if (size.x != base->size.x || size.y != base->size.y)
			{
				base->size = size;
				OS_Event *event = os_win32_push_event(base, OS_EVENT_WINDOW_RESIZE);
				if (event) event->size = size;
			}
			return 0;
		}
		break;
		case WM_MOUSEMOVE:
		{
			base->mouse_position = v2i((i16)LOWORD(lparam), (i16)HIWORD(lparam));
			OS_Event *event = os_win32_push_event(base, OS_EVENT_MOUSE_MOVE);
			if (event) event->mouse = base->mouse_position;
			return 0;
		}
		break;
		case WM_LBUTTONUP: case WM_MBUTTONUP: case WM_RBUTTONUP:
		case WM_LBUTTONDOWN: case WM_MBUTTONDOWN: case WM_RBUTTONDOWN:
		{
			base->mouse_position = v2i((i16)LOWORD(lparam), (i16)HIWORD(lparam));
			b32 down = message == WM_LBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_RBUTTONDOWN;
			OS_Key key = message == WM_LBUTTONUP || message == WM_LBUTTONDOWN ? OS_Key_MouseLeft
				: message == WM_MBUTTONUP || message == WM_MBUTTONDOWN ? OS_Key_MouseMiddle : OS_Key_MouseRight;
			if (down) {
				os_win32_key_down(base, key, false);
				SetCapture(handle);
			} else {
				os_win32_key_up(base, key);
				if (!((base->keys[OS_Key_MouseLeft] | base->keys[OS_Key_MouseMiddle] | base->keys[OS_Key_MouseRight]) & OS_KEY_DOWN)) ReleaseCapture();
			}
			return 0;
		}
		break;
		case WM_CAPTURECHANGED:
		{
			os_win32_key_up(base, OS_Key_MouseLeft);
			os_win32_key_up(base, OS_Key_MouseMiddle);
			os_win32_key_up(base, OS_Key_MouseRight);
			return 0;
		}
		break;
		case WM_SYSKEYUP:
		case WM_KEYUP:
		{
			os_win32_key_up(base, os_win32_key_from_message(wparam, lparam));
			return 0;
		}
		break;
		case WM_SYSKEYDOWN:
		case WM_KEYDOWN:
		{
			if (message == WM_SYSKEYDOWN && wparam == VK_F4) return DefWindowProcW(handle, message, wparam, lparam);
			os_win32_key_down(base, os_win32_key_from_message(wparam, lparam), !!(lparam & (1ll << 30)));
			return 0;
		}
		break;
		case WM_CHAR:
		{
			u16 code_unit = (u16)wparam;
			Rune rune = code_unit;
			if (code_unit >= 0xd800 && code_unit <= 0xdbff)
			{
				window->pending_high_surrogate = code_unit;
				return 0;
			}
			if (code_unit >= 0xdc00 && code_unit <= 0xdfff && window->pending_high_surrogate)
			{
				rune = 0x10000 + (((Rune)window->pending_high_surrogate - 0xd800) << 10) + ((Rune)code_unit - 0xdc00);
			}
			window->pending_high_surrogate = 0;
			OS_Event *event = os_win32_push_event(base, OS_EVENT_TEXT);
			if (event) event->rune = rune;
			return 0;
		}
		break;
		case WM_MOUSEWHEEL:
		{
			i32 raw_delta = GET_WHEEL_DELTA_WPARAM(wparam);
			i32 delta = SIGN(raw_delta);
			POINT mouse = { (i16)LOWORD(lparam), (i16)HIWORD(lparam) };
			ScreenToClient(handle, &mouse);
			base->mouse_position = v2i(mouse.x, mouse.y);
			base->mouse_wheel.y += delta;
			OS_Event *event = os_win32_push_event(base, OS_EVENT_MOUSE_WHEEL);
			if (event) event->wheel_y = (f32)raw_delta / WHEEL_DELTA;
			return 0;
		}
		break;
		case WM_MOUSEHWHEEL:
		{
			i32 raw_delta = GET_WHEEL_DELTA_WPARAM(wparam);
			i32 delta = SIGN(raw_delta);
			POINT mouse = { (i16)LOWORD(lparam), (i16)HIWORD(lparam) };
			ScreenToClient(handle, &mouse);
			base->mouse_position = v2i(mouse.x, mouse.y);
			base->mouse_wheel.x += delta;
			OS_Event *event = os_win32_push_event(base, OS_EVENT_MOUSE_WHEEL);
			if (event) event->wheel_x = (f32)raw_delta / WHEEL_DELTA;
			return 0;
		}
		break;
		case WM_DROPFILES:
		{
			HDROP drop = (HDROP)wparam;
			UINT count = DragQueryFileW(drop, 0xffffffff, NULL, 0);
			for (UINT index = 0; index < count; ++index)
			{
				UINT wide_size = DragQueryFileW(drop, index, NULL, 0) + 1;
				wchar_t *wide_path = malloc(sizeof(*wide_path) * wide_size);
				if (!wide_path) continue;
				DragQueryFileW(drop, index, wide_path, wide_size);
				int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, NULL, 0, NULL, NULL);
				char *path = utf8_size > 0 ? malloc((size_t)utf8_size) : NULL;
				if (path) WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, path, utf8_size, NULL, NULL);
				free(wide_path);
				if (!path) continue;
				OS_Event *event = os_win32_push_event(base, OS_EVENT_FILE_DROP);
				if (event) {
					event->path = string_from_data(path, (u32)utf8_size - 1);
				} else {
					free(path);
				}
			}
			DragFinish(drop);
			return 0;
		}
		break;
	}
	return DefWindowProcW(handle, message, wparam, lparam);
}
