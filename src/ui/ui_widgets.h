#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include "ui_box.h"
#include "ui.h"

UI_Box *ui_text_box(UI_BoxBuilder *builder, u64 key, UI_TextStyle style, const char *format, ...) __attribute__((format(printf, 4, 5)));
UI_Box *ui_text_box_sized(UI_BoxBuilder *builder, u64 key, UI_TextStyle style, String sizing_text, const char *format, ...) __attribute__((format(printf, 5, 6)));
UI_Box *ui_text_box_string(UI_BoxBuilder *builder, u64 key, UI_TextStyle style, String text);
UI_Box *ui_text_box_sized_string(UI_BoxBuilder *builder, u64 key, UI_TextStyle style, String sizing_text, String text);

UI_Box *ui_text_box_desc(UI_BoxBuilder *builder, u64 key, UI_BoxDesc desc, UI_TextStyle style, const char *format, ...) __attribute__((format(printf, 5, 6)));
UI_Box *ui_text_box_sized_desc(UI_BoxBuilder *builder, u64 key, UI_BoxDesc desc, UI_TextStyle style, String sizing_text, const char *format, ...) __attribute__((format(printf, 6, 7)));
UI_Box *ui_text_box_string_desc(UI_BoxBuilder *builder, u64 key, UI_BoxDesc desc, UI_TextStyle style, String text);
UI_Box *ui_text_box_sized_string_desc(UI_BoxBuilder *builder, u64 key, UI_BoxDesc desc, UI_TextStyle style, String sizing_text, String text);

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
	UI_BoxBuilder *builder;
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
UI_BoxTable ui_box_table_begin(UI_BoxBuilder *builder, u64 key, String name, UI_BoxTableDesc desc);
UI_Box *ui_box_table_row_begin(UI_BoxTable *table, u64 key);
void ui_box_table_row_end(UI_BoxTable *table);
UI_Box *ui_box_table_cell_begin(UI_BoxTable *table);
void ui_box_table_cell_end(UI_BoxTable *table);
UI_Box *ui_box_table_end(UI_BoxTable *table);

#endif
