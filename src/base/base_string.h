// A Str is a counted byte span. It may borrow storage and is not necessarily
// null terminated. LIT, str_from_cstr, str_push_copy, and str_push_copy_f
// do have a trailing null byte.

typedef struct {
	union { char *text, *data; };
	u32         size;
} Str;

#define LIT(x) ((Str) { .data = (x), .size = (sizeof(x) - 1) })

static inline Str str_from_data(const void *data, u32 size)
{
	Str result = {};
	result.data = (char *) data;
	result.size = size;
	return result;
}

static inline Str str_from_cstr(const char *data)
{
	Assert(data);
	return str_from_data(data, (u32)strlen(data));
}

b32 str_match(Str a, Str b);
i32 str_compare(Str a, Str b);
i32 str_compare_nocase(Str a, Str b);
b32 str_starts(Str str, Str prefix);
b32 str_ends(Str str, Str suffix);
b32 str_ends_nocase(Str str, Str suffix);
u32 str_find_last(Str str, Str bytes);
Str str_slice(Str str, i32 index, i32 size);

char *str_top(Arena *arena);
Str str_push(Arena *arena, Str str);
Str str_push_v(Arena *arena, const char *format, va_list arguments);
Str str_push_f(Arena *arena, const char *format, ...) __attribute__((format(printf, 2, 3)));
// RJ - This will null terminate the str
u32 str_end(Arena *arena, const char *top);

// RJ - These variants are null terminated!
Str str_push_copy_v(Arena *arena, const char *format, va_list vargs);
Str str_push_copy_f(Arena *arena, const char *format, ...) __attribute__((format(printf, 2, 3)));
Str str_push_copy(Arena *arena, Str str);

// TODO(RJ) remove these
b32 str_consume_prefix(Str *str, Str prefix);
Str str_consume_line(Str *remaining);
b32 str_to_u32(Str str, u32 *value);

