#include "os_win32_internal.h"
#include "os.h"
#include <combaseapi.h>
#include <timeapi.h>

#pragma comment(lib, "Ole32")
#pragma comment(lib, "Winmm")

OS_Win32State os_win32;

void os_win32_enable_ansi_console(void)
{
	HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD mode = 0;
	if (GetConsoleMode(output, &mode))
	{
		SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
	}
}

void os_win32_report_last_error(const char *operation)
{
	DWORD error = GetLastError();
	char *system_message = 0;
	FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		0, error, 0, (char *)&system_message, 0, 0);
	LOG_ERROR("%s failed with error %lu: %s", operation, error,
		system_message ? system_message : "unknown error");
	if (system_message) LocalFree(system_message);
}

b32 os_set_current_directory_to_executable(void)
{
	char path[4096];
	DWORD size = GetModuleFileNameA(0, path, ArrayCount(path));
	if (!size || size >= ArrayCount(path)) {
		os_win32_report_last_error("finding executable path");
		return false;
	}

	while (size && path[size - 1] != '\\' && path[size - 1] != '/') {
		--size;
	}
	if (!size) return false;
	path[size - 1] = 0;

	if (!SetCurrentDirectoryA(path)) {
		os_win32_report_last_error("setting working directory to executable");
		return false;
	}
	return true;
}

b32 os_init(void)
{
	memory_zero(&os_win32, sizeof(os_win32));
	os_win32_enable_ansi_console();
	if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)) {
		os_win32_report_last_error("setting main thread priority");
	}

	HRESULT hr = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
	if (FAILED(hr)) return false;
	os_win32.com_initialized = true;

	if (timeBeginPeriod(1) == TIMERR_NOERROR)
	{
		os_win32.timer_resolution_set = true;
	}
	return true;
}

void os_shutdown(void)
{
	if (os_win32.timer_resolution_set)
	{
		timeEndPeriod(1);
		os_win32.timer_resolution_set = false;
	}
	if (os_win32.com_initialized)
	{
		CoUninitialize();
		os_win32.com_initialized = false;
	}
}
