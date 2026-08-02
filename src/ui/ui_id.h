#ifndef UI_ID_H
#define UI_ID_H

#include "base.h"

typedef struct
{
	u64 value;
}
UI_Id;

// Keys are local to their parent. Strings are hashed once when the UI is built;
// integers remain useful for data-driven and repeated elements.
typedef u64 UI_Key;

#define UI_ID_NONE ((UI_Id) { 0 })
#define UI_KEY(string) ui_key_string(LIT(string))

UI_Id ui_id_from_ptr(const void *pointer);
UI_Key ui_key_string(Str string);
UI_Key ui_key_child(UI_Key parent, UI_Key child);
UI_Id ui_id_child(UI_Id parent, UI_Key child);
b32 ui_id_equal(UI_Id a, UI_Id b);

#endif
