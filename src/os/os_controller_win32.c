#include "os_win32_internal.h"
#include "os_controller.h"

static f32 os_normalize_u8(i32 value)
{
	return (f32)value / 255.f;
}

static f32 os_normalize_i16(i32 value)
{
	return value >= 0 ? (f32)value / 32767.f : (f32)value / 32768.f;
}
#include <xinput.h>

#pragma comment(lib, "Xinput")

b32 os_controller_get_state(u32 player, OS_ControllerState *state)
{
	Assert(state);
	memory_zero(state, sizeof(*state));
	XINPUT_STATE input = { 0 };
	if (XInputGetState(player, &input) != ERROR_SUCCESS) return false;

	WORD buttons = input.Gamepad.wButtons;
	state->buttons[0] = !!(buttons & XINPUT_GAMEPAD_A);
	state->buttons[1] = !!(buttons & XINPUT_GAMEPAD_B);
	state->buttons[2] = !!(buttons & XINPUT_GAMEPAD_X);
	state->buttons[3] = !!(buttons & XINPUT_GAMEPAD_Y);
	state->dpad[0] = !!(buttons & XINPUT_GAMEPAD_DPAD_UP);
	state->dpad[1] = !!(buttons & XINPUT_GAMEPAD_DPAD_DOWN);
	state->dpad[2] = !!(buttons & XINPUT_GAMEPAD_DPAD_LEFT);
	state->dpad[3] = !!(buttons & XINPUT_GAMEPAD_DPAD_RIGHT);
	state->shoulders[0] = !!(buttons & XINPUT_GAMEPAD_LEFT_SHOULDER);
	state->shoulders[1] = !!(buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
	state->thumb_buttons[0] = !!(buttons & XINPUT_GAMEPAD_LEFT_THUMB);
	state->thumb_buttons[1] = !!(buttons & XINPUT_GAMEPAD_RIGHT_THUMB);
	state->start = !!(buttons & XINPUT_GAMEPAD_START);
	state->back = !!(buttons & XINPUT_GAMEPAD_BACK);
	state->triggers[0] = os_normalize_u8(input.Gamepad.bLeftTrigger);
	state->triggers[1] = os_normalize_u8(input.Gamepad.bRightTrigger);
	state->thumb_sticks[0] = v2(os_normalize_i16(input.Gamepad.sThumbLX),
		os_normalize_i16(input.Gamepad.sThumbLY));
	state->thumb_sticks[1] = v2(os_normalize_i16(input.Gamepad.sThumbRX),
		os_normalize_i16(input.Gamepad.sThumbRY));
	return true;
}
