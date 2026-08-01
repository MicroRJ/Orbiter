#include "tabula_internal.h"

#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *tabula_default_allocate(void *user_data, size_t size)
{
	(void)user_data;
	return malloc(size);
}

static void tabula_default_deallocate(void *user_data, void *memory)
{
	(void)user_data;
	free(memory);
}

Tabula_Context *tabula_context_create(const Tabula_ContextDesc *description)
{
	Tabula_Allocator allocator = { 0 };
	if (description) allocator = description->allocator;

	if ((allocator.allocate == NULL) != (allocator.deallocate == NULL)) return NULL;
	if (!allocator.allocate) {
		allocator.allocate = tabula_default_allocate;
		allocator.deallocate = tabula_default_deallocate;
	}

	Tabula_Context *context = allocator.allocate(allocator.user_data, sizeof(*context));
	if (!context) return NULL;
	memset(context, 0, sizeof(*context));
	context->allocator = allocator;
	return context;
}

void tabula_context_destroy(Tabula_Context *context)
{
	if (!context) return;
	Tabula_Allocator allocator = context->allocator;
	Tabula_Block *block = context->blocks;
	while (block) {
		Tabula_Block *next = block->next;
		allocator.deallocate(allocator.user_data, block);
		block = next;
	}
	allocator.deallocate(allocator.user_data, context);
}

b32 tabula_context_out_of_memory(const Tabula_Context *context)
{
	return context && context->out_of_memory;
}

static void *tabula_allocate(Tabula_Context *context, size_t size)
{
	if (!context || context->out_of_memory) return NULL;
	if (size == 0) size = 1;
	if (size > SIZE_MAX - offsetof(Tabula_Block, data)) {
		context->out_of_memory = true;
		return NULL;
	}

	Tabula_Block *block = context->allocator.allocate(
		context->allocator.user_data, offsetof(Tabula_Block, data) + size);
	if (!block) {
		context->out_of_memory = true;
		return NULL;
	}
	block->next = context->blocks;
	context->blocks = block;
	memset(block->data, 0, size);
	return block->data;
}

static void *tabula_grow_array(
	Tabula_Context *context,
	const void *old_items,
	size_t old_count,
	size_t new_count,
	size_t item_size)
{
	if (new_count > SIZE_MAX / item_size) {
		context->out_of_memory = true;
		return NULL;
	}
	void *items = tabula_allocate(context, new_count * item_size);
	if (items && old_items && old_count) memcpy(items, old_items, old_count * item_size);
	return items;
}

static Tabula_String tabula_copy_string(Tabula_Context *context, Tabula_String string)
{
	Tabula_String result = { 0 };
	if (!context) return result;
	if (!string.data && string.length) {
		context->out_of_memory = true;
		return result;
	}
	if (string.length == SIZE_MAX) {
		context->out_of_memory = true;
		return result;
	}
	char *data = tabula_allocate(context, string.length + 1u);
	if (!data) return result;
	if (string.length) memcpy(data, string.data, string.length);
	data[string.length] = 0;
	result.data = data;
	result.length = string.length;
	return result;
}

static bool tabula_string_equal(Tabula_String a, Tabula_String b)
{
	return a.length == b.length &&
		(a.length == 0 || memcmp(a.data, b.data, a.length) == 0);
}

static bool tabula_string_is(Tabula_String string, const char *literal)
{
	size_t length = strlen(literal);
	return string.length == length &&
		(length == 0 || memcmp(string.data, literal, length) == 0);
}

static uint64_t tabula_hash_string(Tabula_String string)
{
	uint64_t hash = UINT64_C(1469598103934665603);
	for (size_t index = 0; index < string.length; ++index) {
		hash ^= (unsigned char)string.data[index];
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

Tabula_Value tabula_value_null(void)
{
	Tabula_Value result = { TABULA_VALUE_NULL, { 0 } };
	return result;
}

Tabula_Value tabula_value_boolean(b32 value)
{
	Tabula_Value result = { TABULA_VALUE_BOOLEAN, { 0 } };
	result.as.boolean = value != 0;
	return result;
}

Tabula_Value tabula_value_integer(i64 value)
{
	Tabula_Value result = { TABULA_VALUE_INTEGER, { 0 } };
	result.as.integer = value;
	return result;
}

Tabula_Value tabula_value_number(f64 value)
{
	Tabula_Value result = { TABULA_VALUE_NUMBER, { 0 } };
	result.as.number = value;
	return result;
}

Tabula_Value tabula_value_color(Tabula_Color value)
{
	Tabula_Value result = { TABULA_VALUE_COLOR, { 0 } };
	result.as.color = value;
	return result;
}

Tabula_Value tabula_value_string(Tabula_Context *context, Tabula_String value)
{
	if (!context) return tabula_value_null();
	Tabula_Value result = { TABULA_VALUE_STRING, { 0 } };
	result.as.string = tabula_copy_string(context, value);
	if (context && context->out_of_memory) return tabula_value_null();
	return result;
}

const char *tabula_value_type_name(Tabula_ValueType type)
{
	switch (type) {
	case TABULA_VALUE_NULL: return "null";
	case TABULA_VALUE_BOOLEAN: return "boolean";
	case TABULA_VALUE_INTEGER: return "integer";
	case TABULA_VALUE_NUMBER: return "number";
	case TABULA_VALUE_STRING: return "string";
	case TABULA_VALUE_COLOR: return "color";
	case TABULA_VALUE_TABLE: return "table";
	}
	return "invalid value";
}

static bool tabula_value_is_valid(Tabula_Value value)
{
	return value.type >= TABULA_VALUE_NULL && value.type <= TABULA_VALUE_TABLE;
}

static size_t tabula_table_find_index(const Tabula_Table *table, Tabula_String key)
{
	if (!table || !table->bucket_count) return SIZE_MAX;
	size_t mask = table->bucket_count - 1u;
	size_t slot = (size_t)tabula_hash_string(key) & mask;
	for (size_t miss = 0; miss < table->bucket_count; ++miss) {
		size_t stored = table->buckets[slot];
		if (!stored) return SIZE_MAX;
		size_t index = stored - 1u;
		if (tabula_string_equal(table->entries[index].key, key)) return index;
		slot = (slot + 1u) & mask;
	}
	return SIZE_MAX;
}

static bool tabula_table_rehash(Tabula_Table *table, size_t bucket_count)
{
	size_t *buckets = tabula_allocate(table->context, bucket_count * sizeof(*buckets));
	if (!buckets) return false;
	size_t mask = bucket_count - 1u;
	for (size_t index = 0; index < table->count; ++index) {
		size_t slot = (size_t)tabula_hash_string(table->entries[index].key) & mask;
		while (buckets[slot]) slot = (slot + 1u) & mask;
		buckets[slot] = index + 1u;
	}
	table->buckets = buckets;
	table->bucket_count = bucket_count;
	return true;
}

Tabula_Table *tabula_table_create(Tabula_Context *context)
{
	if (!context) return NULL;
	Tabula_Table *table = tabula_allocate(context, sizeof(*table));
	if (!table) return NULL;
	table->context = context;
	return table;
}

static bool tabula_table_set_ranges(
	Tabula_Table *table,
	Tabula_String key,
	Tabula_Value value,
	Tabula_SourceRange key_range,
	Tabula_SourceRange value_range)
{
	if (!table || (!key.data && key.length) || !tabula_value_is_valid(value)) return false;
	if (value.type == TABULA_VALUE_TABLE &&
		(!value.as.table || value.as.table->context != table->context)) return false;
	if (value.type == TABULA_VALUE_STRING) {
		value = tabula_value_string(table->context, value.as.string);
		if (table->context->out_of_memory) return false;
	}

	size_t existing = tabula_table_find_index(table, key);
	if (existing != SIZE_MAX) {
		table->entries[existing].value = value;
		table->entries[existing].key_range = key_range;
		table->entries[existing].value_range = value_range;
		return true;
	}

	if (!table->bucket_count) {
		if (!tabula_table_rehash(table, 8u)) return false;
	} else if ((table->count + 1u) * 10u >= table->bucket_count * 7u) {
		if (!tabula_table_rehash(table, table->bucket_count * 2u)) return false;
	}

	if (table->count == table->capacity) {
		size_t capacity = table->capacity ? table->capacity * 2u : 8u;
		Tabula_TableEntry *entries = tabula_grow_array(
			table->context, table->entries, table->count, capacity, sizeof(*entries));
		if (!entries) return false;
		table->entries = entries;
		table->capacity = capacity;
	}

	Tabula_String owned_key = tabula_copy_string(table->context, key);
	if (table->context->out_of_memory) return false;
	size_t index = table->count++;
	table->entries[index].key = owned_key;
	table->entries[index].value = value;
	table->entries[index].key_range = key_range;
	table->entries[index].value_range = value_range;

	size_t mask = table->bucket_count - 1u;
	size_t slot = (size_t)tabula_hash_string(owned_key) & mask;
	while (table->buckets[slot]) slot = (slot + 1u) & mask;
	table->buckets[slot] = index + 1u;
	return true;
}

b32 tabula_table_set(Tabula_Table *table, Tabula_String key, Tabula_Value value)
{
	Tabula_SourceRange range = { 0 };
	return tabula_table_set_ranges(table, key, value, range, range);
}

const Tabula_TableEntry *tabula_table_get_entry(
	const Tabula_Table *table, Tabula_String key)
{
	size_t index = tabula_table_find_index(table, key);
	return index == SIZE_MAX ? NULL : &table->entries[index];
}

const Tabula_Value *tabula_table_get(const Tabula_Table *table, Tabula_String key)
{
	const Tabula_TableEntry *entry = tabula_table_get_entry(table, key);
	return entry ? &entry->value : NULL;
}

size_t tabula_table_count(const Tabula_Table *table)
{
	return table ? table->count : 0;
}

const Tabula_TableEntry *tabula_table_entry_at(
	const Tabula_Table *table, size_t index)
{
	return table && index < table->count ? &table->entries[index] : NULL;
}

static size_t tabula_environment_find_index(
	const Tabula_Environment *environment, Tabula_String name)
{
	if (!environment || !environment->bucket_count) return SIZE_MAX;
	size_t mask = environment->bucket_count - 1u;
	size_t slot = (size_t)tabula_hash_string(name) & mask;
	for (size_t miss = 0; miss < environment->bucket_count; ++miss) {
		size_t stored = environment->buckets[slot];
		if (!stored) return SIZE_MAX;
		size_t index = stored - 1u;
		if (tabula_string_equal(environment->symbols[index].name, name)) return index;
		slot = (slot + 1u) & mask;
	}
	return SIZE_MAX;
}

static bool tabula_environment_rehash(Tabula_Environment *environment, size_t bucket_count)
{
	size_t *buckets = tabula_allocate(environment->context, bucket_count * sizeof(*buckets));
	if (!buckets) return false;
	size_t mask = bucket_count - 1u;
	for (size_t index = 0; index < environment->count; ++index) {
		size_t slot = (size_t)tabula_hash_string(environment->symbols[index].name) & mask;
		while (buckets[slot]) slot = (slot + 1u) & mask;
		buckets[slot] = index + 1u;
	}
	environment->buckets = buckets;
	environment->bucket_count = bucket_count;
	return true;
}

Tabula_Environment *tabula_environment_create(Tabula_Context *context)
{
	if (!context) return NULL;
	Tabula_Environment *environment = tabula_allocate(context, sizeof(*environment));
	if (!environment) return NULL;
	environment->context = context;
	return environment;
}

static Tabula_Symbol *tabula_environment_add(
	Tabula_Environment *environment, Tabula_String name)
{
	if (!environment || (!name.data && name.length) || !name.length) return NULL;
	if (tabula_environment_find_index(environment, name) != SIZE_MAX) return NULL;
	if (!environment->bucket_count) {
		if (!tabula_environment_rehash(environment, 8u)) return NULL;
	} else if ((environment->count + 1u) * 10u >= environment->bucket_count * 7u) {
		if (!tabula_environment_rehash(environment, environment->bucket_count * 2u)) return NULL;
	}
	if (environment->count == environment->capacity) {
		size_t capacity = environment->capacity ? environment->capacity * 2u : 8u;
		Tabula_Symbol *symbols = tabula_grow_array(
			environment->context, environment->symbols, environment->count,
			capacity, sizeof(*symbols));
		if (!symbols) return NULL;
		environment->symbols = symbols;
		environment->capacity = capacity;
	}

	Tabula_String owned_name = tabula_copy_string(environment->context, name);
	if (environment->context->out_of_memory) return NULL;
	size_t index = environment->count++;
	Tabula_Symbol *symbol = &environment->symbols[index];
	symbol->name = owned_name;
	size_t mask = environment->bucket_count - 1u;
	size_t slot = (size_t)tabula_hash_string(owned_name) & mask;
	while (environment->buckets[slot]) slot = (slot + 1u) & mask;
	environment->buckets[slot] = index + 1u;
	return symbol;
}

b32 tabula_environment_add_constant(
	Tabula_Environment *environment, Tabula_String name, Tabula_Value value)
{
	if (!environment || !tabula_value_is_valid(value)) return false;
	if (value.type == TABULA_VALUE_TABLE &&
		(!value.as.table || value.as.table->context != environment->context)) return false;
	Tabula_Symbol *symbol = tabula_environment_add(environment, name);
	if (!symbol) return false;
	symbol->kind = TABULA_SYMBOL_CONSTANT;
	if (value.type == TABULA_VALUE_STRING) {
		value = tabula_value_string(environment->context, value.as.string);
		if (environment->context->out_of_memory) return false;
	}
	symbol->as.constant = value;
	return true;
}

b32 tabula_environment_add_function(
	Tabula_Environment *environment, const Tabula_FunctionDesc *description)
{
	if (!environment || !description || !description->function) return false;
	if (description->maximum_arguments != TABULA_ANY_ARITY &&
		description->minimum_arguments > description->maximum_arguments) return false;
	Tabula_Symbol *symbol = tabula_environment_add(environment, description->name);
	if (!symbol) return false;
	symbol->kind = TABULA_SYMBOL_FUNCTION;
	symbol->as.function = *description;
	symbol->as.function.name = symbol->name;
	return true;
}

typedef enum Tabula_TokenType {
	TABULA_TOKEN_ERROR,
	TABULA_TOKEN_EOF,
	TABULA_TOKEN_IDENTIFIER,
	TABULA_TOKEN_INTEGER,
	TABULA_TOKEN_NUMBER,
	TABULA_TOKEN_STRING,
	TABULA_TOKEN_COLOR,
	TABULA_TOKEN_DOT,
	TABULA_TOKEN_LEFT_PAREN,
	TABULA_TOKEN_RIGHT_PAREN,
	TABULA_TOKEN_LEFT_BRACE,
	TABULA_TOKEN_RIGHT_BRACE,
	TABULA_TOKEN_COMMA,
	TABULA_TOKEN_EQUAL,
	TABULA_TOKEN_DECLARE,
	TABULA_TOKEN_SEMICOLON,
} Tabula_TokenType;

typedef struct Tabula_Token {
	Tabula_TokenType type;
	Tabula_SourceRange range;
	Tabula_String text;
	Tabula_Value literal;
	Tabula_String error;
} Tabula_Token;

typedef struct Tabula_Position {
	size_t offset;
	size_t line;
	size_t column;
} Tabula_Position;

typedef struct Tabula_Parser {
	Tabula_Context *context;
	Tabula_Ast *ast;
	Tabula_Position position;
	Tabula_Token current;
	Tabula_Diagnostic *diagnostic;
} Tabula_Parser;

static Tabula_SourceRange tabula_range(
	const Tabula_Ast *ast, Tabula_Position start, Tabula_Position end)
{
	Tabula_SourceRange range;
	memset(&range, 0, sizeof(range));
	range.source_name = ast->source.name;
	range.start = start.offset;
	range.end = end.offset;
	range.line = start.line;
	range.column = start.column;
	range.end_line = end.line;
	range.end_column = end.column;
	return range;
}

static Tabula_SourceRange tabula_join_ranges(
	Tabula_SourceRange first, Tabula_SourceRange last)
{
	first.end = last.end;
	first.end_line = last.end_line;
	first.end_column = last.end_column;
	return first;
}

static Tabula_Diagnostic *tabula_diagnostic_v(
	Tabula_Context *context,
	Tabula_DiagnosticStage stage,
	Tabula_SourceRange range,
	const char *format,
	va_list arguments)
{
	va_list copy;
	va_copy(copy, arguments);
	int needed = vsnprintf(NULL, 0, format, copy);
	va_end(copy);
	if (needed < 0) return NULL;

	char *message = tabula_allocate(context, (size_t)needed + 1u);
	Tabula_Diagnostic *diagnostic = tabula_allocate(context, sizeof(*diagnostic));
	if (!message || !diagnostic) return NULL;
	vsnprintf(message, (size_t)needed + 1u, format, arguments);
	diagnostic->stage = stage;
	diagnostic->range = range;
	diagnostic->message.data = message;
	diagnostic->message.length = (size_t)needed;
	return diagnostic;
}

static Tabula_Diagnostic *tabula_diagnostic(
	Tabula_Context *context,
	Tabula_DiagnosticStage stage,
	Tabula_SourceRange range,
	const char *format,
	...)
{
	va_list arguments;
	va_start(arguments, format);
	Tabula_Diagnostic *diagnostic =
		tabula_diagnostic_v(context, stage, range, format, arguments);
	va_end(arguments);
	return diagnostic;
}

static char tabula_parser_peek(const Tabula_Parser *parser, size_t lookahead)
{
	size_t offset = parser->position.offset + lookahead;
	return offset < parser->ast->source.text.length
		? parser->ast->source.text.data[offset] : 0;
}

static void tabula_parser_advance_byte(Tabula_Parser *parser)
{
	if (parser->position.offset >= parser->ast->source.text.length) return;
	char byte = parser->ast->source.text.data[parser->position.offset++];
	if (byte == '\r') {
		if (tabula_parser_peek(parser, 0) == '\n') ++parser->position.offset;
		++parser->position.line;
		parser->position.column = 1;
	} else if (byte == '\n') {
		++parser->position.line;
		parser->position.column = 1;
	} else {
		++parser->position.column;
	}
}

static bool tabula_is_digit(char byte)
{
	return byte >= '0' && byte <= '9';
}

static bool tabula_is_letter(char byte)
{
	return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
}

static bool tabula_is_identifier_start(char byte)
{
	return tabula_is_letter(byte) || byte == '_';
}

static bool tabula_is_identifier_continue(char byte)
{
	return tabula_is_identifier_start(byte) || tabula_is_digit(byte);
}

static int tabula_digit_value(char byte)
{
	if (byte >= '0' && byte <= '9') return byte - '0';
	if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
	if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
	return -1;
}

static Tabula_Token tabula_token_from_range(
	Tabula_Parser *parser, Tabula_TokenType type, Tabula_Position start)
{
	Tabula_Token token;
	memset(&token, 0, sizeof(token));
	token.type = type;
	token.range = tabula_range(parser->ast, start, parser->position);
	token.text.data = parser->ast->source.text.data + start.offset;
	token.text.length = parser->position.offset - start.offset;
	return token;
}

static Tabula_Token tabula_error_token(
	Tabula_Parser *parser, Tabula_Position start, const char *message)
{
	Tabula_Token token = tabula_token_from_range(parser, TABULA_TOKEN_ERROR, start);
	token.error = tabula_copy_string(
		parser->context, (Tabula_String){ message, strlen(message) });
	return token;
}

static void tabula_skip_trivia(Tabula_Parser *parser)
{
	for (;;) {
		char byte = tabula_parser_peek(parser, 0);
		if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
			tabula_parser_advance_byte(parser);
		} else if (byte == '/' && tabula_parser_peek(parser, 1) == '/') {
			while (parser->position.offset < parser->ast->source.text.length &&
				(byte = tabula_parser_peek(parser, 0)) != '\r' &&
				byte != '\n') tabula_parser_advance_byte(parser);
		} else {
			break;
		}
	}
}

static Tabula_Token tabula_scan_string(Tabula_Parser *parser, Tabula_Position start)
{
	char quote = tabula_parser_peek(parser, 0);
	tabula_parser_advance_byte(parser);
	size_t remaining = parser->ast->source.text.length - parser->position.offset;
	char *decoded = tabula_allocate(parser->context, remaining + 1u);
	if (!decoded) return tabula_error_token(parser, start, "out of memory");
	size_t length = 0;
	bool terminated = false;

	while (parser->position.offset < parser->ast->source.text.length) {
		char byte = tabula_parser_peek(parser, 0);
		if (byte == '\r' || byte == '\n') break;
		tabula_parser_advance_byte(parser);
		if (byte == quote) {
			terminated = true;
			break;
		}
		if (byte == '\\' && parser->position.offset < parser->ast->source.text.length) {
			char escaped = tabula_parser_peek(parser, 0);
			tabula_parser_advance_byte(parser);
			if (escaped == '\\' || escaped == '\'' || escaped == '"') {
				decoded[length++] = escaped;
			} else if (quote == '"' && escaped == '0') {
				decoded[length++] = 0;
			} else if (quote == '"' && escaped == 'n') {
				decoded[length++] = '\n';
			} else if (quote == '"' && escaped == 'r') {
				decoded[length++] = '\r';
			} else if (quote == '"' && escaped == 't') {
				decoded[length++] = '\t';
			} else {
				decoded[length++] = '\\';
				decoded[length++] = escaped;
			}
		} else {
			decoded[length++] = byte;
		}
	}

	if (!terminated) return tabula_error_token(parser, start, "unterminated string literal");
	decoded[length] = 0;
	Tabula_Token token = tabula_token_from_range(parser, TABULA_TOKEN_STRING, start);
	token.literal.type = TABULA_VALUE_STRING;
	token.literal.as.string.data = decoded;
	token.literal.as.string.length = length;
	return token;
}

static Tabula_Token tabula_scan_color(Tabula_Parser *parser, Tabula_Position start)
{
	tabula_parser_advance_byte(parser);
	u64 packed = 0;
	size_t digits = 0;
	int digit;
	while ((digit = tabula_digit_value(tabula_parser_peek(parser, 0))) >= 0) {
		packed = packed * 16u + (u64)digit;
		++digits;
		tabula_parser_advance_byte(parser);
	}
	if ((digits != 6u && digits != 8u) ||
		tabula_is_identifier_continue(tabula_parser_peek(parser, 0))) {
		while (tabula_is_identifier_continue(tabula_parser_peek(parser, 0)))
			tabula_parser_advance_byte(parser);
		return tabula_error_token(
			parser, start, "color literal must contain 6 or 8 hexadecimal digits");
	}
	Tabula_Token token = tabula_token_from_range(parser, TABULA_TOKEN_COLOR, start);
	token.literal.type = TABULA_VALUE_COLOR;
	token.literal.as.color.r = (u8)(packed >> (digits == 8u ? 24u : 16u));
	token.literal.as.color.g = (u8)(packed >> (digits == 8u ? 16u : 8u));
	token.literal.as.color.b = (u8)(packed >> (digits == 8u ? 8u : 0u));
	token.literal.as.color.a = digits == 8u ? (u8)packed : 255u;
	return token;
}

static Tabula_Token tabula_scan_number(Tabula_Parser *parser, Tabula_Position start)
{
	char first = tabula_parser_peek(parser, 0);
	char prefix = tabula_parser_peek(parser, 1);
	if (first == '0' &&
		(prefix == 'b' || prefix == 'B' || prefix == 'x' || prefix == 'X' ||
		 prefix == 'h' || prefix == 'H')) {
		unsigned base = (prefix == 'b' || prefix == 'B') ? 2u : 16u;
		tabula_parser_advance_byte(parser);
		tabula_parser_advance_byte(parser);
		u64 integer = 0;
		size_t digits = 0;
		bool overflow = false;
		for (;;) {
			int digit = tabula_digit_value(tabula_parser_peek(parser, 0));
			if (digit < 0 || (unsigned)digit >= base) break;
			if (integer > (u64)INT64_MAX / base ||
				integer * base > (u64)INT64_MAX - (u64)digit) overflow = true;
			if (!overflow) integer = integer * base + (u64)digit;
			++digits;
			tabula_parser_advance_byte(parser);
		}
		if (!digits || overflow ||
			tabula_is_identifier_continue(tabula_parser_peek(parser, 0))) {
			while (tabula_is_identifier_continue(tabula_parser_peek(parser, 0)))
				tabula_parser_advance_byte(parser);
			return tabula_error_token(parser, start,
				overflow ? "integer literal is out of range" : "invalid integer literal");
		}
		Tabula_Token token = tabula_token_from_range(parser, TABULA_TOKEN_INTEGER, start);
		token.literal = tabula_value_integer((i64)integer);
		return token;
	}

	u64 integer = 0;
	f64 number = 0.0;
	bool integer_overflow = false;
	while (tabula_is_digit(tabula_parser_peek(parser, 0))) {
		unsigned digit = (unsigned)(tabula_parser_peek(parser, 0) - '0');
		if (integer > (u64)INT64_MAX / 10u ||
			integer * 10u > (u64)INT64_MAX - digit) integer_overflow = true;
		if (!integer_overflow) integer = integer * 10u + digit;
		number = number * 10.0 + digit;
		tabula_parser_advance_byte(parser);
	}

	bool is_number = false;
	if (tabula_parser_peek(parser, 0) == '.') {
		is_number = true;
		tabula_parser_advance_byte(parser);
		f64 place = 0.1;
		while (tabula_is_digit(tabula_parser_peek(parser, 0))) {
			number += (tabula_parser_peek(parser, 0) - '0') * place;
			place *= 0.1;
			tabula_parser_advance_byte(parser);
		}
	}
	if (tabula_parser_peek(parser, 0) == '%') {
		is_number = true;
		number /= 100.0;
		tabula_parser_advance_byte(parser);
	}
	if (tabula_parser_peek(parser, 0) == 'f' ||
		tabula_parser_peek(parser, 0) == 'F') {
		is_number = true;
		tabula_parser_advance_byte(parser);
	}
	if (tabula_is_identifier_continue(tabula_parser_peek(parser, 0))) {
		while (tabula_is_identifier_continue(tabula_parser_peek(parser, 0)))
			tabula_parser_advance_byte(parser);
		return tabula_error_token(parser, start, "invalid numeric literal");
	}
	if (!is_number && integer_overflow)
		return tabula_error_token(parser, start, "integer literal is out of range");
	if (is_number && number > DBL_MAX)
		return tabula_error_token(parser, start, "number literal is out of range");

	Tabula_Token token = tabula_token_from_range(
		parser, is_number ? TABULA_TOKEN_NUMBER : TABULA_TOKEN_INTEGER, start);
	token.literal = is_number
		? tabula_value_number(number) : tabula_value_integer((i64)integer);
	return token;
}

static Tabula_Token tabula_scan_token(Tabula_Parser *parser)
{
	tabula_skip_trivia(parser);
	Tabula_Position start = parser->position;
	char byte = tabula_parser_peek(parser, 0);
	if (parser->position.offset >= parser->ast->source.text.length)
		return tabula_token_from_range(parser, TABULA_TOKEN_EOF, start);
	if (!byte) {
		tabula_parser_advance_byte(parser);
		return tabula_error_token(parser, start, "NUL byte outside a string literal");
	}

	if (tabula_is_identifier_start(byte)) {
		tabula_parser_advance_byte(parser);
		while (tabula_is_identifier_continue(tabula_parser_peek(parser, 0)))
			tabula_parser_advance_byte(parser);
		return tabula_token_from_range(parser, TABULA_TOKEN_IDENTIFIER, start);
	}
	if (tabula_is_digit(byte)) return tabula_scan_number(parser, start);
	if (byte == '#') return tabula_scan_color(parser, start);
	if (byte == '\'' || byte == '"') return tabula_scan_string(parser, start);

	tabula_parser_advance_byte(parser);
	switch (byte) {
	case '.': return tabula_token_from_range(parser, TABULA_TOKEN_DOT, start);
	case '(': return tabula_token_from_range(parser, TABULA_TOKEN_LEFT_PAREN, start);
	case ')': return tabula_token_from_range(parser, TABULA_TOKEN_RIGHT_PAREN, start);
	case '{': return tabula_token_from_range(parser, TABULA_TOKEN_LEFT_BRACE, start);
	case '}': return tabula_token_from_range(parser, TABULA_TOKEN_RIGHT_BRACE, start);
	case ',': return tabula_token_from_range(parser, TABULA_TOKEN_COMMA, start);
	case '=': return tabula_token_from_range(parser, TABULA_TOKEN_EQUAL, start);
	case ';': return tabula_token_from_range(parser, TABULA_TOKEN_SEMICOLON, start);
	case ':':
		if (tabula_parser_peek(parser, 0) == '=') {
			tabula_parser_advance_byte(parser);
			return tabula_token_from_range(parser, TABULA_TOKEN_DECLARE, start);
		}
		return tabula_error_token(parser, start, "expected '=' after ':'");
	default: return tabula_error_token(parser, start, "unrecognized character");
	}
}

static const char *tabula_token_name(Tabula_TokenType type)
{
	switch (type) {
	case TABULA_TOKEN_ERROR: return "valid token";
	case TABULA_TOKEN_EOF: return "end of input";
	case TABULA_TOKEN_IDENTIFIER: return "identifier";
	case TABULA_TOKEN_INTEGER: return "integer";
	case TABULA_TOKEN_NUMBER: return "number";
	case TABULA_TOKEN_STRING: return "string";
	case TABULA_TOKEN_COLOR: return "color";
	case TABULA_TOKEN_DOT: return "'.'";
	case TABULA_TOKEN_LEFT_PAREN: return "'('";
	case TABULA_TOKEN_RIGHT_PAREN: return "')'";
	case TABULA_TOKEN_LEFT_BRACE: return "'{'";
	case TABULA_TOKEN_RIGHT_BRACE: return "'}'";
	case TABULA_TOKEN_COMMA: return "','";
	case TABULA_TOKEN_EQUAL: return "'='";
	case TABULA_TOKEN_DECLARE: return "':='";
	case TABULA_TOKEN_SEMICOLON: return "';'";
	}
	return "token";
}

static void tabula_parser_error(
	Tabula_Parser *parser, Tabula_SourceRange range, const char *format, ...)
{
	if (parser->diagnostic) return;
	va_list arguments;
	va_start(arguments, format);
	parser->diagnostic = tabula_diagnostic_v(
		parser->context, TABULA_DIAGNOSTIC_PARSE, range, format, arguments);
	va_end(arguments);
}

static void tabula_parser_advance(Tabula_Parser *parser)
{
	parser->current = tabula_scan_token(parser);
}

static bool tabula_parser_take(Tabula_Parser *parser, Tabula_TokenType type)
{
	if (parser->current.type != type) return false;
	tabula_parser_advance(parser);
	return true;
}

static Tabula_Token tabula_parser_expect(
	Tabula_Parser *parser, Tabula_TokenType type)
{
	Tabula_Token token = parser->current;
	if (token.type == type) {
		tabula_parser_advance(parser);
	} else if (token.type == TABULA_TOKEN_ERROR) {
		tabula_parser_error(parser, token.range, "%.*s",
			(int)token.error.length, token.error.data ? token.error.data : "");
	} else {
		tabula_parser_error(parser, token.range, "expected %s, got %s",
			tabula_token_name(type), tabula_token_name(token.type));
	}
	return token;
}

static Tabula_Node *tabula_new_node(
	Tabula_Parser *parser, Tabula_NodeKind kind, Tabula_SourceRange range)
{
	Tabula_Node *node = tabula_allocate(parser->context, sizeof(*node));
	if (!node) {
		tabula_parser_error(parser, range, "out of memory");
		return NULL;
	}
	node->kind = kind;
	node->range = range;
	return node;
}

static bool tabula_node_list_push(
	Tabula_Parser *parser, Tabula_NodeList *list, Tabula_Node *node)
{
	if (list->count == list->capacity) {
		size_t capacity = list->capacity ? list->capacity * 2u : 8u;
		Tabula_Node **items = tabula_grow_array(
			parser->context, list->items, list->count, capacity, sizeof(*items));
		if (!items) {
			tabula_parser_error(parser, node ? node->range : parser->current.range,
				"out of memory");
			return false;
		}
		list->items = items;
		list->capacity = capacity;
	}
	list->items[list->count++] = node;
	return true;
}

static Tabula_Node *tabula_parse_expression(Tabula_Parser *parser);
static Tabula_Node *tabula_parse_statement(Tabula_Parser *parser);

static Tabula_Node *tabula_parse_table(Tabula_Parser *parser)
{
	Tabula_Token opening = tabula_parser_expect(parser, TABULA_TOKEN_LEFT_BRACE);
	if (parser->diagnostic) return NULL;
	Tabula_Node *table = tabula_new_node(parser, TABULA_NODE_TABLE, opening.range);
	if (!table) return NULL;

	while (parser->current.type != TABULA_TOKEN_RIGHT_BRACE &&
		parser->current.type != TABULA_TOKEN_EOF && !parser->diagnostic) {
		while (tabula_parser_take(parser, TABULA_TOKEN_SEMICOLON)) {}
		if (parser->current.type == TABULA_TOKEN_RIGHT_BRACE) break;
		Tabula_Node *statement = tabula_parse_statement(parser);
		if (!statement) break;
		if (statement->kind != TABULA_NODE_ASSIGN &&
			statement->kind != TABULA_NODE_DECLARE) {
			tabula_parser_error(parser, statement->range,
				"table bodies contain assignments or local declarations");
			break;
		}
		if (!tabula_node_list_push(parser, &table->as.list, statement)) break;
		while (tabula_parser_take(parser, TABULA_TOKEN_SEMICOLON)) {}
	}

	Tabula_Token closing = tabula_parser_expect(parser, TABULA_TOKEN_RIGHT_BRACE);
	if (parser->diagnostic) return NULL;
	table->range = tabula_join_ranges(opening.range, closing.range);
	return table;
}

static Tabula_Node *tabula_parse_primary(Tabula_Parser *parser)
{
	Tabula_Token token = parser->current;
	if (token.type == TABULA_TOKEN_ERROR) {
		tabula_parser_error(parser, token.range, "%.*s",
			(int)token.error.length, token.error.data ? token.error.data : "");
		return NULL;
	}

	if (token.type == TABULA_TOKEN_INTEGER ||
		token.type == TABULA_TOKEN_NUMBER ||
		token.type == TABULA_TOKEN_STRING ||
		token.type == TABULA_TOKEN_COLOR) {
		tabula_parser_advance(parser);
		Tabula_Node *literal = tabula_new_node(parser, TABULA_NODE_LITERAL, token.range);
		if (literal) literal->as.literal = token.literal;
		return literal;
	}

	if (token.type == TABULA_TOKEN_IDENTIFIER) {
		tabula_parser_advance(parser);
		if (tabula_string_is(token.text, "true") ||
			tabula_string_is(token.text, "false") ||
			tabula_string_is(token.text, "null")) {
			Tabula_Node *literal = tabula_new_node(
				parser, TABULA_NODE_LITERAL, token.range);
			if (literal) {
				if (tabula_string_is(token.text, "null"))
					literal->as.literal = tabula_value_null();
				else literal->as.literal = tabula_value_boolean(
					tabula_string_is(token.text, "true"));
			}
			return literal;
		}
		Tabula_Node *name = tabula_new_node(parser, TABULA_NODE_NAME, token.range);
		if (name) name->as.name = token.text;
		return name;
	}

	if (token.type == TABULA_TOKEN_LEFT_BRACE) return tabula_parse_table(parser);

	tabula_parser_error(parser, token.range, "expected expression, got %s",
		tabula_token_name(token.type));
	return NULL;
}

static Tabula_Node *tabula_parse_expression(Tabula_Parser *parser)
{
	Tabula_Node *expression = tabula_parse_primary(parser);
	if (!expression) return NULL;

	while (!parser->diagnostic) {
		if (tabula_parser_take(parser, TABULA_TOKEN_DOT)) {
			Tabula_Token name = tabula_parser_expect(parser, TABULA_TOKEN_IDENTIFIER);
			if (parser->diagnostic) return NULL;
			Tabula_Node *member = tabula_new_node(parser, TABULA_NODE_MEMBER,
				tabula_join_ranges(expression->range, name.range));
			if (!member) return NULL;
			member->as.member.object = expression;
			member->as.member.name = name.text;
			member->as.member.name_range = name.range;
			expression = member;
		} else if (parser->current.type == TABULA_TOKEN_LEFT_PAREN) {
			Tabula_Token opening = tabula_parser_expect(parser, TABULA_TOKEN_LEFT_PAREN);
			Tabula_Node *call = tabula_new_node(parser, TABULA_NODE_CALL,
				tabula_join_ranges(expression->range, opening.range));
			if (!call) return NULL;
			call->as.call.callee = expression;
			if (parser->current.type != TABULA_TOKEN_RIGHT_PAREN) {
				for (;;) {
					Tabula_Node *argument = tabula_parse_expression(parser);
					if (!argument || !tabula_node_list_push(
						parser, &call->as.call.arguments, argument)) return NULL;
					if (!tabula_parser_take(parser, TABULA_TOKEN_COMMA)) break;
					if (parser->current.type == TABULA_TOKEN_RIGHT_PAREN) {
						tabula_parser_error(parser, parser->current.range,
							"expected expression after ','");
						return NULL;
					}
				}
			}
			Tabula_Token closing = tabula_parser_expect(parser, TABULA_TOKEN_RIGHT_PAREN);
			if (parser->diagnostic) return NULL;
			call->range = tabula_join_ranges(expression->range, closing.range);
			expression = call;
		} else {
			break;
		}
	}
	return expression;
}

static bool tabula_node_is_path(const Tabula_Node *node)
{
	if (!node) return false;
	if (node->kind == TABULA_NODE_NAME) return true;
	return node->kind == TABULA_NODE_MEMBER &&
		tabula_node_is_path(node->as.member.object);
}

static Tabula_Node *tabula_parse_statement(Tabula_Parser *parser)
{
	Tabula_Node *left = tabula_parse_expression(parser);
	if (!left) return NULL;

	if (parser->current.type == TABULA_TOKEN_DECLARE) {
		Tabula_Token operation = parser->current;
		tabula_parser_advance(parser);
		if (left->kind != TABULA_NODE_NAME) {
			tabula_parser_error(parser, left->range,
				"the left side of ':=' must be a single identifier");
			return NULL;
		}
		Tabula_Node *value = tabula_parse_expression(parser);
		if (!value) return NULL;
		Tabula_Node *declaration = tabula_new_node(parser, TABULA_NODE_DECLARE,
			tabula_join_ranges(left->range, value->range));
		if (!declaration) return NULL;
		(void)operation;
		declaration->as.assign.target = left;
		declaration->as.assign.value = value;
		return declaration;
	}

	if (parser->current.type == TABULA_TOKEN_EQUAL) {
		tabula_parser_advance(parser);
		if (!tabula_node_is_path(left)) {
			tabula_parser_error(parser, left->range,
				"the left side of '=' must be an identifier path");
			return NULL;
		}
		Tabula_Node *value = tabula_parse_expression(parser);
		if (!value) return NULL;
		Tabula_Node *assignment = tabula_new_node(parser, TABULA_NODE_ASSIGN,
			tabula_join_ranges(left->range, value->range));
		if (!assignment) return NULL;
		assignment->as.assign.target = left;
		assignment->as.assign.value = value;
		return assignment;
	}

	return left;
}

Tabula_ParseResult tabula_parse(Tabula_Context *context, Tabula_Source source)
{
	Tabula_ParseResult result;
	memset(&result, 0, sizeof(result));
	if (!context) return result;
	if ((!source.name.data && source.name.length) ||
		(!source.text.data && source.text.length)) {
		Tabula_SourceRange range = { 0 };
		result.diagnostics = tabula_diagnostic(context, TABULA_DIAGNOSTIC_PARSE,
			range, "source strings have a null data pointer with nonzero length");
		result.diagnostic_count = result.diagnostics ? 1u : 0u;
		return result;
	}

	Tabula_Ast *ast = tabula_allocate(context, sizeof(*ast));
	if (!ast) return result;
	ast->context = context;
	ast->source.name = tabula_copy_string(context, source.name);
	ast->source.text = tabula_copy_string(context, source.text);
	if (context->out_of_memory) return result;

	Tabula_Parser parser;
	memset(&parser, 0, sizeof(parser));
	parser.context = context;
	parser.ast = ast;
	parser.position.line = 1u;
	parser.position.column = 1u;
	tabula_parser_advance(&parser);

	Tabula_SourceRange program_range = parser.current.range;
	Tabula_Node *program = tabula_new_node(&parser, TABULA_NODE_PROGRAM, program_range);
	ast->program = program;
	while (program && parser.current.type != TABULA_TOKEN_EOF && !parser.diagnostic) {
		while (tabula_parser_take(&parser, TABULA_TOKEN_SEMICOLON)) {}
		if (parser.current.type == TABULA_TOKEN_EOF) break;
		Tabula_Node *statement = tabula_parse_statement(&parser);
		if (!statement || !tabula_node_list_push(&parser, &program->as.list, statement)) break;
		program->range = tabula_join_ranges(program->range, statement->range);
		while (tabula_parser_take(&parser, TABULA_TOKEN_SEMICOLON)) {}
	}

	if (!parser.diagnostic && context->out_of_memory) {
		parser.diagnostic = tabula_diagnostic(context, TABULA_DIAGNOSTIC_PARSE,
			parser.current.range, "out of memory");
	}
	result.success = parser.diagnostic == NULL && !context->out_of_memory;
	result.ast = result.success ? ast : NULL;
	result.diagnostics = parser.diagnostic;
	result.diagnostic_count = parser.diagnostic ? 1u : 0u;
	return result;
}

typedef struct Tabula_LocalScope {
	struct Tabula_LocalScope *parent;
	Tabula_Table *values;
} Tabula_LocalScope;

struct Tabula_Evaluator {
	Tabula_Context *context;
	const Tabula_Ast *ast;
	const Tabula_Environment *environment;
	Tabula_Table *root;
	Tabula_Table *current_table;
	Tabula_LocalScope *scope;
	Tabula_Diagnostic *diagnostic;
};

static void tabula_eval_error(
	Tabula_Evaluator *evaluator, Tabula_SourceRange range, const char *format, ...)
{
	if (evaluator->diagnostic) return;
	va_list arguments;
	va_start(arguments, format);
	evaluator->diagnostic = tabula_diagnostic_v(
		evaluator->context, TABULA_DIAGNOSTIC_EVALUATE, range, format, arguments);
	va_end(arguments);
}

static const Tabula_Symbol *tabula_environment_find(
	const Tabula_Environment *environment, Tabula_String name)
{
	size_t index = tabula_environment_find_index(environment, name);
	return index == SIZE_MAX ? NULL : &environment->symbols[index];
}

static const Tabula_TableEntry *tabula_find_local(
	const Tabula_Evaluator *evaluator, Tabula_String name)
{
	for (Tabula_LocalScope *scope = evaluator->scope; scope; scope = scope->parent) {
		const Tabula_TableEntry *entry = tabula_table_get_entry(scope->values, name);
		if (entry) return entry;
	}
	return NULL;
}

static const Tabula_Value *tabula_find_language_value(
	const Tabula_Evaluator *evaluator, Tabula_String name)
{
	const Tabula_TableEntry *local = tabula_find_local(evaluator, name);
	if (local) return &local->value;
	const Tabula_Value *value = tabula_table_get(evaluator->current_table, name);
	if (value) return value;
	if (evaluator->current_table != evaluator->root) {
		value = tabula_table_get(evaluator->root, name);
		if (value) return value;
	}
	const Tabula_Symbol *symbol = tabula_environment_find(evaluator->environment, name);
	if (symbol && symbol->kind == TABULA_SYMBOL_CONSTANT) return &symbol->as.constant;
	return NULL;
}

static bool tabula_eval_node(
	Tabula_Evaluator *evaluator, const Tabula_Node *node, Tabula_Value *result);

static bool tabula_eval_member(
	Tabula_Evaluator *evaluator, const Tabula_Node *node, Tabula_Value *result)
{
	Tabula_Value object;
	if (!tabula_eval_node(evaluator, node->as.member.object, &object)) return false;
	if (object.type != TABULA_VALUE_TABLE) {
		tabula_eval_error(evaluator, node->as.member.object->range,
			"expected table before '.', got %s", tabula_value_type_name(object.type));
		return false;
	}
	const Tabula_Value *value = tabula_table_get(object.as.table, node->as.member.name);
	if (!value) {
		tabula_eval_error(evaluator, node->as.member.name_range,
			"unknown field '%.*s'", (int)node->as.member.name.length,
			node->as.member.name.data);
		return false;
	}
	*result = *value;
	return true;
}

static bool tabula_call_result_is_valid(
	Tabula_Evaluator *evaluator, Tabula_Value value, Tabula_SourceRange range)
{
	if (!tabula_value_is_valid(value)) {
		tabula_eval_error(evaluator, range, "host function returned an invalid value type");
		return false;
	}
	if (value.type == TABULA_VALUE_TABLE &&
		(!value.as.table || value.as.table->context != evaluator->context)) {
		tabula_eval_error(evaluator, range,
			"host function returned a table owned by another context");
		return false;
	}
	if (value.type == TABULA_VALUE_STRING &&
		(!value.as.string.data && value.as.string.length)) {
		tabula_eval_error(evaluator, range, "host function returned an invalid string");
		return false;
	}
	return true;
}

static bool tabula_eval_call(
	Tabula_Evaluator *evaluator, const Tabula_Node *node, Tabula_Value *result)
{
	if (node->as.call.callee->kind != TABULA_NODE_NAME) {
		Tabula_Value callee;
		if (!tabula_eval_node(evaluator, node->as.call.callee, &callee)) return false;
		tabula_eval_error(evaluator, node->as.call.callee->range,
			"values of type %s are not callable", tabula_value_type_name(callee.type));
		return false;
	}

	Tabula_String name = node->as.call.callee->as.name;
	if (tabula_find_language_value(evaluator, name)) {
		tabula_eval_error(evaluator, node->as.call.callee->range,
			"'%.*s' is a value, not a function", (int)name.length, name.data);
		return false;
	}
	const Tabula_Symbol *symbol = tabula_environment_find(evaluator->environment, name);
	if (!symbol) {
		tabula_eval_error(evaluator, node->as.call.callee->range,
			"unknown function '%.*s'", (int)name.length, name.data);
		return false;
	}
	if (symbol->kind != TABULA_SYMBOL_FUNCTION) {
		tabula_eval_error(evaluator, node->as.call.callee->range,
			"'%.*s' is not callable", (int)name.length, name.data);
		return false;
	}

	const Tabula_FunctionDesc *function = &symbol->as.function;
	size_t count = node->as.call.arguments.count;
	if (count < function->minimum_arguments ||
		(function->maximum_arguments != TABULA_ANY_ARITY &&
		 count > function->maximum_arguments)) {
		if (function->minimum_arguments == function->maximum_arguments) {
			tabula_eval_error(evaluator, node->range,
				"function '%.*s' expects %zu argument%s, got %zu",
				(int)name.length, name.data, function->minimum_arguments,
				function->minimum_arguments == 1u ? "" : "s", count);
		} else {
			tabula_eval_error(evaluator, node->range,
				"function '%.*s' received an invalid argument count",
				(int)name.length, name.data);
		}
		return false;
	}

	Tabula_Value *arguments = NULL;
	if (count) {
		arguments = tabula_allocate(evaluator->context, count * sizeof(*arguments));
		if (!arguments) {
			tabula_eval_error(evaluator, node->range, "out of memory");
			return false;
		}
	}
	for (size_t index = 0; index < count; ++index) {
		if (!tabula_eval_node(evaluator, node->as.call.arguments.items[index],
			&arguments[index])) return false;
	}

	Tabula_Call call;
	memset(&call, 0, sizeof(call));
	call.context = evaluator->context;
	call.evaluator = evaluator;
	call.user_data = function->user_data;
	call.range = node->range;
	Tabula_Value function_result = tabula_value_null();
	b32 callback_success = function->function(&call, arguments, count, &function_result);
	if (evaluator->diagnostic) return false;
	if (!callback_success) {
		tabula_eval_error(evaluator, node->range, "function '%.*s' failed",
			(int)name.length, name.data);
		return false;
	}
	if (!tabula_call_result_is_valid(evaluator, function_result, node->range))
		return false;
	if (function_result.type == TABULA_VALUE_STRING) {
		function_result = tabula_value_string(evaluator->context, function_result.as.string);
		if (evaluator->context->out_of_memory) {
			tabula_eval_error(evaluator, node->range, "out of memory");
			return false;
		}
	}
	*result = function_result;
	return true;
}

static bool tabula_ensure_assignment_table(
	Tabula_Evaluator *evaluator,
	Tabula_Table *base,
	const Tabula_Node *path,
	Tabula_Table **result)
{
	if (path->kind == TABULA_NODE_NAME) {
		const Tabula_Value *existing = tabula_table_get(base, path->as.name);
		if (!existing) {
			Tabula_Table *created = tabula_table_create(evaluator->context);
			if (!created) {
				tabula_eval_error(evaluator, path->range, "out of memory");
				return false;
			}
			Tabula_Value value = { TABULA_VALUE_TABLE, { 0 } };
			value.as.table = created;
			if (!tabula_table_set_ranges(
				base, path->as.name, value, path->range, path->range)) return false;
			*result = created;
			return true;
		}
		if (existing->type != TABULA_VALUE_TABLE) {
			tabula_eval_error(evaluator, path->range,
				"cannot extend '%.*s': expected table, got %s",
				(int)path->as.name.length, path->as.name.data,
				tabula_value_type_name(existing->type));
			return false;
		}
		*result = existing->as.table;
		return true;
	}

	Tabula_Table *parent;
	if (!tabula_ensure_assignment_table(
		evaluator, base, path->as.member.object, &parent)) return false;
	const Tabula_Value *existing = tabula_table_get(parent, path->as.member.name);
	if (!existing) {
		Tabula_Table *created = tabula_table_create(evaluator->context);
		if (!created) {
			tabula_eval_error(evaluator, path->range, "out of memory");
			return false;
		}
		Tabula_Value value = { TABULA_VALUE_TABLE, { 0 } };
		value.as.table = created;
		if (!tabula_table_set_ranges(parent, path->as.member.name, value,
			path->as.member.name_range, path->range)) return false;
		*result = created;
		return true;
	}
	if (existing->type != TABULA_VALUE_TABLE) {
		tabula_eval_error(evaluator, path->as.member.name_range,
			"cannot extend '%.*s': expected table, got %s",
			(int)path->as.member.name.length, path->as.member.name.data,
			tabula_value_type_name(existing->type));
		return false;
	}
	*result = existing->as.table;
	return true;
}

static bool tabula_eval_assignment(
	Tabula_Evaluator *evaluator,
	const Tabula_Node *node,
	Tabula_Table *base,
	Tabula_Value *result)
{
	Tabula_Value value;
	if (!tabula_eval_node(evaluator, node->as.assign.value, &value)) return false;
	const Tabula_Node *target = node->as.assign.target;
	if (target->kind == TABULA_NODE_NAME) {
		if (!tabula_table_set_ranges(base, target->as.name, value,
			target->range, node->as.assign.value->range)) {
			tabula_eval_error(evaluator, target->range, "failed to assign field");
			return false;
		}
	} else {
		Tabula_Table *parent;
		if (!tabula_ensure_assignment_table(
			evaluator, base, target->as.member.object, &parent)) return false;
		if (!tabula_table_set_ranges(parent, target->as.member.name, value,
			target->as.member.name_range, node->as.assign.value->range)) {
			tabula_eval_error(evaluator, target->range, "failed to assign field");
			return false;
		}
	}
	*result = value;
	return true;
}

static bool tabula_eval_declaration(
	Tabula_Evaluator *evaluator, const Tabula_Node *node, Tabula_Value *result)
{
	Tabula_String name = node->as.assign.target->as.name;
	if (tabula_table_get(evaluator->scope->values, name)) {
		tabula_eval_error(evaluator, node->as.assign.target->range,
			"local '%.*s' is already declared in this scope",
			(int)name.length, name.data);
		return false;
	}
	Tabula_Value value;
	if (!tabula_eval_node(evaluator, node->as.assign.value, &value)) return false;
	if (!tabula_table_set_ranges(evaluator->scope->values, name, value,
		node->as.assign.target->range, node->as.assign.value->range)) {
		tabula_eval_error(evaluator, node->range, "failed to declare local");
		return false;
	}
	*result = value;
	return true;
}

static bool tabula_eval_table_literal(
	Tabula_Evaluator *evaluator, const Tabula_Node *node, Tabula_Value *result)
{
	Tabula_Table *table = tabula_table_create(evaluator->context);
	Tabula_Table *locals = tabula_table_create(evaluator->context);
	if (!table || !locals) {
		tabula_eval_error(evaluator, node->range, "out of memory");
		return false;
	}
	Tabula_LocalScope scope;
	scope.parent = evaluator->scope;
	scope.values = locals;
	Tabula_LocalScope *old_scope = evaluator->scope;
	Tabula_Table *old_current = evaluator->current_table;
	evaluator->scope = &scope;
	evaluator->current_table = table;

	Tabula_Value ignored = tabula_value_null();
	for (size_t index = 0; index < node->as.list.count; ++index) {
		const Tabula_Node *statement = node->as.list.items[index];
		bool success = statement->kind == TABULA_NODE_DECLARE
			? tabula_eval_declaration(evaluator, statement, &ignored)
			: tabula_eval_assignment(evaluator, statement, table, &ignored);
		if (!success) {
			evaluator->scope = old_scope;
			evaluator->current_table = old_current;
			return false;
		}
	}
	evaluator->scope = old_scope;
	evaluator->current_table = old_current;
	result->type = TABULA_VALUE_TABLE;
	result->as.table = table;
	return true;
}

static bool tabula_eval_node(
	Tabula_Evaluator *evaluator, const Tabula_Node *node, Tabula_Value *result)
{
	if (!node || evaluator->diagnostic) return false;
	switch (node->kind) {
	case TABULA_NODE_LITERAL:
		*result = node->as.literal;
		return true;
	case TABULA_NODE_NAME: {
		const Tabula_Value *value = tabula_find_language_value(evaluator, node->as.name);
		if (value) {
			*result = *value;
			return true;
		}
		const Tabula_Symbol *symbol =
			tabula_environment_find(evaluator->environment, node->as.name);
		if (symbol && symbol->kind == TABULA_SYMBOL_FUNCTION) {
			tabula_eval_error(evaluator, node->range,
				"function '%.*s' must be called",
				(int)node->as.name.length, node->as.name.data);
		} else {
			tabula_eval_error(evaluator, node->range,
				"unknown identifier '%.*s'",
				(int)node->as.name.length, node->as.name.data);
		}
		return false;
	}
	case TABULA_NODE_MEMBER:
		return tabula_eval_member(evaluator, node, result);
	case TABULA_NODE_CALL:
		return tabula_eval_call(evaluator, node, result);
	case TABULA_NODE_TABLE:
		return tabula_eval_table_literal(evaluator, node, result);
	case TABULA_NODE_DECLARE:
		return tabula_eval_declaration(evaluator, node, result);
	case TABULA_NODE_ASSIGN:
		return tabula_eval_assignment(
			evaluator, node, evaluator->current_table, result);
	case TABULA_NODE_PROGRAM:
		for (size_t index = 0; index < node->as.list.count; ++index) {
			if (!tabula_eval_node(evaluator, node->as.list.items[index], result))
				return false;
		}
		return true;
	}
	return false;
}

void *tabula_call_user_data(const Tabula_Call *call)
{
	return call ? call->user_data : NULL;
}

Tabula_Context *tabula_call_context(const Tabula_Call *call)
{
	return call ? call->context : NULL;
}

Tabula_SourceRange tabula_call_range(const Tabula_Call *call)
{
	Tabula_SourceRange range = { 0 };
	return call ? call->range : range;
}

void tabula_call_error(Tabula_Call *call, Tabula_String message)
{
	if (!call || !call->evaluator || call->evaluator->diagnostic) return;
	tabula_eval_error(call->evaluator, call->range, "%.*s",
		(int)message.length, message.data ? message.data : "");
}

Tabula_EvalResult tabula_evaluate(
	Tabula_Context *context,
	const Tabula_Ast *ast,
	const Tabula_Environment *environment)
{
	Tabula_EvalResult result;
	memset(&result, 0, sizeof(result));
	result.value = tabula_value_null();
	if (!context || !ast || !ast->program) return result;

	Tabula_Evaluator evaluator;
	memset(&evaluator, 0, sizeof(evaluator));
	evaluator.context = context;
	evaluator.ast = ast;
	evaluator.environment = environment;
	if (ast->context != context) {
		tabula_eval_error(&evaluator, ast->program->range,
			"AST belongs to another context");
	} else if (environment && environment->context != context) {
		tabula_eval_error(&evaluator, ast->program->range,
			"environment belongs to another context");
	}

	if (!evaluator.diagnostic) {
		evaluator.root = tabula_table_create(context);
		Tabula_Table *locals = tabula_table_create(context);
		if (!evaluator.root || !locals) {
			tabula_eval_error(&evaluator, ast->program->range, "out of memory");
		} else {
			Tabula_LocalScope scope = { NULL, locals };
			evaluator.scope = &scope;
			evaluator.current_table = evaluator.root;
			Tabula_Value ignored = tabula_value_null();
			if (tabula_eval_node(&evaluator, ast->program, &ignored)) {
				result.value.type = TABULA_VALUE_TABLE;
				result.value.as.table = evaluator.root;
			}
		}
	}

	result.success = evaluator.diagnostic == NULL && !context->out_of_memory;
	if (!result.success) result.value = tabula_value_null();
	result.diagnostics = evaluator.diagnostic;
	result.diagnostic_count = evaluator.diagnostic ? 1u : 0u;
	return result;
}
