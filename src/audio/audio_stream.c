#include "base.h"
#include "audio_stream.h"

struct Audio_Stream
{
	f32 *samples;
	u32  sample_rate;
	u32  channels;
	u32  capacity;
	u32  read_cursor;
	u32  count;
	u32  acquired_count;
	u64  overrun_frames;
};

void audio_stream_write(Audio_Stream *stream, const f32 *frames, u32 frame_count)
{
	Assert(stream);
	Assert(frames || frame_count == 0);
	for (u32 frame = 0; frame < frame_count; ++frame)
	{
		if (stream->count == stream->capacity)
		{
			stream->read_cursor = (stream->read_cursor + 1) % stream->capacity;
			stream->count -= 1;
			stream->overrun_frames += 1;
		}
		u32 write_cursor = (stream->read_cursor + stream->count) % stream->capacity;
		memory_copy(stream->samples + write_cursor * stream->channels,
			frames + frame * stream->channels, sizeof(*frames) * stream->channels);
		stream->count += 1;
	}
}

Audio_Stream *audio_stream_create(Arena *arena, Audio_StreamDesc desc)
{
	Assert(arena);
	Assert(desc.sample_rate > 0);
	Assert(desc.channels > 0);
	Assert(desc.frame_capacity > 0);

	Audio_Stream *stream = arena_push_zero(arena, sizeof(*stream));
	stream->samples = arena_push_zero(arena,
		sizeof(*stream->samples) * desc.frame_capacity * desc.channels);
	stream->sample_rate = desc.sample_rate;
	stream->channels = desc.channels;
	stream->capacity = desc.frame_capacity;
	return stream;
}

Audio_ReadSpan audio_stream_acquire(Audio_Stream *stream)
{
	Assert(stream);
	Assert(stream->acquired_count == 0);
	u32 contiguous_frames = Min(stream->count,
		stream->capacity - stream->read_cursor);
	stream->acquired_count = contiguous_frames;
	return (Audio_ReadSpan) {
		.samples = stream->samples + stream->read_cursor * stream->channels,
		.frame_count = contiguous_frames,
	};
}

void audio_stream_consume(Audio_Stream *stream, u32 frame_count)
{
	Assert(stream);
	if (frame_count > stream->acquired_count)
	{
		LOG_ERROR("audio consume exceeded acquired span: requested %u, acquired %u, queued %u, capacity %u, read cursor %u",
			frame_count, stream->acquired_count, stream->count, stream->capacity, stream->read_cursor);
		frame_count = stream->acquired_count;
	}
	stream->read_cursor = (stream->read_cursor + frame_count) % stream->capacity;
	stream->count -= frame_count;
	stream->acquired_count = 0;
}

void audio_stream_discard(Audio_Stream *stream)
{
	Assert(stream);
	Assert(stream->acquired_count == 0);
	stream->read_cursor = 0;
	stream->count = 0;
}

u32 audio_stream_available_frames(const Audio_Stream *stream)
{
	return stream->count;
}

u32 audio_stream_capacity_frames(const Audio_Stream *stream)
{
	return stream->capacity;
}

u64 audio_stream_overrun_frames(const Audio_Stream *stream)
{
	return stream->overrun_frames;
}
