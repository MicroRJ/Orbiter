#include "platform.h"

static char *arena_format_size(u64 size)
{
	u64 gigabytes = size >> 30;
	u64 megabytes = size >> 20 & KB_MASK;
	u64 kilobytes = size >> 10 & KB_MASK;
	u64 bytes = size & KB_MASK;
	return temp_format_("(size %llu: %llu GBs + %llu MBs + %llu KBs + %llu bytes)", size, gigabytes, megabytes, kilobytes, bytes);
}

static u64 arena_align_position(const Arena *arena, u64 position, u64 alignment)
{
	Assert(alignment && !(alignment & (alignment - 1)));
	u64 address = (u64)(uintptr_t)arena->memory + position;
	u64 aligned_address = (address + alignment - 1) & ~(alignment - 1);
	Assert(aligned_address >= address);
	return aligned_address - (u64)(uintptr_t)arena->memory;
}

static void arena_commit_through(Arena *arena, u64 end)
{
	if (end <= arena->committed_size) return;

	u64 committed_size = (end + ARENA_COMMIT_GRANULARITY - 1) & ~(ARENA_COMMIT_GRANULARITY - 1);
	committed_size = Min(committed_size, arena->reserved_size);
	Assert(platform_virtual_commit(arena->memory + arena->committed_size, committed_size - arena->committed_size));
	arena->committed_size = committed_size;
}

void arena_ensure_committed(Arena *arena, u64 size)
{
	Assert(size <= arena->reserved_size - arena->position);
	arena_commit_through(arena, arena->position + size);
}

Arena arena_create(u64 reserved_size, const char *name)
{
	if (!reserved_size) reserved_size = MB(256);
	Assert(reserved_size);

	Arena arena = {
		.name = name,
		.reserved_size = reserved_size,
		.memory = platform_virtual_reserve(reserved_size),
	};
	Assert(arena.memory);
	return arena;
}

void arena_destroy(Arena *arena)
{
	if (arena->memory) platform_virtual_release(arena->memory);
	*arena = (Arena) {};
}

void arena_reset(Arena *arena)
{
	arena->position = 0;
}

void *arena_top(const Arena *arena)
{
	return arena->memory + arena->position;
}

void *arena_base(const Arena *arena)
{
	return arena->memory;
}

u64 arena_used(const Arena *arena)
{
	return arena->position;
}

void *arena_pop(Arena *arena, u64 size)
{
	Assert(size <= arena->position);
	arena->position -= size;
	return arena->memory + arena->position;
}

void *arena_push_aligned(Arena *arena, u64 size, u64 alignment)
{
	prof_add_metric(PROF_METRIC_STACK_PUSH_CALLS, 1);
	prof_add_metric(PROF_METRIC_STACK_PUSH_SIZE, size);

	u64 position = arena_align_position(arena, arena->position, alignment);
	Assert(position <= arena->reserved_size);
	if (size > arena->reserved_size - position)
	{
		LOG_ERROR("\n\tarena '%s' ran out of memory when pushing size: %s""\n\t\t capacity: %s""\n\t\t usage: %s",
			arena->name, arena_format_size(size), arena_format_size(arena->reserved_size), arena_format_size(arena->position));
	}
	Assert(size <= arena->reserved_size - position);
	u64 end = position + size;
	Assert(end <= arena->reserved_size);
	arena_commit_through(arena, end);
	arena->position = end;
	return arena->memory + position;
}

void *arena_push_fill(Arena *arena, u64 size, u32 fill)
{
	void *result = arena_push(arena, size);
	memory_fill(result, fill, size);
	return result;
}

void *arena_push_zero(Arena *arena, u64 size)
{
	return arena_push_fill(arena, size, 0);
}

void *arena_push_copy(Arena *arena, u64 size, const void *data)
{
	void *dest = arena_push(arena, size);
	memory_copy(dest, data, size);
	return dest;
}

void *arena_push_byte(Arena *arena, u32 value)
{
	u8 *place = arena_push_aligned(arena, 1, 1);
	*place = value;
	return place;
}
