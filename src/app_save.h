#ifndef ORBITER_APP_SAVE_H
#define ORBITER_APP_SAVE_H

#include "nes/emulator.h"

typedef enum
{
	APP_PIXEL_FORMAT_RGBA8 = 1,
}
App_PixelFormat;

typedef struct
{
	u32 width;
	u32 height;
	u32 stride;
	App_PixelFormat format;
	ByteSpan pixels;
}
App_Thumbnail;

typedef struct
{
	NES_State state;
	App_Thumbnail thumbnail;
}
App_Save;

ByteSpan app_save_encode(Arena *arena, const App_Save *save);
b32 app_save_decode(Arena *arena, ByteSpan encoded, App_Save *save);

#endif
