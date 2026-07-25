#ifndef BASE_TEMP_H
#define BASE_TEMP_H

void *thread_temp_alloc(i64 size);
char *temp_format_v(const char *format, u32 *length, va_list arguments);
char *temp_format_(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
