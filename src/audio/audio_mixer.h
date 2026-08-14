#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

typedef struct Audio_Mixer Audio_Mixer;

typedef struct
{
	// Caller-owned mono PCM at the output sample rate. The samples must remain
	// alive until every voice playing this clip has finished.
	const f32 *samples;
	u32 frame_count;
}
Audio_Clip;

typedef struct
{
	u32 voice_capacity;
}
Audio_MixerDesc;

typedef struct
{
	f32 gain;
}
Audio_PlayDesc;

Audio_Mixer *audio_mixer_create(Arena *arena, Audio_MixerDesc desc);
b32 audio_mixer_play(Audio_Mixer *mixer, Audio_Clip clip, Audio_PlayDesc desc);

// Renders the next frames without consuming them. Call advance with the number
// of frames the backend actually accepted.
b32 audio_mixer_render(const Audio_Mixer *mixer, f32 *output, const f32 *input, u32 frame_count, f32 input_gain, f32 voice_gain, f32 master_gain);
void audio_mixer_advance(Audio_Mixer *mixer, u32 frame_count);
void audio_mixer_stop_all(Audio_Mixer *mixer);
u32 audio_mixer_active_voice_count(const Audio_Mixer *mixer);

#endif
