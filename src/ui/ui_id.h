#ifndef UI_ID_H
#define UI_ID_H

#include "base.h"

typedef struct
{
	u64 value;
}
UI_Id;

#define UI_ID_NONE ((UI_Id) { 0 })

UI_Id ui_id_from_ptr(const void *pointer);
UI_Id ui_id_child(UI_Id parent, u64 child);
b32 ui_id_equal(UI_Id a, UI_Id b);

#endif
