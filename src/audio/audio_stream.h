#ifndef AUDIO_STREAM_H
#define AUDIO_STREAM_H

typedef struct Audio_Stream Audio_Stream;

typedef struct
{
	const f32 *samples;
	u32        frame_count;
}
Audio_ReadSpan;

typedef struct
{
	u32 sample_rate;
	u32 channels;
	u32 frame_capacity;
}
Audio_StreamDesc;

Audio_Stream *audio_stream_create(Arena *arena, Audio_StreamDesc desc);
void audio_stream_write(Audio_Stream *stream, const f32 *frames, u32 frame_count);

// The consumer may hold one readable span at a time. consume closes that span
// and may consume fewer than the number of frames acquired.
Audio_ReadSpan audio_stream_acquire(Audio_Stream *stream);
void audio_stream_consume(Audio_Stream *stream, u32 frame_count);
void audio_stream_discard(Audio_Stream *stream);

u32 audio_stream_available_frames(const Audio_Stream *stream);
u32 audio_stream_capacity_frames(const Audio_Stream *stream);
u64 audio_stream_overrun_frames(const Audio_Stream *stream);

#endif
