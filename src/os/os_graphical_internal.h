#ifndef OS_GRAPHICAL_INTERNAL_H
#define OS_GRAPHICAL_INTERNAL_H

#include "os_graphical.h"

OS_Event *os_graphical_push_event(OS_Window *window, OS_EventType type);
void os_graphical_reset_events(OS_Window *window);
void os_graphical_free_events(OS_Window *window);

#endif
