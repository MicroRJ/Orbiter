#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include "ui_box.h"
#include "ui.h"

UI_Box *ui_text_box(UI_BoxBuilder *builder, u64 key, String text, UI_BoxDesc desc, UI_TextStyle style);
UI_Box *ui_text_box_sized(UI_BoxBuilder *builder, u64 key, String text, String sizing_text, UI_BoxDesc desc, UI_TextStyle style);

#endif
