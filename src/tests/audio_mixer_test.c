#include "base.h"
#include "audio_mixer.h"

static u32 failures;

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); failures ++; } } while (0)

static Audio_Mixer *test_mixer(Arena *arena, u32 voice_capacity)
{
	return audio_mixer_create(arena, (Audio_MixerDesc) { .voice_capacity = voice_capacity });
}

static void test_bed_gain(void)
{
	Arena arena = arena_create(MB(1), "audio mixer bed test");
	Audio_Mixer *mixer = test_mixer(&arena, 2);
	f32 bed[] = { -0.5f, 0.25f, 0.75f };
	f32 output[ArrayCount(bed)] = {};
	CHECK(audio_mixer_render(mixer, output, bed, ArrayCount(bed), 0.5f, 1.f, 0.5f));
	CHECK(output[0] == -0.125f);
	CHECK(output[1] == 0.0625f);
	CHECK(output[2] == 0.1875f);
	arena_destroy(&arena);
}

static void test_voice_mixing_and_lifecycle(void)
{
	Arena arena = arena_create(MB(1), "audio mixer voice test");
	Audio_Mixer *mixer = test_mixer(&arena, 2);
	f32 first_samples[] = { 0.25f, 0.5f, 0.75f };
	f32 second_samples[] = { -0.125f, -0.25f };
	CHECK(audio_mixer_play(mixer, (Audio_Clip) { first_samples, ArrayCount(first_samples) }, (Audio_PlayDesc) { .gain = 0.5f }));
	CHECK(audio_mixer_play(mixer, (Audio_Clip) { second_samples, ArrayCount(second_samples) }, (Audio_PlayDesc) { .gain = 1.f }));

	f32 output[2] = {};
	CHECK(audio_mixer_render(mixer, output, 0, ArrayCount(output), 1.f, 0.5f, 1.f));
	CHECK(output[0] == 0.f);
	CHECK(output[1] == 0.f);
	CHECK(audio_mixer_active_voice_count(mixer) == 2);
	audio_mixer_advance(mixer, ArrayCount(output));
	CHECK(audio_mixer_active_voice_count(mixer) == 1);

	CHECK(audio_mixer_render(mixer, output, 0, ArrayCount(output), 1.f, 1.f, 1.f));
	CHECK(output[0] == 0.375f);
	CHECK(output[1] == 0.f);
	CHECK(audio_mixer_active_voice_count(mixer) == 1);
	audio_mixer_advance(mixer, ArrayCount(output));
	CHECK(audio_mixer_active_voice_count(mixer) == 0);
	CHECK(!audio_mixer_render(mixer, output, 0, ArrayCount(output), 1.f, 1.f, 1.f));
	CHECK(output[0] == 0.f && output[1] == 0.f);
	arena_destroy(&arena);
}

static void test_oldest_voice_is_stolen(void)
{
	Arena arena = arena_create(MB(1), "audio mixer stealing test");
	Audio_Mixer *mixer = test_mixer(&arena, 2);
	f32 first[] = { 0.10f };
	f32 second[] = { 0.20f };
	f32 third[] = { 0.30f };
	CHECK(audio_mixer_play(mixer, (Audio_Clip) { first, 1 }, (Audio_PlayDesc) { .gain = 1.f }));
	CHECK(audio_mixer_play(mixer, (Audio_Clip) { second, 1 }, (Audio_PlayDesc) { .gain = 1.f }));
	CHECK(audio_mixer_play(mixer, (Audio_Clip) { third, 1 }, (Audio_PlayDesc) { .gain = 1.f }));
	f32 output = 0.f;
	CHECK(audio_mixer_render(mixer, &output, 0, 1, 1.f, 1.f, 1.f));
	CHECK(fabsf(output - 0.50f) < 0.0001f);
	arena_destroy(&arena);
}

static void test_partial_backend_write(void)
{
	Arena arena = arena_create(MB(1), "audio mixer partial write test");
	Audio_Mixer *mixer = test_mixer(&arena, 1);
	f32 samples[] = { 0.25f, 0.50f, 0.75f };
	f32 output[2] = {};
	CHECK(audio_mixer_play(mixer, (Audio_Clip) { samples, ArrayCount(samples) }, (Audio_PlayDesc) { .gain = 1.f }));
	CHECK(audio_mixer_render(mixer, output, 0, ArrayCount(output), 1.f, 1.f, 1.f));
	CHECK(output[0] == 0.25f && output[1] == 0.50f);
	audio_mixer_advance(mixer, 1);
	CHECK(audio_mixer_render(mixer, output, 0, ArrayCount(output), 1.f, 1.f, 1.f));
	CHECK(output[0] == 0.50f && output[1] == 0.75f);
	audio_mixer_advance(mixer, 2);
	CHECK(audio_mixer_active_voice_count(mixer) == 0);
	arena_destroy(&arena);
}

static void test_output_is_limited(void)
{
	Arena arena = arena_create(MB(1), "audio mixer limiter test");
	Audio_Mixer *mixer = test_mixer(&arena, 2);
	f32 voice_samples[] = { 0.75f, -0.75f };
	CHECK(audio_mixer_play(mixer, (Audio_Clip) { voice_samples, ArrayCount(voice_samples) }, (Audio_PlayDesc) { .gain = 1.f }));
	f32 bed[] = { 0.75f, -0.75f };
	f32 output[2] = {};
	CHECK(audio_mixer_render(mixer, output, bed, ArrayCount(output), 1.f, 1.f, 1.f));
	CHECK(output[0] == 1.f);
	CHECK(output[1] == -1.f);
	arena_destroy(&arena);
}

static void test_stop_all(void)
{
	Arena arena = arena_create(MB(1), "audio mixer stop test");
	Audio_Mixer *mixer = test_mixer(&arena, 2);
	f32 samples[] = { 0.25f, 0.25f };
	CHECK(audio_mixer_play(mixer, (Audio_Clip) { samples, ArrayCount(samples) }, (Audio_PlayDesc) { .gain = 1.f }));
	audio_mixer_stop_all(mixer);
	CHECK(audio_mixer_active_voice_count(mixer) == 0);
	f32 output = 1.f;
	CHECK(!audio_mixer_render(mixer, &output, 0, 1, 1.f, 1.f, 1.f));
	CHECK(output == 0.f);
	arena_destroy(&arena);
}

int main(void)
{
	test_bed_gain();
	test_voice_mixing_and_lifecycle();
	test_oldest_voice_is_stolen();
	test_partial_backend_write();
	test_output_is_limited();
	test_stop_all();
	if (failures) {
		fprintf(stderr, "Audio mixer tests failed: %u\n", failures);
		return 1;
	}
	printf("Audio mixer tests passed\n");
	return 0;
}
