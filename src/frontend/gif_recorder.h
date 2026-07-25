#ifndef GIF_RECORDER_H
#define GIF_RECORDER_H

#include "base.h"
#include "graphics.h"
#include "msf_gif.h"

typedef struct
{
	MsfGifState state;
	vec2i size;
	u32 frame_count;
	b32 recording;
	FILE *file;
	char path[256];
}
GifRecorder;

b32 gif_recorder_begin(GifRecorder *recorder, vec2i size, const char *name);
b32 gif_recorder_frame(GifRecorder *recorder, Color_RGBA8 *pixels, u32 stride);
b32 gif_recorder_end(GifRecorder *recorder);

#endif
