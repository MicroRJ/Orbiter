char *begin_append_sequence(Arena *arena)
{
	return arena_top(arena);
}

u32 end_append_sequence(Arena *arena, const char *start)
{
	u32 size = (u32)arena_distance(arena, start);
	arena_push_byte(arena, 0);
	return size;
}

b32 string_match(String a, String b)
{
	return a.size == b.size && memory_match(a.text, b.text, a.size);
}

String push_append_formatted_v(Arena *arena, const char *format, va_list arguments)
{
	va_list count_arguments;
	va_copy(count_arguments, arguments);
	u32 size = _vscprintf(format, count_arguments);
	va_end(count_arguments);
	char *text = arena_push_aligned(arena, size + 1, 1);
	--arena->position;
	vsprintf_s(text, size + 1, format, arguments);
	return (String) { .text = text, .size = size };
}

String push_append_formatted(Arena *arena, const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	String result = push_append_formatted_v(arena, format, arguments);
	va_end(arguments);
	return result;
}

String push_append_string(Arena *arena, String string)
{
	char *text = arena_push_aligned(arena, string.size, 1);
	memory_copy(text, string.text, string.size);
	return (String) { .text = text, .size = string.size };
}

String string_slice(String string, i32 index, i32 size)
{
	if (index < 0) {
		index = string.size + index;
	}
	if (size < 0) {
		size = string.size - index + size + 1;
	}
	Assert(size >= 0 && size <= string.size);
	Assert(index >= 0 && index <= string.size);
	Assert(index + size >= 0 && index + size <= string.size);
	return (String) { .text = string.text + index, .size = size };
}

String push_string_wrap(Arena *arena, const char *text)
{
	return push_string_copy(arena, string_from_data(text, (u32)strlen(text)));
}

String push_string_copy(Arena *arena, String string)
{
	char *text = arena_push_aligned(arena, string.size + 1, 1);
	memory_copy(text, string.text, string.size);
	text[string.size] = 0;
	return (String) { .text = text, .size = string.size };
}

String push_formatted_v(Arena *arena, const char *format, va_list arguments)
{
	va_list count_arguments;
	va_copy(count_arguments, arguments);
	u32 size = _vscprintf(format, count_arguments);
	va_end(count_arguments);
	char *text = arena_push_aligned(arena, size + 1, 1);
	vsprintf_s(text, size + 1, format, arguments);
	return (String) { .text = text, .size = size };
}

String push_formatted(Arena *arena, const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	String result = push_formatted_v(arena, format, arguments);
	va_end(arguments);
	return result;
}
