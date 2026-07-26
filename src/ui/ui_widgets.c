#include "ui_widgets.h"

typedef struct
{
	String string;
	String sizing_string;
	UI_TextStyle style;
}
UI_TextBoxData;

typedef struct
{
	UI_BoxTableColumn *columns;
	f32 *natural_widths;
	f32 *resolved_widths;
	u32 column_count;
	f32 row_height;
	f32 column_gap;
}
UI_BoxTableData;

static vec2 ui_box__measure_text(UI_Box *box, UI_BoxConstraints constraints)
{
	(void)constraints;
	UI_TextBoxData *text = box->content;
	String measured_string = text->sizing_string.size ? text->sizing_string : text->string;
	return ui_measure_text(box->ui, text->style, measured_string);
}

static void ui_box__paint_text(UI_Box *box)
{
	UI_TextBoxData *text = box->content;
	vec2 text_size = ui_measure_text(box->ui, text->style, text->string);
	vec2 remaining = v2(Max(0.f, box->viewport.w - text_size.x), Max(0.f, box->viewport.h - text_size.y));
	vec2 position = v2_add(box->viewport.pos, v2_mul(remaining, text->style.align));
	ui_push_clip(box->ui, box->clip_rect);
	ui_push_clip(box->ui, box->viewport);
	ui_draw_text(box->ui, (rect_f32) { .pos = position, .size = text_size }, text->style, text->string);
	ui_pop_clip(box->ui);
	ui_pop_clip(box->ui);
}

static const UI_BoxOps ui_box__text_ops = {
	.measure = ui_box__measure_text,
	.paint = ui_box__paint_text,
};

static UI_Box *ui_box__make_text(UI_BoxBuilder *builder, u64 key, UI_BoxDesc *desc, UI_TextStyle style, String sizing_string, String string)
{
	Assert(builder);
	Assert(builder->ui);
	Assert(builder->ui->text);
	Assert(style.font);
	Assert(style.size > 0);
	UI_TextBoxData *text = arena_push_zero(builder->arena, sizeof(*text));
	text->string = string;
	text->sizing_string = sizing_string;
	text->style = style;
	UI_Box *box = desc ? ui_box_make_desc(builder, key, string, *desc) : ui_box_make(builder, key, string);
	box->ops = &ui_box__text_ops;
	box->content = text;
	return box;
}

UI_Box *ui_text_box(UI_BoxBuilder *builder, u64 key, UI_TextStyle style, const char *format, ...)
{
	Assert(builder);
	va_list arguments;
	va_start(arguments, format);
	String string = push_formatted_v(builder->arena, format, arguments);
	va_end(arguments);
	return ui_box__make_text(builder, key, 0, style, (String) {}, string);
}

UI_Box *ui_text_box_sized(UI_BoxBuilder *builder, u64 key, UI_TextStyle style, String sizing_string, const char *format, ...)
{
	Assert(builder);
	va_list arguments;
	va_start(arguments, format);
	String string = push_formatted_v(builder->arena, format, arguments);
	va_end(arguments);
	return ui_box__make_text(builder, key, 0, style, sizing_string, string);
}

UI_Box *ui_text_box_string(UI_BoxBuilder *builder, u64 key, UI_TextStyle style, String string)
{
	return ui_box__make_text(builder, key, 0, style, (String) {}, string);
}

UI_Box *ui_text_box_sized_string(UI_BoxBuilder *builder, u64 key, UI_TextStyle style, String sizing_string, String string)
{
	return ui_box__make_text(builder, key, 0, style, sizing_string, string);
}

UI_Box *ui_text_box_desc(UI_BoxBuilder *builder, u64 key, UI_BoxDesc desc, UI_TextStyle style, const char *format, ...)
{
	Assert(builder);
	va_list arguments;
	va_start(arguments, format);
	String string = push_formatted_v(builder->arena, format, arguments);
	va_end(arguments);
	return ui_box__make_text(builder, key, &desc, style, (String) {}, string);
}

UI_Box *ui_text_box_sized_desc(UI_BoxBuilder *builder, u64 key, UI_BoxDesc desc, UI_TextStyle style, String sizing_string, const char *format, ...)
{
	Assert(builder);
	va_list arguments;
	va_start(arguments, format);
	String string = push_formatted_v(builder->arena, format, arguments);
	va_end(arguments);
	return ui_box__make_text(builder, key, &desc, style, sizing_string, string);
}

UI_Box *ui_text_box_string_desc(UI_BoxBuilder *builder, u64 key, UI_BoxDesc desc, UI_TextStyle style, String string)
{
	return ui_box__make_text(builder, key, &desc, style, (String) {}, string);
}

UI_Box *ui_text_box_sized_string_desc(UI_BoxBuilder *builder, u64 key, UI_BoxDesc desc, UI_TextStyle style, String sizing_string, String string)
{
	return ui_box__make_text(builder, key, &desc, style, sizing_string, string);
}

UI_BoxTableColumn ui_box_table_content(void)
{
	return (UI_BoxTableColumn) { .kind = UI_BOX_TABLE_COLUMN_CONTENT };
}

UI_BoxTableColumn ui_box_table_fixed(f32 width)
{
	return (UI_BoxTableColumn) { .kind = UI_BOX_TABLE_COLUMN_FIXED, .value = Max(0.f, width) };
}

UI_BoxTableColumn ui_box_table_flex(f32 weight)
{
	return (UI_BoxTableColumn) { .kind = UI_BOX_TABLE_COLUMN_FLEX, .value = Max(0.f, weight) };
}

static vec2 ui_box__measure_table(UI_Box *box, UI_BoxConstraints constraints)
{
	(void)constraints;
	UI_BoxTableData *table = box->content;
	Assert(table);
	memory_zero(table->natural_widths, table->column_count * sizeof(*table->natural_widths));

	for (u32 row_index = 0; row_index < box->child_count; row_index++)
	{
		UI_Box *row = box->children[row_index];
		Assert(row->child_count == table->column_count);
		for (u32 column = 0; column < table->column_count; column++)
		{
			UI_Box *cell = row->children[column];
			cell->desc.size[AXIS_X] = ui_box_content();
			vec2 measured = ui_box_measure(cell, (UI_BoxConstraints) { .max = v2(UI_BOX_INFINITY, table->row_height) });
			table->natural_widths[column] = Max(table->natural_widths[column], measured.x);
		}
	}

	f32 table_width = table->column_gap * Max((i32)table->column_count - 1, 0);
	for (u32 column = 0; column < table->column_count; column++)
	{
		UI_BoxTableColumn spec = table->columns[column];
		table->resolved_widths[column] = spec.kind == UI_BOX_TABLE_COLUMN_FIXED ? spec.value : table->natural_widths[column];
		table_width += table->resolved_widths[column];
	}
	for (u32 row_index = 0; row_index < box->child_count; row_index++)
	{
		UI_Box *row = box->children[row_index];
		row->measured_size = row->arranged_size = v2(table_width, table->row_height);
		for (u32 column = 0; column < table->column_count; column++)
		{
			UI_Box *cell = row->children[column];
			cell->desc.size[AXIS_X] = ui_box_pixels(table->resolved_widths[column]);
			cell->measured_size.x = cell->arranged_size.x = table->resolved_widths[column];
		}
	}

	f32 table_height = table->row_height * box->child_count + box->desc.gap * Max((i32)box->child_count - 1, 0);
	return v2(table_width, table_height);
}

static void ui_box__prepare_table_layout(UI_Box *box)
{
	UI_BoxTableData *table = box->content;
	f32 committed_width = table->column_gap * Max((i32)table->column_count - 1, 0);
	f32 flex_weight = 0.f;
	for (u32 column = 0; column < table->column_count; column++)
	{
		UI_BoxTableColumn spec = table->columns[column];
		if (spec.kind == UI_BOX_TABLE_COLUMN_FIXED) {
			table->resolved_widths[column] = spec.value;
			committed_width += spec.value;
		}
		else if (spec.kind == UI_BOX_TABLE_COLUMN_CONTENT) {
			table->resolved_widths[column] = table->natural_widths[column];
			committed_width += table->natural_widths[column];
		}
		else {
			flex_weight += spec.value;
		}
	}
	f32 flex_space = Max(0.f, box->viewport.w - committed_width);
	for (u32 column = 0; column < table->column_count; column++)
	{
		UI_BoxTableColumn spec = table->columns[column];
		if (spec.kind == UI_BOX_TABLE_COLUMN_FLEX) {
			table->resolved_widths[column] = flex_weight > 0.f ? flex_space * spec.value / flex_weight : table->natural_widths[column];
		}
	}

	f32 table_width = table->column_gap * Max((i32)table->column_count - 1, 0);
	for (u32 column = 0; column < table->column_count; column++) {
		table_width += table->resolved_widths[column];
	}
	for (u32 row_index = 0; row_index < box->child_count; row_index++)
	{
		UI_Box *row = box->children[row_index];
		row->measured_size.x = row->arranged_size.x = table_width;
		for (u32 column = 0; column < table->column_count; column++)
		{
			UI_Box *cell = row->children[column];
			cell->desc.size[AXIS_X] = ui_box_pixels(table->resolved_widths[column]);
			cell->measured_size.x = cell->arranged_size.x = table->resolved_widths[column];
		}
	}
}

static const UI_BoxOps ui_box__table_ops = {
	.measure_children = ui_box__measure_table,
	.prepare_layout = ui_box__prepare_table_layout,
};

UI_BoxTable ui_box_table_begin(UI_BoxBuilder *builder, u64 key, String name, UI_BoxTableDesc desc)
{
	Assert(builder);
	Assert(desc.columns);
	Assert(desc.column_count);
	Assert(desc.row_height > 0.f);
	UI_BoxTableData *data = arena_push_zero(builder->arena, sizeof(*data));
	data->columns = arena_push_copy(builder->arena, desc.column_count * sizeof(*data->columns), desc.columns);
	data->natural_widths = arena_push_zero(builder->arena, desc.column_count * sizeof(*data->natural_widths));
	data->resolved_widths = arena_push_zero(builder->arena, desc.column_count * sizeof(*data->resolved_widths));
	data->column_count = desc.column_count;
	data->row_height = desc.row_height;
	data->column_gap = desc.column_gap;

	UI_BoxDesc box_desc = builder->desc;
	box_desc.axis = AXIS_Y;
	box_desc.gap = desc.row_gap;
	UI_Box *box = ui_box_begin_desc(builder, key, name, box_desc);
	box->content = data;
	box->ops = &ui_box__table_ops;
	return (UI_BoxTable) {
		.builder = builder,
		.box = box,
		.desc = desc,
	};
}

UI_Box *ui_box_table_row_begin(UI_BoxTable *table, u64 key)
{
	Assert(table);
	Assert(table->builder);
	Assert(!table->row);
	UI_BoxDesc desc = ui_box_desc();
	desc.axis = AXIS_X;
	desc.size[AXIS_X] = ui_box_fill(1.f);
	desc.size[AXIS_Y] = ui_box_pixels(table->desc.row_height);
	desc.gap = table->desc.column_gap;
	table->row = ui_box_begin_desc(table->builder, key, LIT("table row"), desc);
	table->column_index = 0;
	return table->row;
}

void ui_box_table_row_end(UI_BoxTable *table)
{
	Assert(table);
	Assert(table->row);
	Assert(!table->cell);
	Assert(table->column_index == table->desc.column_count);
	ui_box_end(table->builder);
	table->row = 0;
}

UI_Box *ui_box_table_cell_begin(UI_BoxTable *table)
{
	Assert(table);
	Assert(table->row);
	Assert(!table->cell);
	Assert(table->column_index < table->desc.column_count);
	UI_BoxDesc desc = ui_box_desc();
	desc.size[AXIS_Y] = ui_box_fill(1.f);
	desc.horz_padd[0] = desc.horz_padd[1] = table->desc.cell_padd.x;
	desc.vert_padd[0] = desc.vert_padd[1] = table->desc.cell_padd.y;
	desc.overflow[AXIS_X] = UI_BOX_OVERFLOW_CLIP;
	table->cell = ui_box_begin_desc(table->builder, table->column_index + 1, LIT("table cell"), desc);
	table->column_index++;
	return table->cell;
}

void ui_box_table_cell_end(UI_BoxTable *table)
{
	Assert(table);
	Assert(table->cell);
	ui_box_end(table->builder);
	table->cell = 0;
}

UI_Box *ui_box_table_end(UI_BoxTable *table)
{
	Assert(table);
	Assert(table->box);
	Assert(!table->row);
	Assert(!table->cell);
	ui_box_end(table->builder);
	return table->box;
}
