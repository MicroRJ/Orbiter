ByteSpan push_file(Arena *arena, Str path)
{
	Assert(arena);
	if (!path.data || !path.size) return (ByteSpan) {};

	u64 arena_position = arena->position;
	char *path_cstr = arena_push_aligned(arena, (u64)path.size + 1, 1);
	memory_copy(path_cstr, path.data, path.size);
	path_cstr[path.size] = 0;
	Platform_File file = platform_access_file(path_cstr, PLATFORM_FILE_OPEN_EXISTING, PLATFORM_FILE_READ | PLATFORM_FILE_SHARE_READ | PLATFORM_FILE_SHARE_WRITE | PLATFORM_FILE_SHARE_DELETE);
	arena->position = arena_position;
	if (!platform_file_is_valid(file)) return (ByteSpan) {};

	u64 size = 0;
	b32 success = platform_get_file_size(file, &size) && size <= arena->reserved_size - arena->position;
	u8 *data = success ? arena_push_aligned(arena, size, 1) : 0;
	u64 bytes_read = 0;
	if (success) success = (!size || platform_read_file(file, data, size, &bytes_read)) && bytes_read == size;
	platform_close_file(file);
	if (!success)
	{
		arena->position = arena_position;
		return (ByteSpan) {};
	}
	return byte_span(data, size);
}
