typedef struct
{
	const char *name;
	u64 reserved_size;
	u64 committed_size;
	u64 position;
	u8 *memory;
}
Arena;

enum
{
	ARENA_DEFAULT_ALIGNMENT = 16,
	ARENA_COMMIT_GRANULARITY = KiB(64),
};

#define ARENA_SCOPE(arena) for (u64 _arena_position = (arena)->position; _arena_position != ~(u64)0; (arena)->position = _arena_position, _arena_position = ~(u64)0)

Arena arena_create(u64 reserved_size, const char *name);
void arena_destroy(Arena *arena);
void arena_reset(Arena *arena);
void *arena_top(const Arena *arena);
void *arena_base(const Arena *arena);
u64 arena_used(const Arena *arena);
void *arena_push_aligned(Arena *arena, u64 size, u64 alignment);
void arena_ensure_committed(Arena *arena, u64 size);
static inline void *arena_push(Arena *arena, u64 size) { return arena_push_aligned(arena, size, ARENA_DEFAULT_ALIGNMENT); }
void *arena_push_fill(Arena *arena, u64 size, u32 fill);
void *arena_push_zero(Arena *arena, u64 size);
void *arena_push_copy(Arena *arena, u64 size, const void *data);
void *arena_push_byte(Arena *arena, u32 byte);
void *arena_pop(Arena *arena, u64 size);

#define arena_distance(arena, pointer) ((u8 *)arena_top(arena) - (u8 *)(pointer))
