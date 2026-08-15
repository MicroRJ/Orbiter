typedef struct
{
	LoggerSink *function;
	void *user_data;
}
LoggerSinkSlot;

static const char *logger_level_names[LOG_LEVEL_COUNT] = {
	[LOG_LEVEL_TRACE] = "trace",
	[LOG_LEVEL_DEBUG] = "debug",
	[LOG_LEVEL_INFO] = "info",
	[LOG_LEVEL_WARNING] = "warning",
	[LOG_LEVEL_ERROR] = "error",
	[LOG_LEVEL_FATAL] = "fatal",
};

static const char *logger_console_colors[LOG_LEVEL_COUNT] = {
	[LOG_LEVEL_TRACE] = "\033[90m",
	[LOG_LEVEL_DEBUG] = "",
	[LOG_LEVEL_INFO] = "\033[94m",
	[LOG_LEVEL_WARNING] = "\033[35m",
	[LOG_LEVEL_ERROR] = "\033[91m",
	[LOG_LEVEL_FATAL] = "\033[97;41m",
};

global LogLevel logger_g_level = LOG_LEVEL_TRACE;
global LoggerSinkSlot logger_g_sinks[LOGGER_MAX_SINKS];
global u64 logger_g_sequence;
global thread_decl i32 logger_g_indent;

static void logger_console_sink(const LogRecord *record, void *user_data)
{
	(void)user_data;
	const char *color = logger_console_colors[record->level];
	printf("%s(%i) %s%-7s\033[0m: %*s%s\n", record->source.file, record->source.line, color, record->tag, record->indent * 2, "", record->message);
}

const char *logger_level_name(LogLevel level)
{
	Assert(level < LOG_LEVEL_COUNT);
	return logger_level_names[level];
}

void logger_set_level(LogLevel level)
{
	Assert(level < LOG_LEVEL_COUNT);
	logger_g_level = level;
}

LogLevel logger_get_level(void)
{
	return logger_g_level;
}

b32 logger_add_sink(LoggerSink *sink, void *user_data)
{
	Assert(sink);
	for (u32 index = 0; index < LOGGER_MAX_SINKS; ++index)
	{
		LoggerSinkSlot *slot = &logger_g_sinks[index];
		if (slot->function == sink && slot->user_data == user_data) {
			return true;
		}
		if (!slot->function)
		{
			*slot = (LoggerSinkSlot) { sink, user_data };
			return true;
		}
	}
	return false;
}

void logger_remove_sink(LoggerSink *sink, void *user_data)
{
	for (u32 index = 0; index < LOGGER_MAX_SINKS; ++index)
	{
		LoggerSinkSlot *slot = &logger_g_sinks[index];
		if (slot->function == sink && slot->user_data == user_data) {
			*slot = (LoggerSinkSlot) {};
		}
	}
}

void logger_indent(i32 delta)
{
	logger_g_indent += delta;
	Assert(logger_g_indent >= 0);
	Assert(logger_g_indent < 128);
}

void logger_write(LogLevel level, const char *tag, SourceLoc source, const char *message)
{
	Assert(level < LOG_LEVEL_COUNT);
	if (level < logger_g_level) {
		return;
	}
	LogRecord record = {
		.sequence = ++logger_g_sequence,
		.level = level,
		.source = source,
		.tag = tag ? tag : logger_level_name(level),
		.message = message ? message : "",
		.indent = logger_g_indent,
	};
	logger_console_sink(&record, 0);
	for (u32 index = 0; index < LOGGER_MAX_SINKS; ++index)
	{
		LoggerSinkSlot *slot = &logger_g_sinks[index];
		if (slot->function) {
			slot->function(&record, slot->user_data);
		}
	}
}

void assertion_failed(const char *file, int line, const char *expression)
{
	logger_write(LOG_LEVEL_FATAL, "assertion", (SourceLoc) { file, line }, temp_format_("assertion failed: %s", expression));
#if defined(_DEBUG)
	__debugbreak();
#endif
	exit(1);
}
