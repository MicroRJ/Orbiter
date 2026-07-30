#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include "ui_box.h"
#include "ui.h"

UI_Box *ui_text_box(UI_Context *ui, UI_Key key, UI_TextStyle style, const char *format, ...) __attribute__((format(printf, 4, 5)));
UI_Box *ui_text_box_sized(UI_Context *ui, UI_Key key, UI_TextStyle style, String sizing_text, const char *format, ...) __attribute__((format(printf, 5, 6)));
UI_Box *ui_text_box_string(UI_Context *ui, UI_Key key, UI_TextStyle style, String text);
UI_Box *ui_text_box_sized_string(UI_Context *ui, UI_Key key, UI_TextStyle style, String sizing_text, String text);

UI_Box *ui_text_box_desc(UI_Context *ui, UI_Key key, UI_BoxDesc desc, UI_TextStyle style, const char *format, ...) __attribute__((format(printf, 5, 6)));
UI_Box *ui_text_box_sized_desc(UI_Context *ui, UI_Key key, UI_BoxDesc desc, UI_TextStyle style, String sizing_text, const char *format, ...) __attribute__((format(printf, 6, 7)));
UI_Box *ui_text_box_string_desc(UI_Context *ui, UI_Key key, UI_BoxDesc desc, UI_TextStyle style, String text);
UI_Box *ui_text_box_sized_string_desc(UI_Context *ui, UI_Key key, UI_BoxDesc desc, UI_TextStyle style, String sizing_text, String text);

UI_Response ui_button(UI_Context *ui, UI_Key key, String text);

void ui_box_paint(UI_Box *box);

typedef struct
{
	UI_Context *ui;
	UI_Box *root;
	UI_Box *viewport;
	UI_Box *track;
	UI_Box *space_before;
	UI_Box *thumb;
	UI_Box *space_after;
	AXIS axis;
	b32 has_previous;
	b32 reset;
	f32 offset;
	f32 target;
	u32 parent_count;
	u32 desc_count;
}
UI_Scroll;

// The scope must build exactly one box. That box becomes the clipped viewport;
// a virtual list can therefore be used directly without a wrapper.
UI_Scroll *ui_scroll_begin(UI_Context *ui, UI_Key key, AXIS axis);
void ui_scroll_reset(UI_Scroll *scroll);
void ui_scroll_end(UI_Scroll *scroll);

typedef enum
{
	UI_BOX_TABLE_COLUMN_CONTENT,
	UI_BOX_TABLE_COLUMN_FIXED,
	UI_BOX_TABLE_COLUMN_FLEX,
}
UI_BoxTableColumnKind;

typedef struct
{
	UI_BoxTableColumnKind kind;
	f32 value;
}
UI_BoxTableColumn;

typedef struct
{
	const UI_BoxTableColumn *columns;
	u32 column_count;
	f32 row_height;
	f32 column_gap;
	f32 row_gap;
	vec2 cell_padd;
}
UI_BoxTableDesc;

typedef struct
{
	UI_Context *ui;
	UI_Box *box;
	UI_Box *row;
	UI_Box *cell;
	UI_BoxTableDesc desc;
	u32 column_index;
}
UI_BoxTable;

UI_BoxTableColumn ui_box_table_content(void);
UI_BoxTableColumn ui_box_table_fixed(f32 width);
UI_BoxTableColumn ui_box_table_flex(f32 weight);
UI_BoxTable ui_box_table_begin(UI_Context *ui, UI_Key key, String name, UI_BoxTableDesc desc);
UI_Box *ui_box_table_row_begin(UI_BoxTable *table, UI_Key key);
void ui_box_table_row_end(UI_BoxTable *table);
UI_Box *ui_box_table_cell_begin(UI_BoxTable *table);
void ui_box_table_cell_end(UI_BoxTable *table);
UI_Box *ui_box_table_end(UI_BoxTable *table);

#endif
