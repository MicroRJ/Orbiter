#include "base.h"
#include "audio_stream.h"

struct Audio_Stream
{
	f32 *samples;
	u32  capacity;
	u64  read_cursor;
	u64  write_cursor;
	u32  acquired_count;
	u64  overrun_frames;
};

static u64 audio_stream_write_part(Audio_Stream *stream, const f32 *frames, u32 frame_count)
{
	u64 destination = stream->write_cursor % stream->capacity;
	u64 contiguous = Min(stream->capacity - destination, frame_count);
	memory_copy(stream->samples + destination, frames, contiguous * sizeof(* frames));
	stream->write_cursor += contiguous;
	u64 readable = stream->write_cursor - stream->read_cursor;
	u64 overrun = readable - Min(readable, stream->capacity);
	stream->overrun_frames += overrun;
	stream->read_cursor += overrun;
	return contiguous;
}

void audio_stream_write(Audio_Stream *stream, const f32 *first_frame, u32 frame_count)
{
	Assert(stream);
	Assert(first_frame || frame_count == 0);

	while (frame_count) {
		u64 wrote = audio_stream_write_part(stream, first_frame, frame_count);
		Assert(wrote <= frame_count);
		frame_count -= wrote;
		first_frame += wrote;
	}
}

Audio_Stream *audio_stream_create(Arena *arena, Audio_StreamDesc desc)
{
	Assert(arena);
	Assert(desc.frame_capacity > 0);

	Audio_Stream *stream = arena_push_zero(arena, sizeof(*stream));
	stream->samples = arena_push_zero(arena, sizeof(*stream->samples) * desc.frame_capacity);
	stream->capacity = desc.frame_capacity;
	return stream;
}

Audio_ReadSpan audio_stream_acquire(Audio_Stream *stream)
{
	Assert(stream);
	Assert(stream->acquired_count == 0);
	Assert(stream->write_cursor >= stream->read_cursor);
	Assert(stream->write_cursor - stream->read_cursor <= stream->capacity);
	u64 read_capacity = stream->write_cursor - stream->read_cursor;
	u64 read_offset = stream->read_cursor % stream->capacity;
	u64 read_contiguous = Min(read_capacity, stream->capacity - read_offset);
	stream->acquired_count = read_contiguous;
	return (Audio_ReadSpan) {
		.samples = stream->samples + read_offset,
		.frame_count = read_contiguous,
	};
}

void audio_stream_consume(Audio_Stream *stream, u32 frame_count)
{
	Assert(stream);
	Assert(frame_count <= stream->acquired_count);
	stream->read_cursor += frame_count;
	stream->acquired_count = 0;
}

void audio_stream_discard(Audio_Stream *stream)
{
	Assert(stream);
	Assert(stream->acquired_count == 0);
	stream->read_cursor = 0;
	stream->write_cursor = 0;
}

u32 audio_stream_queued_frames(const Audio_Stream *stream)
{
	Assert(stream->write_cursor >= stream->read_cursor);
	Assert(stream->write_cursor - stream->read_cursor <= stream->capacity);
	u64 readable = stream->write_cursor - stream->read_cursor;
	return readable;
}

u32 audio_stream_capacity_frames(const Audio_Stream *stream)
{
	return stream->capacity;
}

u64 audio_stream_overrun_frames(const Audio_Stream *stream)
{
	return stream->overrun_frames;
}
