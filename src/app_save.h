#ifndef ORBITER_APP_SAVE_H
#define ORBITER_APP_SAVE_H

#include "orb.h"

typedef struct
{
	Orb_SaveState state;
	Orb_Thumbnail thumbnail;
}
App_SaveData;

ByteSpan app_save_encode(Arena *arena, const App_SaveData *data);
b32 app_save_decode(Arena *arena, ByteSpan encoded, App_SaveData *data);

#endif
