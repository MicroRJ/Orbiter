#include "base.h"
#include "audio_mixer.h"

typedef struct
{
	Audio_Clip clip;
	u32 cursor;
	f32 gain;
	u64 serial;
	b32 active;
}
Audio_Voice;

struct Audio_Mixer
{
	Audio_Voice *voices;
	u32 voice_capacity;
	u64 next_serial;
};

Audio_Mixer *audio_mixer_create(Arena *arena, Audio_MixerDesc desc)
{
	Assert(arena);
	Assert(desc.voice_capacity);
	Audio_Mixer *mixer = arena_push_zero(arena, sizeof(*mixer));
	mixer->voices = arena_push_zero(arena, sizeof(*mixer->voices) * desc.voice_capacity);
	mixer->voice_capacity = desc.voice_capacity;
	mixer->next_serial = 1;
	return mixer;
}

static Audio_Voice *audio_mixer_acquire_voice(Audio_Mixer *mixer)
{
	Audio_Voice *oldest = mixer->voices;
	for (u32 index = 0; index < mixer->voice_capacity; index ++)
	{
		Audio_Voice *voice = mixer->voices + index;
		if (!voice->active) return voice;
		if (voice->serial < oldest->serial) oldest = voice;
	}
	return oldest;
}

b32 audio_mixer_play(Audio_Mixer *mixer, Audio_Clip clip, Audio_PlayDesc desc)
{
	Assert(mixer);
	if (!clip.samples || !clip.frame_count) return false;
	Audio_Voice *voice = audio_mixer_acquire_voice(mixer);
	*voice = (Audio_Voice) {
		.clip = clip,
		.gain = desc.gain,
		.serial = mixer->next_serial ++,
		.active = true,
	};
	return true;
}

b32 audio_mixer_render(const Audio_Mixer *mixer, f32 *output, const f32 *input, u32 frame_count, f32 input_gain, f32 voice_gain, f32 master_gain)
{
	Assert(mixer);
	Assert(output || !frame_count);
	b32 has_audio = !!input;
	for (u32 frame = 0; frame < frame_count; frame ++) output[frame] = input ? input[frame] * input_gain : 0.f;

	for (u32 voice_index = 0; voice_index < mixer->voice_capacity; voice_index ++)
	{
		const Audio_Voice *voice = mixer->voices + voice_index;
		if (!voice->active) continue;
		has_audio = true;
		u32 remaining = voice->clip.frame_count - voice->cursor;
		u32 mix_count = Min(frame_count, remaining);
		f32 gain = voice->gain * voice_gain;
		for (u32 frame = 0; frame < mix_count; frame ++) output[frame] += voice->clip.samples[voice->cursor + frame] * gain;
	}

	for (u32 frame = 0; frame < frame_count; frame ++) output[frame] = CLAMP(output[frame] * master_gain, -1.f, 1.f);
	return has_audio;
}

void audio_mixer_advance(Audio_Mixer *mixer, u32 frame_count)
{
	Assert(mixer);
	for (u32 voice_index = 0; voice_index < mixer->voice_capacity; voice_index ++)
	{
		Audio_Voice *voice = mixer->voices + voice_index;
		if (!voice->active) continue;
		u32 remaining = voice->clip.frame_count - voice->cursor;
		voice->cursor += Min(frame_count, remaining);
		if (voice->cursor == voice->clip.frame_count) *voice = (Audio_Voice) {};
	}
}

void audio_mixer_stop_all(Audio_Mixer *mixer)
{
	Assert(mixer);
	memory_zero(mixer->voices, sizeof(*mixer->voices) * mixer->voice_capacity);
}

u32 audio_mixer_active_voice_count(const Audio_Mixer *mixer)
{
	Assert(mixer);
	u32 count = 0;
	for (u32 index = 0; index < mixer->voice_capacity; index ++) count += !!mixer->voices[index].active;
	return count;
}
