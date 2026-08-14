#ifndef BASE_STREAM_H
#define BASE_STREAM_H

typedef enum
{
	BYTE_STREAM_READ,
	BYTE_STREAM_WRITE,
}
ByteStreamMode;

typedef struct
{
	u8 *data;
	u64 size;
	u64 cursor;
	Arena *arena;
	u64 arena_start_position;
	ByteStreamMode mode;
	b32 failed;
	b32 ended;
}
ByteStream;

// Integer transfers use little-endian byte order. Failure is sticky: the
// failing transfer and all later transfers leave the cursor unchanged.
ByteStream byte_stream_reader(ByteSpan source);
ByteStream byte_stream_writer(ByteSpan destination);
// The stream owns the arena tail until byte_stream_written or
// byte_stream_cancel. Successful writes advance and commit the arena.
// Finalizing a failed arena writer or cancelling it restores the arena.
ByteStream byte_stream_arena_writer(Arena *arena);
ByteSpan byte_stream_written(ByteStream *stream);
void byte_stream_cancel(ByteStream *stream);
u64 byte_stream_remaining(const ByteStream *stream);
ByteSpan byte_stream_take(ByteStream *stream, u64 size);
void byte_stream_skip(ByteStream *stream, u64 size);
void byte_transfer_bytes(ByteStream *stream, ByteSpan bytes);
void byte_transfer_u8(ByteStream *stream, u8 *value);
void byte_transfer_u16(ByteStream *stream, u16 *value);
void byte_transfer_u32(ByteStream *stream, u32 *value);
void byte_transfer_u64(ByteStream *stream, u64 *value);

#endif
