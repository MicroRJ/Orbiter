#include "tabula/tabula.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_failures;

#define CHECK(condition_) do { \
	if (!(condition_)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition_); \
		++test_failures; \
	} \
} while (0)

typedef struct CountingAllocator {
	size_t allocations;
	size_t deallocations;
} CountingAllocator;

typedef struct FailingAllocator {
	size_t calls;
	size_t fail_at;
	size_t allocations;
	size_t deallocations;
} FailingAllocator;

static void *counting_allocate(void *user_data, size_t size)
{
	CountingAllocator *allocator = user_data;
	++allocator->allocations;
	return malloc(size);
}

static void counting_deallocate(void *user_data, void *memory)
{
	CountingAllocator *allocator = user_data;
	++allocator->deallocations;
	free(memory);
}

static void *failing_allocate(void *user_data, size_t size)
{
	FailingAllocator *allocator = user_data;
	++allocator->calls;
	if (allocator->calls == allocator->fail_at) return NULL;
	void *memory = malloc(size);
	if (memory) ++allocator->allocations;
	return memory;
}

static void failing_deallocate(void *user_data, void *memory)
{
	FailingAllocator *allocator = user_data;
	++allocator->deallocations;
	free(memory);
}

static b32 string_equal(Tabula_String string, const char *text)
{
	size_t length = strlen(text);
	return string.length == length &&
		(length == 0 || memcmp(string.data, text, length) == 0);
}

static b32 message_contains(const Tabula_Diagnostic *diagnostic, const char *text)
{
	if (!diagnostic) return 0;
	size_t length = strlen(text);
	if (length > diagnostic->message.length) return 0;
	for (size_t index = 0; index + length <= diagnostic->message.length; ++index) {
		if (memcmp(diagnostic->message.data + index, text, length) == 0) return 1;
	}
	return 0;
}

static Tabula_ParseResult parse_text(Tabula_Context *context, const char *text)
{
	Tabula_Source source = {
		TABULA_STRING_LITERAL("test.tab"),
		{ text, strlen(text) },
	};
	return tabula_parse(context, source);
}

static const Tabula_Value *field(const Tabula_Table *table, const char *name)
{
	return tabula_table_get(table, (Tabula_String){ name, strlen(name) });
}

static b32 add_integers(
	Tabula_Call *call,
	const Tabula_Value *arguments,
	size_t argument_count,
	Tabula_Value *result)
{
	CHECK(tabula_call_context(call) != NULL);
	CHECK(tabula_call_user_data(call) == (void *)0x1234);
	CHECK(tabula_call_range(call).line != 0);
	if (argument_count != 2u ||
		arguments[0].type != TABULA_VALUE_INTEGER ||
		arguments[1].type != TABULA_VALUE_INTEGER) {
		tabula_call_error(call, TABULA_STRING_LITERAL("add expects two integers"));
		return 0;
	}
	*result = tabula_value_integer(
		arguments[0].as.integer + arguments[1].as.integer);
	return 1;
}

static void test_successful_program(Tabula_Context *context)
{
	Tabula_Environment *environment = tabula_environment_create(context);
	CHECK(environment != NULL);
	CHECK(tabula_environment_add_constant(environment,
		TABULA_STRING_LITERAL("BASE"), tabula_value_integer(40)));
	Tabula_FunctionDesc add = {
		TABULA_STRING_LITERAL("add"), 2u, 2u, add_integers, (void *)0x1234,
	};
	CHECK(tabula_environment_add_function(environment, &add));

	const char *text =
		"// locals are immutable and do not appear in the result\n"
		"base := BASE\n"
		"accent := #ff00aa80\n"
		"theme.editor.text_size = add(base, 0b10)\n"
		"theme.editor.opacity = 50%\n"
		"theme.editor.color = accent\n"
		"theme.flags.enabled = true\n"
		"theme.flags.unset = null\n"
		"strings.path = 'data\\tables\\default.tab'\n"
		"nested = { local := 7; a = local; deep.answer = 0h2a; }\n";
	Tabula_ParseResult parsed = parse_text(context, text);
	CHECK(parsed.success);
	CHECK(parsed.ast != NULL);
	CHECK(parsed.diagnostic_count == 0u);

	Tabula_EvalResult evaluated = tabula_evaluate(context, parsed.ast, environment);
	CHECK(evaluated.success);
	CHECK(evaluated.value.type == TABULA_VALUE_TABLE);
	Tabula_Table *root = evaluated.value.as.table;
	CHECK(tabula_table_count(root) == 3u);
	CHECK(field(root, "base") == NULL);
	CHECK(field(root, "accent") == NULL);

	const Tabula_TableEntry *first = tabula_table_entry_at(root, 0u);
	const Tabula_TableEntry *second = tabula_table_entry_at(root, 1u);
	const Tabula_TableEntry *third = tabula_table_entry_at(root, 2u);
	CHECK(first && string_equal(first->key, "theme"));
	CHECK(second && string_equal(second->key, "strings"));
	CHECK(third && string_equal(third->key, "nested"));

	const Tabula_Value *theme_value = field(root, "theme");
	CHECK(theme_value && theme_value->type == TABULA_VALUE_TABLE);
	const Tabula_Value *editor_value = field(theme_value->as.table, "editor");
	CHECK(editor_value && editor_value->type == TABULA_VALUE_TABLE);
	const Tabula_Value *text_size = field(editor_value->as.table, "text_size");
	const Tabula_Value *opacity = field(editor_value->as.table, "opacity");
	const Tabula_Value *color = field(editor_value->as.table, "color");
	CHECK(text_size && text_size->type == TABULA_VALUE_INTEGER);
	CHECK(text_size && text_size->as.integer == 42);
	CHECK(opacity && opacity->type == TABULA_VALUE_NUMBER);
	CHECK(opacity && opacity->as.number > 0.499 && opacity->as.number < 0.501);
	CHECK(color && color->type == TABULA_VALUE_COLOR);
	CHECK(color && color->as.color.r == 255 && color->as.color.g == 0 &&
		color->as.color.b == 170 && color->as.color.a == 128);

	const Tabula_Value *flags = field(theme_value->as.table, "flags");
	CHECK(flags && flags->type == TABULA_VALUE_TABLE);
	const Tabula_Value *enabled = field(flags->as.table, "enabled");
	const Tabula_Value *unset = field(flags->as.table, "unset");
	CHECK(enabled && enabled->type == TABULA_VALUE_BOOLEAN && enabled->as.boolean);
	CHECK(unset && unset->type == TABULA_VALUE_NULL);

	const Tabula_Value *strings = field(root, "strings");
	const Tabula_Value *path = strings ? field(strings->as.table, "path") : NULL;
	CHECK(path && path->type == TABULA_VALUE_STRING);
	CHECK(path && string_equal(path->as.string, "data\\tables\\default.tab"));

	const Tabula_Value *nested = field(root, "nested");
	CHECK(nested && nested->type == TABULA_VALUE_TABLE);
	CHECK(nested && field(nested->as.table, "local") == NULL);
	const Tabula_Value *a = nested ? field(nested->as.table, "a") : NULL;
	CHECK(a && a->type == TABULA_VALUE_INTEGER && a->as.integer == 7);
	const Tabula_Value *deep = nested ? field(nested->as.table, "deep") : NULL;
	const Tabula_Value *answer = deep ? field(deep->as.table, "answer") : NULL;
	CHECK(answer && answer->type == TABULA_VALUE_INTEGER && answer->as.integer == 42);

	const Tabula_TableEntry *text_size_entry =
		tabula_table_get_entry(editor_value->as.table, TABULA_STRING_LITERAL("text_size"));
	CHECK(text_size_entry && text_size_entry->value_range.line == 4u);
	CHECK(text_size_entry && string_equal(
		text_size_entry->value_range.source_name, "test.tab"));
}

static void test_unknown_identifier(Tabula_Context *context)
{
	Tabula_ParseResult parsed = parse_text(context, "answer = missing");
	CHECK(parsed.success);
	Tabula_EvalResult evaluated = tabula_evaluate(context, parsed.ast, NULL);
	CHECK(!evaluated.success);
	CHECK(evaluated.diagnostic_count == 1u);
	CHECK(evaluated.diagnostics[0].stage == TABULA_DIAGNOSTIC_EVALUATE);
	CHECK(evaluated.diagnostics[0].range.line == 1u);
	CHECK(message_contains(evaluated.diagnostics, "unknown identifier"));
}

static void test_function_type_error(Tabula_Context *context)
{
	Tabula_Environment *environment = tabula_environment_create(context);
	Tabula_FunctionDesc add = {
		TABULA_STRING_LITERAL("add"), 2u, 2u, add_integers, (void *)0x1234,
	};
	CHECK(tabula_environment_add_function(environment, &add));
	Tabula_ParseResult parsed = parse_text(context, "answer = add('forty', 2)");
	CHECK(parsed.success);
	Tabula_EvalResult evaluated = tabula_evaluate(context, parsed.ast, environment);
	CHECK(!evaluated.success);
	CHECK(message_contains(evaluated.diagnostics, "expects two integers"));
}

static void test_assignment_type_error(Tabula_Context *context)
{
	Tabula_ParseResult parsed = parse_text(context, "item = 1; item.child = 2");
	CHECK(parsed.success);
	Tabula_EvalResult evaluated = tabula_evaluate(context, parsed.ast, NULL);
	CHECK(!evaluated.success);
	CHECK(message_contains(evaluated.diagnostics, "expected table"));
}

static void test_parse_errors(Tabula_Context *context)
{
	Tabula_ParseResult bad_color = parse_text(context, "color = #123");
	CHECK(!bad_color.success);
	CHECK(bad_color.diagnostic_count == 1u);
	CHECK(bad_color.diagnostics[0].stage == TABULA_DIAGNOSTIC_PARSE);
	CHECK(message_contains(bad_color.diagnostics, "6 or 8"));

	Tabula_ParseResult dotted_local = parse_text(context, "a.b := 1");
	CHECK(!dotted_local.success);
	CHECK(message_contains(dotted_local.diagnostics, "single identifier"));

	Tabula_ParseResult at_local = parse_text(context, "@old = 1");
	CHECK(!at_local.success);
	CHECK(message_contains(at_local.diagnostics, "unrecognized"));
}

static void test_immutable_local(Tabula_Context *context)
{
	Tabula_ParseResult parsed = parse_text(context, "value := 1; value := 2");
	CHECK(parsed.success);
	Tabula_EvalResult evaluated = tabula_evaluate(context, parsed.ast, NULL);
	CHECK(!evaluated.success);
	CHECK(message_contains(evaluated.diagnostics, "already declared"));
}

static void test_local_and_global_are_distinct(Tabula_Context *context)
{
	Tabula_ParseResult parsed = parse_text(context,
		"value = 1; value := 2; observed = value");
	CHECK(parsed.success);
	Tabula_EvalResult evaluated = tabula_evaluate(context, parsed.ast, NULL);
	CHECK(evaluated.success);
	const Tabula_Value *global = field(evaluated.value.as.table, "value");
	const Tabula_Value *observed = field(evaluated.value.as.table, "observed");
	CHECK(global && global->type == TABULA_VALUE_INTEGER && global->as.integer == 1);
	CHECK(observed && observed->type == TABULA_VALUE_INTEGER && observed->as.integer == 2);
}

static void test_embedded_nul_is_not_eof(Tabula_Context *context)
{
	const char text[] = { 'a', ' ', '=', ' ', '1', 0, 'b', ' ', '=', ' ', '2' };
	Tabula_ParseResult parsed = tabula_parse(context, (Tabula_Source) {
		TABULA_STRING_LITERAL("nul.tab"),
		{ text, sizeof(text) },
	});
	CHECK(!parsed.success);
	CHECK(message_contains(parsed.diagnostics, "NUL byte"));
}

static void test_allocation_failures_are_observable(void)
{
	b32 reached_success = 0;
	for (size_t fail_at = 1; fail_at < 128u && !reached_success; ++fail_at)
	{
		FailingAllocator allocator = { 0 };
		allocator.fail_at = fail_at;
		Tabula_ContextDesc description = {
			{ failing_allocate, failing_deallocate, &allocator },
		};
		Tabula_Context *context = tabula_context_create(&description);
		if (!context) {
			CHECK(fail_at == 1u);
			continue;
		}

		Tabula_ParseResult parsed = parse_text(context, "value = 42");
		Tabula_EvalResult evaluated = { 0 };
		if (parsed.success) evaluated = tabula_evaluate(context, parsed.ast, NULL);
		reached_success = parsed.success && evaluated.success;
		CHECK(reached_success || tabula_context_out_of_memory(context));
		if (tabula_context_out_of_memory(context)) {
			Tabula_ParseResult again = parse_text(context, "other = 1");
			CHECK(!again.success);
		}
		tabula_context_destroy(context);
		CHECK(allocator.allocations == allocator.deallocations);
	}
	CHECK(reached_success);
}

int main(void)
{
	CountingAllocator counts = { 0 };
	Tabula_ContextDesc description = {
		{ counting_allocate, counting_deallocate, &counts },
	};
	Tabula_Context *context = tabula_context_create(&description);
	CHECK(context != NULL);

	test_successful_program(context);
	test_unknown_identifier(context);
	test_function_type_error(context);
	test_assignment_type_error(context);
	test_parse_errors(context);
	test_immutable_local(context);
	test_local_and_global_are_distinct(context);
	test_embedded_nul_is_not_eof(context);
	test_allocation_failures_are_observable();

	tabula_context_destroy(context);
	CHECK(counts.allocations == counts.deallocations);
	if (test_failures) {
		fprintf(stderr, "%d Tabula test(s) failed\n", test_failures);
		return 1;
	}
	printf("Tabula tests passed (%zu allocations released)\n", counts.allocations);
	return 0;
}
