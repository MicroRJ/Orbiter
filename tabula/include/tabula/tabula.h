#ifndef TABULA_TABULA_H
#define TABULA_TABULA_H

#include <stddef.h>
#include <stdint.h>

#ifndef TABULA_USE_EXTERNAL_TYPES
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef float f32;
typedef double f64;
typedef i32 b32;
#endif

#if defined(__cplusplus)
#define TABULA_STATIC_ASSERT_(condition_, message_) static_assert(condition_, message_)
#else
#define TABULA_STATIC_ASSERT_(condition_, message_) _Static_assert(condition_, message_)
#endif
TABULA_STATIC_ASSERT_(sizeof(u8) == 1, "Tabula requires an 8-bit u8");
TABULA_STATIC_ASSERT_(sizeof(u16) == 2, "Tabula requires a 16-bit u16");
TABULA_STATIC_ASSERT_(sizeof(u32) == 4, "Tabula requires a 32-bit u32");
TABULA_STATIC_ASSERT_(sizeof(u64) == 8, "Tabula requires a 64-bit u64");
TABULA_STATIC_ASSERT_(sizeof(i64) == 8 && (i64)-1 < 0, "Tabula requires a signed 64-bit i64");
TABULA_STATIC_ASSERT_(sizeof(f32) == 4, "Tabula requires a 32-bit f32");
TABULA_STATIC_ASSERT_(sizeof(f64) == 8, "Tabula requires a 64-bit f64");
TABULA_STATIC_ASSERT_(sizeof(b32) == 4 && (b32)-1 < 0, "Tabula requires a signed 32-bit b32");
#undef TABULA_STATIC_ASSERT_

#ifdef __cplusplus
extern "C" {
#endif

#define TABULA_STRING_LITERAL(text_) \
	((Tabula_String){ (text_), sizeof(text_) - 1u })
#define TABULA_ANY_ARITY ((size_t)-1)

typedef struct Tabula_Context Tabula_Context;
typedef struct Tabula_Ast Tabula_Ast;
typedef struct Tabula_Table Tabula_Table;
typedef struct Tabula_Environment Tabula_Environment;
typedef struct Tabula_Call Tabula_Call;

typedef struct Tabula_String {
	const char *data;
	size_t length;
} Tabula_String;

typedef void *(*Tabula_AllocateFn)(void *user_data, size_t size);
typedef void (*Tabula_DeallocateFn)(void *user_data, void *memory);

typedef struct Tabula_Allocator {
	Tabula_AllocateFn allocate;
	Tabula_DeallocateFn deallocate;
	void *user_data;
} Tabula_Allocator;

typedef struct Tabula_ContextDesc {
	/* Both callbacks must be supplied, or neither. */
	Tabula_Allocator allocator;
} Tabula_ContextDesc;

typedef struct Tabula_Source {
	Tabula_String name;
	Tabula_String text;
} Tabula_Source;

typedef struct Tabula_SourceRange {
	Tabula_String source_name;
	size_t start;
	size_t end;
	size_t line;
	size_t column;
	size_t end_line;
	size_t end_column;
} Tabula_SourceRange;

typedef enum Tabula_DiagnosticStage {
	TABULA_DIAGNOSTIC_PARSE,
	TABULA_DIAGNOSTIC_EVALUATE,
} Tabula_DiagnosticStage;

typedef struct Tabula_Diagnostic {
	Tabula_DiagnosticStage stage;
	Tabula_SourceRange range;
	Tabula_String message;
} Tabula_Diagnostic;

typedef struct Tabula_Color {
	u8 r;
	u8 g;
	u8 b;
	u8 a;
} Tabula_Color;

typedef enum Tabula_ValueType {
	TABULA_VALUE_NULL,
	TABULA_VALUE_BOOLEAN,
	TABULA_VALUE_INTEGER,
	TABULA_VALUE_NUMBER,
	TABULA_VALUE_STRING,
	TABULA_VALUE_COLOR,
	TABULA_VALUE_TABLE,
} Tabula_ValueType;

typedef struct Tabula_Value {
	Tabula_ValueType type;
	union {
		b32 boolean;
		i64 integer;
		f64 number;
		Tabula_String string;
		Tabula_Color color;
		Tabula_Table *table;
	} as;
} Tabula_Value;

typedef struct Tabula_TableEntry {
	Tabula_String key;
	Tabula_Value value;
	Tabula_SourceRange key_range;
	Tabula_SourceRange value_range;
} Tabula_TableEntry;

typedef b32 (*Tabula_EagerFunctionFn)(
	Tabula_Call *call,
	const Tabula_Value *arguments,
	size_t argument_count,
	Tabula_Value *result);

typedef struct Tabula_FunctionDesc {
	Tabula_String name;
	size_t minimum_arguments;
	size_t maximum_arguments;
	Tabula_EagerFunctionFn function;
	void *user_data;
} Tabula_FunctionDesc;

typedef struct Tabula_ParseResult {
	const Tabula_Ast *ast;
	const Tabula_Diagnostic *diagnostics;
	size_t diagnostic_count;
	b32 success;
} Tabula_ParseResult;

typedef struct Tabula_EvalResult {
	Tabula_Value value;
	const Tabula_Diagnostic *diagnostics;
	size_t diagnostic_count;
	b32 success;
} Tabula_EvalResult;

Tabula_Context *tabula_context_create(const Tabula_ContextDesc *description);
void tabula_context_destroy(Tabula_Context *context);
/* Allocation failure is fail-stop. Destroy and recreate a failed context. */
b32 tabula_context_out_of_memory(const Tabula_Context *context);

Tabula_Value tabula_value_null(void);
Tabula_Value tabula_value_boolean(b32 value);
Tabula_Value tabula_value_integer(i64 value);
Tabula_Value tabula_value_number(f64 value);
Tabula_Value tabula_value_color(Tabula_Color value);
/* The returned string is copied into, and owned by, context. */
Tabula_Value tabula_value_string(Tabula_Context *context, Tabula_String value);

const char *tabula_value_type_name(Tabula_ValueType type);

Tabula_Table *tabula_table_create(Tabula_Context *context);
b32 tabula_table_set(Tabula_Table *table, Tabula_String key, Tabula_Value value);
const Tabula_Value *tabula_table_get(const Tabula_Table *table, Tabula_String key);
const Tabula_TableEntry *tabula_table_get_entry(
	const Tabula_Table *table, Tabula_String key);
size_t tabula_table_count(const Tabula_Table *table);
const Tabula_TableEntry *tabula_table_entry_at(
	const Tabula_Table *table, size_t index);

Tabula_Environment *tabula_environment_create(Tabula_Context *context);
b32 tabula_environment_add_constant(
	Tabula_Environment *environment, Tabula_String name, Tabula_Value value);
b32 tabula_environment_add_function(
	Tabula_Environment *environment, const Tabula_FunctionDesc *description);

void *tabula_call_user_data(const Tabula_Call *call);
Tabula_Context *tabula_call_context(const Tabula_Call *call);
Tabula_SourceRange tabula_call_range(const Tabula_Call *call);
/* Records the function's diagnostic. A false callback without one gets a generic error. */
void tabula_call_error(Tabula_Call *call, Tabula_String message);

Tabula_ParseResult tabula_parse(Tabula_Context *context, Tabula_Source source);
Tabula_EvalResult tabula_evaluate(
	Tabula_Context *context,
	const Tabula_Ast *ast,
	const Tabula_Environment *environment);

#ifdef __cplusplus
}
#endif

#endif
