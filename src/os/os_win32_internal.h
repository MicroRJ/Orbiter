#ifndef OS_WIN32_INTERNAL_H
#define OS_WIN32_INTERNAL_H

#define NOMINMAX
#define _NO_CRT_STDIO_INLINE
#define _WIN32_WINNT 0x0601

#include "base.h"
#include "os_graphical.h"
#include <windows.h>

typedef struct OS_Win32State OS_Win32State;
struct OS_Win32State
{
	HCURSOR cursors[OS_CURSOR_COUNT];
	OS_Key  key_map[256];
	b32     com_initialized;
	b32     timer_resolution_set;
};

extern OS_Win32State os_win32;

LRESULT CALLBACK os_win32_window_proc(HWND window, UINT message,
	WPARAM wparam, LPARAM lparam);
void os_win32_report_last_error(const char *operation);
void os_win32_enable_ansi_console(void);

#endif
