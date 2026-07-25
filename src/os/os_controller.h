#ifndef OS_CONTROLLER_H
#define OS_CONTROLLER_H

#include "base.h"

typedef struct
{
	b32  shoulders[2];
	b32  start;
	b32  back;
	b32  dpad[4];
	b32  buttons[4];
	b32  thumb_buttons[2];
	f32  triggers[2];
	vec2 thumb_sticks[2];
}
OS_ControllerState;

b32 os_controller_get_state(u32 player, OS_ControllerState *state);

#endif
