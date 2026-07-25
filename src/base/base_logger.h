#ifndef BASE_LOGGER_H
#define BASE_LOGGER_H

typedef enum
{
	LOG_LEVEL_TRACE,
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_INFO,
	LOG_LEVEL_WARNING,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_FATAL,
	LOG_LEVEL_COUNT,
}
LogLevel;

typedef struct
{
	const char *file;
	i32 line;
}
SourceLoc;

typedef struct
{
	u64 sequence;
	LogLevel level;
	SourceLoc source;
	const char *tag;
	const char *message;
	i32 indent;
}
LogRecord;

typedef void LoggerSink(const LogRecord *record, void *user_data);

enum { LOGGER_MAX_SINKS = 8 };

#define SOURCE_LOC_HERE ((SourceLoc) { __FILE__, __LINE__ })
#define LOG_WRITE(level, message, ...) logger_write(level, 0, SOURCE_LOC_HERE, temp_format_(message, __VA_ARGS__))
#define LOG_TRACE(message, ...) LOG_WRITE(LOG_LEVEL_TRACE, message, __VA_ARGS__)
#define LOG_DEBUG(message, ...) LOG_WRITE(LOG_LEVEL_DEBUG, message, __VA_ARGS__)
#define LOG_INFO(message, ...) LOG_WRITE(LOG_LEVEL_INFO, message, __VA_ARGS__)
#define LOG_WARN(message, ...) LOG_WRITE(LOG_LEVEL_WARNING, message, __VA_ARGS__)
#define LOG_ERROR(message, ...) LOG_WRITE(LOG_LEVEL_ERROR, message, __VA_ARGS__)
#define LOG_FATAL(message, ...) LOG_WRITE(LOG_LEVEL_FATAL, message, __VA_ARGS__)

const char *logger_level_name(LogLevel level);
void logger_set_level(LogLevel level);
LogLevel logger_get_level(void);
b32 logger_add_sink(LoggerSink *sink, void *user_data);
void logger_remove_sink(LoggerSink *sink, void *user_data);
void logger_indent(i32 delta);
void logger_write(LogLevel level, const char *custom_tag, SourceLoc source, const char *message);

#endif
