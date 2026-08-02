#ifndef BASE_H
#define BASE_H

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <string.h>
#include <setjmp.h>
#include <math.h>
#include <stdarg.h>
#include <assert.h>
#include "platform.h"

typedef signed long long int i64;
typedef int                  i32;
typedef signed short         i16;
typedef signed char          i8;
typedef unsigned long long   u64;
typedef unsigned int         u32;
typedef unsigned short       u16;
typedef unsigned char        u8;
typedef float                f32;
typedef double               f64;
typedef i32                  b32;
typedef u32                  Rune;

typedef struct
{
	u8 *data;
	u64 size;
}
ByteSpan;

static inline ByteSpan byte_span(void *data, u64 size)
{
	return (ByteSpan) { data, size };
}

#define ArrayCount(array) (sizeof(array) / sizeof((array)[0]))
#define Min(a, b) (((a) < (b)) ? (a) : (b))
#define Max(a, b) (((a) > (b)) ? (a) : (b))
#define global static
#define thread_decl __declspec(thread)
#define APIFUNC

static void memory_copy(void *destination, const void *source, u64 size);
static void memory_zero(void *destination, u64 size);
static void memory_fill(void *destination, u8 byte, u64 size);

typedef struct
{
	f64 seconds;
}
Seconds;

static inline Seconds seconds_now(void)
{
	u64 frequency = platform_counter_frequency();
	return (Seconds) { frequency ? platform_counter() / (f64)frequency : 0.0 };
}

#define NANOSECONDS_PER_SECOND (1e9)
#define NANOSECONDS_TO_SECONDS (1.0 / NANOSECONDS_PER_SECOND)
#define MAX_VALUE_U8  ((u8)~0)
#define MAX_VALUE_U16 ((u16)~0)
#define MAX_VALUE_U32 ((u32)~0)
#define U64FromU32Packed(high, low) (((u64)(high) << 32) | ((u64)(low) & MAX_VALUE_U32))

#define STATIC_ASSERT(x) _Static_assert(x, "")
void assertion_failed(const char *file, int line, const char *expression);
#define Assert(expression) do { if (!(expression)) assertion_failed(__FILE__, __LINE__, #expression); } while (0)


#define KB_MASK ((1 << 10) - 1)

#define KiB(value) ((u64)(value) << 10)
#define MB(value) ((u64)(value) << 20)

#define CONCAT_(A,B) A##B
#define CONCAT(A,B) CONCAT_(A,B)

#define ABS(x) (((x) < 0) ? -(x) : (x))
#define SIGN(x) ((x) < 0 ? -1 : (x) > 0 ? +1 : 0)
#define CLAMP(x, y, z) Min(Max(x, y), z)




enum { true = 1, false = 0 };


enum {
	CORNER_TOP_L = 0,
	CORNER_BOT_L = 1,
	CORNER_TOP_R = 2,
	CORNER_BOT_R = 3,
};

// MUST BE THIS ORDER
typedef enum {
	AXIS_X = 0, AXIS_Y = 1
} AXIS;

#include "base_vectors.h"
#include "base_rect.h"
#include "base_stream.h"
#include "base_hash.h"
#include "base_arena.h"
#include "base_string.h"
#include "base_temp.h"
#include "base_logger.h"
#include "base_profiler.h"
#include "base_image.h"

static b32 memory_match(const void *left, const void *right, u64 size)
{
	prof_add_metric(PROF_METRIC_COPY_MEMORY_CALLS, 1);
	prof_add_metric(PROF_METRIC_COPY_MEMORY_SIZE, (i64)size);
	return memcmp(left, right, size) == 0;
}

static void memory_copy(void *dest, const void *source, u64 size)
{
	prof_add_metric(PROF_METRIC_COPY_MEMORY_CALLS, 1);
	prof_add_metric(PROF_METRIC_COPY_MEMORY_SIZE, (i64)size);
	memcpy(dest, source, size);
}

static void memory_zero(void *dest, u64 size)
{
	prof_add_metric(PROF_METRIC_ZERO_MEMORY_CALLS, 1);
	prof_add_metric(PROF_METRIC_ZERO_MEMORY_SIZE, (i64)size);
	memset(dest, 0, size);
}

static void memory_fill(void *dest, u8 byte, u64 size)
{
	prof_add_metric(PROF_METRIC_FILL_MEMORY_CALLS, 1);
	prof_add_metric(PROF_METRIC_FILL_MEMORY_SIZE, (i64)size);
	memset(dest, byte, size);
}

#endif
