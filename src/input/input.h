#ifndef ORBITER_INPUT_H
#define ORBITER_INPUT_H

#include "os_graphical.h"

typedef enum
{
	INPUT_KEY_DOWN     = 1 << 0,
	INPUT_KEY_PRESSED  = 1 << 1,
	INPUT_KEY_RELEASED = 1 << 2,
	INPUT_KEY_REPEAT   = 1 << 3,
}
Input_KeyStateFlags;

typedef u8 Input_KeyState;

typedef struct
{
	Input_KeyState keys[OS_Key_COUNT];
}
Input_State;

void input_state_update(Input_State *input, const OS_Window *window);

#endif
