#include "os_win32_internal.h"
#include "os_dialog.h"
#include <ShObjIdl.h>

#pragma comment(lib, "Shell32")
#pragma comment(lib, "Uuid")

Str os_dialog_open_file(Arena *arena)
{
	IFileOpenDialog *dialog = 0;
	HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, 0,
		CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&dialog);
	if (FAILED(hr)) return (Str) { 0 };

	hr = dialog->lpVtbl->Show(dialog, 0);
	// Cancelling a file picker is a normal empty result. The old layer asserted
	// because it treated HRESULT_FROM_WIN32(ERROR_CANCELLED) as a fatal error.
	if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
	{
		dialog->lpVtbl->Release(dialog);
		return (Str) { 0 };
	}
	if (FAILED(hr))
	{
		dialog->lpVtbl->Release(dialog);
		return (Str) { 0 };
	}

	IShellItem *item = 0;
	PWSTR wide_path = 0;
	hr = dialog->lpVtbl->GetResult(dialog, &item);
	if (SUCCEEDED(hr))
	{
		hr = item->lpVtbl->GetDisplayName(item, SIGDN_FILESYSPATH,
			&wide_path);
	}
	Str result = { 0 };
	if (SUCCEEDED(hr) && wide_path)
	{
		i32 size = WideCharToMultiByte(CP_UTF8, 0, wide_path, -1,
			0, 0, 0, 0);
		char *path = arena_push(arena, size);
		WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, path, size, 0, 0);
		result = str_from_data(path, size - 1);
	}
	if (wide_path) CoTaskMemFree(wide_path);
	if (item) item->lpVtbl->Release(item);
	dialog->lpVtbl->Release(dialog);
	return result;
}

