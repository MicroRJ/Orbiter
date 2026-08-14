#ifndef OS_H
#define OS_H

#include "os_graphical.h"
#include "os_audio.h"
#include "os_controller.h"
#include "os_dialog.h"

b32 os_init(void);
void os_shutdown(void);
b32 os_set_current_directory_to_executable(void);

#endif
