#include "base.h"
#include "os_graphical_internal.h"

OS_Event *os_graphical_push_event(OS_Window *window, OS_EventType type)
{
	if (window->event_count == window->event_capacity)
	{
		u32 capacity = window->event_capacity ? window->event_capacity * 2 : 64;
		OS_Event *events = realloc(window->events, sizeof(*events) * capacity);
		if (!events)
		{
			LOG_ERROR("failed to grow OS event queue to %u entries", capacity);
			return NULL;
		}
		window->events = events;
		window->event_capacity = capacity;
	}
	OS_Event *event = &window->events[window->event_count++];
	memory_zero(event, sizeof(*event));
	event->type = type;
	return event;
}

void os_graphical_reset_events(OS_Window *window)
{
	for (u32 index = 0; index < window->event_count; ++index) {
		if (window->events[index].type == OS_EVENT_FILE_DROP) free((void *)window->events[index].path.data);
	}
	window->event_count = 0;
}

void os_graphical_free_events(OS_Window *window)
{
	os_graphical_reset_events(window);
	free(window->events);
	window->events = NULL;
	window->event_capacity = 0;
}
