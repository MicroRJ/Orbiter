void *thread_temp_alloc(i64 size)
{
	global thread_decl char buffer[KiB(1)];
	global thread_decl u32 position;
	Assert(size <= sizeof(buffer));
	if (position + size > sizeof(buffer)) {
		position = 0;
	}
	void *memory = buffer + position;
	position += size;
	return memory;
}

char *temp_format_v(const char *format, u32 *length, va_list arguments)
{
	va_list count_arguments;
	va_copy(count_arguments, arguments);
	u32 size = _vscprintf(format, count_arguments);
	va_end(count_arguments);
	char *text = thread_temp_alloc(size + 1);
	vsprintf_s(text, size + 1, format, arguments);
	if (length) {
		*length = size;
	}
	return text;
}

char *temp_format_(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	char *text = temp_format_v(format, NULL, arguments);
	va_end(arguments);
	return text;
}
