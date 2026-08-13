#include "input.h"

void input_state_update(Input_State *input, const OS_Window *window)
{
	Assert(input);
	Assert(window);

	for (u32 key = 0; key < OS_Key_COUNT; ++key) input->keys[key] &= INPUT_KEY_DOWN;

	for (u32 event_index = 0; event_index < os_window_event_count(window); ++event_index)
	{
		const OS_Event *event = os_window_event(window, event_index);
		if (event->type == OS_EVENT_WINDOW_FOCUS_LOST)
		{
			for (u32 key = 0; key < OS_Key_COUNT; ++key)
			{
				if (input->keys[key] & INPUT_KEY_DOWN) input->keys[key] |= INPUT_KEY_RELEASED;
				input->keys[key] &= ~INPUT_KEY_DOWN;
			}
		}
		else if (event->key > OS_Key_Null && event->key < OS_Key_COUNT)
		{
			if (event->type == OS_EVENT_KEY_PRESS) input->keys[event->key] |= INPUT_KEY_DOWN | (event->repeat ? INPUT_KEY_REPEAT : INPUT_KEY_PRESSED);
			else if (event->type == OS_EVENT_KEY_RELEASE) input->keys[event->key] = (input->keys[event->key] & ~INPUT_KEY_DOWN) | INPUT_KEY_RELEASED;
		}
	}
}
