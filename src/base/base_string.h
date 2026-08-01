//
// ALL STRINGS ARE NULL TERMINATED
// Todo, remove const qualifier from string data, who tf cares
//

typedef struct {
	union { char *text, *data; };
	u32         size;
} String;

#define LIT(x) ((String) { .text = (x), .size = (sizeof(x) - 1) })

static
String string_from_data(const void *text, u32 size) {
	String result = {};
	result.text = (char *) text;
	result.size = size;
	return result;
}

char *begin_append_sequence(Arena *arena);
u32 end_append_sequence(Arena *arena, const char *start);
String push_append_string(Arena *arena, String string);

__attribute__((format(printf, 2, 3)))
String push_append_formatted(Arena *arena, const char *format, ...);

b32 string_match(String a, String b);
String string_slice(String str, i32 index, i32 size);


String push_formatted_v(Arena *arena, const char *format, va_list vargs);
String push_formatted(Arena *arena, const char *format, ...) __attribute__((format(printf, 2, 3)));

String push_string_wrap(Arena *arena, const char *text);
String push_string_copy(Arena *arena, String str);
