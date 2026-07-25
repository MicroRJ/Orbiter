#include "debugger.h"
#include "views.h"

static
void nes_cpu_table_set_field(UI_Table *table, u32 row, u32 label_column,
	String label, String value)
{
	UI_TextStyle label_style = table->ui->theme.code;
	label_style.color = table->ui->theme.text_subtle;
	UI_TextStyle value_style = table->ui->theme.code;
	value_style.color = table->ui->theme.text_vibrant;
	ui_table_set_text(table, row, label_column, label_style, label);
	ui_table_set_text(table, row, label_column + 1, value_style, value);
}

static
void nes_cpu_view_draw_flags(ViewFrameData *frame, rect_f32 rect, u32 status)
{
	static const char names[] = "NV1BDIZC";
	static const u8 masks[] = {
		0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
	};
	f32 spacing = 4.f;
	f32 cell_width = Max(12.f,
		Min(44.f, (rect.w - spacing * 7.f) / 8.f));
	UI_TextStyle label_style = frame->ui->theme.code;
	label_style.color = frame->ui->theme.text_subtle;
	UI_TextStyle value_style = frame->ui->theme.code;
	value_style.color = frame->ui->theme.text_vibrant;

	for (u32 index = 0; index < 8; ++index)
	{
		rect_f32 cell = rect_f32_slice(&rect, AXIS_X, cell_width);
		b32 set = !!(status & masks[index]);
		ui_draw_rect(frame->ui, cell, set
			? frame->ui->theme.slider_thumb
			: frame->ui->theme.slider_track);
		ui_draw_rect_outline(frame->ui, cell, 1.f,
			frame->ui->theme.panel_outline);

		String name = string_from_data((char *)&names[index], 1);
		rect_f32 name_rect = cell;
		name_rect.x += cell_width * 0.5f - 6.f;
		name_rect.y += 2.f;
		ui_draw_text(frame->ui, name_rect, label_style, name);

		String value = push_formatted(frame->scratch, "%u", set);
		rect_f32 value_rect = cell;
		value_rect.x += cell_width * 0.5f - 6.f;
		value_rect.y += 21.f;
		ui_draw_text(frame->ui, value_rect, value_style, value);

		rect.x += spacing;
	}
}

static void cpu_view_content(ViewFrameData *frame)
{
	Assert(frame->debugger);
	Assert(frame->publication->valid);

	// Published state is observational. A future editable CPU view must send a
	// debugger command instead of modifying this copy or the core directly.
	const NES_CPUState *cpu = &frame->publication->state.cpu;
	UI_TextStyle section_style = frame->ui->theme.code;
	section_style.color = frame->ui->theme.text_neutral;

	rect_f32 layout = rect_f32_inset(frame->rect, 12.f);
	f32 row_height = frame->ui->theme.code.size + 8.f;

	// Fixed label widths made longer names collide with their values. Content
	// columns now use measured font bounds and values consume the remaining room.
	rect_f32 registers_rect = rect_f32_slice(&layout, AXIS_Y, row_height * 2.f);
	UI_Table registers = ui_table_begin(frame->ui, frame->scratch, registers_rect, 2, 6, row_height);
	for (u32 column = 0; column < registers.column_count; ++column)
	{
		ui_table_set_column(&registers, column, (column & 1)
			? ui_table_column_flex(1.f)
			: ui_table_column_content());
	}
	nes_cpu_table_set_field(&registers, 0, 0, LIT("A"),
		push_formatted(frame->scratch, "$%02X", cpu->A));
	nes_cpu_table_set_field(&registers, 0, 2, LIT("X"),
		push_formatted(frame->scratch, "$%02X", cpu->X));
	nes_cpu_table_set_field(&registers, 0, 4, LIT("Y"),
		push_formatted(frame->scratch, "$%02X", cpu->Y));
	nes_cpu_table_set_field(&registers, 1, 0, LIT("S"),
		push_formatted(frame->scratch, "$%02X", cpu->S));
	nes_cpu_table_set_field(&registers, 1, 2, LIT("PC"),
		push_formatted(frame->scratch, "$%04X", cpu->PC));
	nes_cpu_table_set_field(&registers, 1, 4, LIT("P"),
		push_formatted(frame->scratch, "$%02X", cpu->P));
	ui_table_draw(&registers);

	rect_f32 details_rect = rect_f32_slice(&layout, AXIS_Y, row_height);
	UI_Table details = ui_table_begin(frame->ui, frame->scratch,
		details_rect, 1, 2, row_height);
	ui_table_set_column(&details, 0, ui_table_column_content());
	ui_table_set_column(&details, 1, ui_table_column_flex(1.f));
	nes_cpu_table_set_field(&details, 0, 0, LIT("STACK"),
		push_formatted(frame->scratch, "$%04X", 0x0100 | cpu->S));
	ui_table_draw(&details);

	rect_f32 flags_title = rect_f32_slice(&layout, AXIS_Y, row_height);
	ui_draw_text(frame->ui, flags_title, section_style, LIT("STATUS FLAGS"));
	rect_f32 flags = rect_f32_slice(&layout, AXIS_Y, 46.f);
	nes_cpu_view_draw_flags(frame, flags, cpu->P);
}

void cpu_view_frame(ViewFrameData *frame)
{
	ViewFrameData content = view_begin_frame(frame, LIT("CPU"));
	cpu_view_content(&content);
	view_end_frame(&content);
}
