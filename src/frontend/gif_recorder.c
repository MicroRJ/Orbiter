#include "base.h"
#include "graphics.h"
#define MSF_GIF_IMPL
#include "gif_recorder.h"

enum { GIF_RECORDER_BIT_DEPTH = 16 };

static size_t gif_recorder_write(const void *buffer, size_t size, size_t count, void *stream)
{
	return fwrite(buffer, size, count, stream);
}

b32 gif_recorder_begin(GifRecorder *recorder, vec2i size, const char *name)
{
	Assert(recorder);
	Assert(!recorder->recording);
	memory_zero(recorder, sizeof(*recorder));
	recorder->size = size;
	for (u32 index = 1; index < 10000; ++index)
	{
		snprintf(recorder->path, sizeof(recorder->path), "data/%s_%03u.gif", name, index);
		Platform_File_Info info;
		if (!platform_get_file_info(recorder->path, &info)) {
			break;
		}
	}
	recorder->file = fopen(recorder->path, "wb");
	if (!recorder->file) {
		return false;
	}
	recorder->recording = msf_gif_begin_to_file(&recorder->state, size.x, size.y, gif_recorder_write, recorder->file);
	if (!recorder->recording)
	{
		fclose(recorder->file);
		recorder->file = 0;
	}
	return recorder->recording;
}

b32 gif_recorder_frame(GifRecorder *recorder, Color_RGBA8 *pixels, u32 stride)
{
	Assert(recorder);
	Assert(pixels);
	if (!recorder->recording) {
		return false;
	}
	// GIF delays are integral centiseconds. 2, 2, 1 averages exactly 60 Hz.
	u32 delay = recorder->frame_count % 3 == 2 ? 1 : 2;
	if (!msf_gif_frame_to_file(&recorder->state, (u8 *)pixels, delay, GIF_RECORDER_BIT_DEPTH, stride)) {
		recorder->recording = false;
		fclose(recorder->file);
		recorder->file = 0;
		return false;
	}
	++recorder->frame_count;
	return true;
}

b32 gif_recorder_end(GifRecorder *recorder)
{
	Assert(recorder);
	if (!recorder->recording) {
		return false;
	}
	b32 success = msf_gif_end_to_file(&recorder->state);
	recorder->recording = false;
	success = fclose(recorder->file) == 0 && success;
	recorder->file = 0;
	if (success) LOG_INFO("saved %u-frame GIF to '%s'", recorder->frame_count, recorder->path);
	else LOG_ERROR("failed to save GIF capture");
	recorder->frame_count = 0;
	return success;
}
