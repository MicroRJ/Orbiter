ByteStream byte_stream_reader(ByteSpan source)
{
	return (ByteStream) { .data = source.data, .size = source.size, .mode = BYTE_STREAM_READ, .failed = !source.data && source.size };
}

ByteStream byte_stream_writer(ByteSpan destination)
{
	return (ByteStream) { .data = destination.data, .size = destination.size, .mode = BYTE_STREAM_WRITE, .failed = !destination.data && destination.size };
}

ByteStream byte_stream_arena_writer(Arena *arena)
{
	ByteStream stream = { .arena = arena, .mode = BYTE_STREAM_WRITE };
	if (!arena || !arena->memory || arena->position > arena->reserved_size) stream.failed = true;
	else
	{
		stream.data = arena->memory + arena->position;
		stream.size = arena->reserved_size - arena->position;
		stream.arena_start_position = arena->position;
	}
	return stream;
}

static b32 byte_stream_owns_arena_tail(const ByteStream *stream)
{
	return !stream->arena || stream->arena->position == stream->arena_start_position + stream->cursor;
}

ByteSpan byte_stream_written(ByteStream *stream)
{
	if (!stream || stream->mode != BYTE_STREAM_WRITE || stream->ended)
	{
		if (stream) stream->failed = true;
		return (ByteSpan) {};
	}
	if (!byte_stream_owns_arena_tail(stream))
	{
		stream->failed = true;
		stream->ended = true;
		return (ByteSpan) {};
	}
	if (stream->failed)
	{
		if (stream->arena) stream->arena->position = stream->arena_start_position;
		stream->ended = true;
		return (ByteSpan) {};
	}
	stream->ended = true;
	return byte_span(stream->data, stream->cursor);
}

void byte_stream_cancel(ByteStream *stream)
{
	if (!stream || stream->ended) return;
	if (stream->arena)
	{
		if (!byte_stream_owns_arena_tail(stream))
		{
			stream->failed = true;
			stream->ended = true;
			return;
		}
		stream->arena->position = stream->arena_start_position;
	}
	stream->ended = true;
}

u64 byte_stream_remaining(const ByteStream *stream)
{
	return stream->cursor <= stream->size ? stream->size - stream->cursor : 0;
}

ByteSpan byte_stream_take(ByteStream *stream, u64 size)
{
	if (stream->failed || stream->ended || !byte_stream_owns_arena_tail(stream) || stream->cursor > stream->size || size > stream->size - stream->cursor)
	{
		stream->failed = true;
		return (ByteSpan) {};
	}
	if (!size) return byte_span(stream->data ? stream->data + stream->cursor : 0, 0);
	ByteSpan result = byte_span(stream->data + stream->cursor, size);
	if (stream->arena)
	{
		void *memory = arena_push_aligned(stream->arena, size, 1);
		Assert(memory == result.data);
	}
	stream->cursor += size;
	return result;
}

void byte_stream_skip(ByteStream *stream, u64 size)
{
	(void)byte_stream_take(stream, size);
}

void byte_transfer_bytes(ByteStream *stream, ByteSpan bytes)
{
	if (!bytes.size) return;
	if (!bytes.data)
	{
		stream->failed = true;
		return;
	}
	ByteSpan transfer = byte_stream_take(stream, bytes.size);
	if (!transfer.data)
	{
		if (stream->mode == BYTE_STREAM_READ && bytes.data) memory_zero(bytes.data, bytes.size);
		return;
	}
	if (stream->mode == BYTE_STREAM_READ) memory_copy(bytes.data, transfer.data, bytes.size);
	else memory_copy(transfer.data, bytes.data, bytes.size);
}

void byte_transfer_u8(ByteStream *stream, u8 *value)
{
	byte_transfer_bytes(stream, byte_span(value, sizeof(*value)));
}

void byte_transfer_u16(ByteStream *stream, u16 *value)
{
	if (!value)
	{
		stream->failed = true;
		return;
	}
	ByteSpan transfer = byte_stream_take(stream, sizeof(*value));
	if (!transfer.data)
	{
		if (stream->mode == BYTE_STREAM_READ) *value = 0;
		return;
	}
	if (stream->mode == BYTE_STREAM_READ) *value = (u16)(transfer.data[0] | (u16)transfer.data[1] << 8);
	else
	{
		transfer.data[0] = (u8)*value;
		transfer.data[1] = (u8)(*value >> 8);
	}
}

void byte_transfer_u32(ByteStream *stream, u32 *value)
{
	if (!value)
	{
		stream->failed = true;
		return;
	}
	ByteSpan transfer = byte_stream_take(stream, sizeof(*value));
	if (!transfer.data)
	{
		if (stream->mode == BYTE_STREAM_READ) *value = 0;
		return;
	}
	if (stream->mode == BYTE_STREAM_READ)
	{
		*value = 0;
		for (u32 index = 0; index < 4; index ++) *value |= (u32)transfer.data[index] << (index * 8);
	}
	else for (u32 index = 0; index < 4; index ++) transfer.data[index] = (u8)(*value >> (index * 8));
}

void byte_transfer_u64(ByteStream *stream, u64 *value)
{
	if (!value)
	{
		stream->failed = true;
		return;
	}
	ByteSpan transfer = byte_stream_take(stream, sizeof(*value));
	if (!transfer.data)
	{
		if (stream->mode == BYTE_STREAM_READ) *value = 0;
		return;
	}
	if (stream->mode == BYTE_STREAM_READ)
	{
		*value = 0;
		for (u32 index = 0; index < 8; index ++) *value |= (u64)transfer.data[index] << (index * 8);
	}
	else for (u32 index = 0; index < 8; index ++) transfer.data[index] = (u8)(*value >> (index * 8));
}
