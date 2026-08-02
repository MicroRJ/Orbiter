char *str_top(Arena *arena)
{
	return arena_top(arena);
}

u32 str_end(Arena *arena, const char *start)
{
	u32 size = (u32)arena_distance(arena, start);
	arena_push_byte(arena, 0);
	return size;
}

b32 str_match(Str a, Str b)
{
	return a.size == b.size && (a.size == 0 || memory_match(a.data, b.data, a.size));
}

i32 str_compare(Str a, Str b)
{
	u32 count = Min(a.size, b.size);
	for (u32 index = 0; index < count; index ++)
	{
		u8 left = (u8)a.data[index];
		u8 right = (u8)b.data[index];
		if (left != right) return left < right ? -1 : 1;
	}
	return a.size == b.size ? 0 : a.size < b.size ? -1 : 1;
}

static u8 lower(u8 byte)
{
	return byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte;
}

i32 str_compare_nocase(Str a, Str b)
{
	u32 count = Min(a.size, b.size);
	for (u32 index = 0; index < count; index ++)
	{
		u8 left = lower((u8)a.data[index]);
		u8 right = lower((u8)b.data[index]);
		if (left != right) return left < right ? -1 : 1;
	}
	return a.size == b.size ? 0 : a.size < b.size ? -1 : 1;
}

b32 str_starts(Str string, Str prefix)
{
	return string.size >= prefix.size && (!prefix.size || memory_match(string.data, prefix.data, prefix.size));
}

b32 str_ends(Str string, Str suffix)
{
	return string.size >= suffix.size && (!suffix.size || memory_match(string.data + string.size - suffix.size, suffix.data, suffix.size));
}

b32 str_ends_nocase(Str string, Str suffix)
{
	if (!suffix.size) return true;
	if (string.size < suffix.size) return false;
	Str end = str_from_data(string.data + string.size - suffix.size, suffix.size);
	return str_compare_nocase(end, suffix) == 0;
}

b32 str_consume_prefix(Str *string, Str prefix)
{
	Assert(string);
	if (!str_starts(*string, prefix)) return false;
	if (prefix.size) string->data += prefix.size;
	string->size -= prefix.size;
	return true;
}

u32 str_find_last(Str string, Str bytes)
{
	for (u32 index = string.size; index > 0; index --) {
		for (u32 byte_index = 0; byte_index < bytes.size; byte_index ++) {
			if (string.data[index - 1] == bytes.data[byte_index]) return index - 1;
		}
	}
	return MAX_VALUE_U32;
}

Str str_consume_line(Str *remaining)
{
	Assert(remaining);
	if (!remaining->size) return str_from_data(remaining->data, 0);
	u32 end = 0;
	while (end < remaining->size && remaining->data[end] != '\n') end ++;
	u32 line_size = end;
	if (line_size && remaining->data[line_size - 1] == '\r') line_size --;
	Str line = str_from_data(remaining->data, line_size);
	u32 consumed = end + (end < remaining->size);
	remaining->data += consumed;
	remaining->size -= consumed;
	return line;
}

b32 str_to_u32(Str string, u32 *value)
{
	Assert(value);
	if (!string.size) return false;
	u32 result = 0;
	for (u32 index = 0; index < string.size; index ++)
	{
		u8 byte = (u8)string.data[index];
		if (byte < '0' || byte > '9') return false;
		u32 digit = byte - '0';
		if (result > (MAX_VALUE_U32 - digit) / 10) return false;
		result = result * 10 + digit;
	}
	*value = result;
	return true;
}

Str str_push_v(Arena *arena, const char *format, va_list arguments)
{
	va_list count_arguments;
	va_copy(count_arguments, arguments);
	u32 size = _vscprintf(format, count_arguments);
	va_end(count_arguments);
	char *data = arena_push_aligned(arena, size + 1, 1);
	--arena->position;
	vsprintf_s(data, size + 1, format, arguments);
	return (Str) { .data = data, .size = size };
}

Str str_push_f(Arena *arena, const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	Str result = str_push_v(arena, format, arguments);
	va_end(arguments);
	return result;
}

Str str_push(Arena *arena, Str string)
{
	char *data = arena_push_aligned(arena, string.size, 1);
	if (string.size) memory_copy(data, string.data, string.size);
	return (Str) { .data = data, .size = string.size };
}

Str str_slice(Str string, i32 index, i32 size)
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
	return (Str) { .data = index ? string.data + index : string.data, .size = size };
}

Str str_push_copy(Arena *arena, Str string)
{
	char *data = arena_push_aligned(arena, string.size + 1, 1);
	if (string.size) memory_copy(data, string.data, string.size);
	data[string.size] = 0;
	return (Str) { .data = data, .size = string.size };
}

Str str_push_copy_v(Arena *arena, const char *format, va_list arguments)
{
	va_list count_arguments;
	va_copy(count_arguments, arguments);
	u32 size = _vscprintf(format, count_arguments);
	va_end(count_arguments);
	char *data = arena_push_aligned(arena, size + 1, 1);
	vsprintf_s(data, size + 1, format, arguments);
	return (Str) { .data = data, .size = size };
}

Str str_push_copy_f(Arena *arena, const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	Str result = str_push_copy_v(arena, format, arguments);
	va_end(arguments);
	return result;
}
