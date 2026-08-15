#include "nes_process.h"
#include "ui_widgets.h"
#include "views.h"

static void cpu_view_field(UI_Context *ui, u64 key, UI_TextStyle label_style, UI_TextStyle value_style, Str sizing_text, const char *label, const char *format, ...)
{
	ui_box_push_id(ui, key);
	ui_clean(ui);
	ui_axis(ui, AXIS_X);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_gap(ui, 8.f);
	ui_padd(ui, AXIS_Y, 4.f, 4.f);
	ui_box_begin(ui, 1, LIT("CPU field"));

	ui_clean(ui);
	ui_text_box(ui, 1, label_style, "%s", label);

	va_list arguments;
	va_start(arguments, format);
	Str value = str_push_copy_v(&ui->frame_arena, format, arguments);
	va_end(arguments);
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	if (sizing_text.size) ui_text_sized(ui, 2, value_style, sizing_text, value);
	else ui_text(ui, 2, value_style, value);

	ui_box_end(ui);
	ui_box_pop_id(ui);
}

static void cpu_view_register_row_begin(UI_Context *ui, u64 key)
{
	ui_clean(ui);
	ui_axis(ui, AXIS_X);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_box_begin(ui, key, LIT("CPU register row"));
}

static UI_Box *build_flag_box(UI_Context *ui, u64 key, UI_TextStyle label_style, UI_TextStyle value_style, char name, b32 set)
{
	ui_clean(ui);
	ui_axis(ui, AXIS_Y);
	ui_size(ui, AXIS_X, ui_flex(1.f, 1.f));
	ui_min_size(ui, AXIS_X, 12.f);
	ui_max_size(ui, AXIS_X, 44.f);
	ui_padd(ui, AXIS_X, 4.f, 4.f);
	ui_padd(ui, AXIS_Y, 4.f, 4.f);
	ui_gap(ui, 3.f);
	UI_Box *cell = ui_box_begin(ui, key, LIT("CPU status flag"));

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	label_style.align.x = 0.5f;
	ui_text_box(ui, 1, label_style, "%c", name);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	value_style.align.x = 0.5f;
	ui_text_box(ui, 2, value_style, "%u", set);

	ui_box_end(ui);
	cell->paint.flags |= UI_BOX_DRAW_BACKGROUND | UI_BOX_DRAW_BORDER;
	cell->paint.background = set ? ui->theme.slider_thumb : ui->theme.slider_track;
	cell->paint.border = ui->theme.panel_outline;
	cell->paint.border_width = 1.f;
	return cell;
}

static void cpu_view_content(ViewFrameData *frame)
{
	Assert(frame->emulator);

	// Published state is observational. A future editable CPU view must send a
	// debugger command instead of modifying this copy or the core directly.
	const NES_CPUState *cpu = &frame->publication->cpu;
	UI_TextStyle label_style = frame->ui->theme.code;
	label_style.color = frame->ui->theme.text_subtle;
	UI_TextStyle value_style = frame->ui->theme.code;
	value_style.color = frame->ui->theme.text_vibrant;
	UI_TextStyle section_style = frame->ui->theme.code;
	section_style.color = frame->ui->theme.text_neutral;

	ui_clean(frame->ui);
	ui_size(frame->ui, AXIS_X, ui_grow(1.f));
	ui_size(frame->ui, AXIS_Y, ui_grow(1.f));
	ui_box_begin(frame->ui, ui_key_child(UI_KEY("CPU view"), frame->view->id), LIT("CPU view"));

	ui_clean(frame->ui);
	ui_size(frame->ui, AXIS_X, ui_grow(1.f));
	ui_size(frame->ui, AXIS_Y, ui_grow(1.f));
	UI_ScrollBox *scroll = ui_scroll_box_begin(frame->ui, 1, AXIS_Y);

	ui_clean(frame->ui);
	ui_axis(frame->ui, AXIS_Y);
	ui_size(frame->ui, AXIS_X, ui_grow(1.f));
	ui_size(frame->ui, AXIS_Y, ui_grow(1.f));
	ui_padd(frame->ui, AXIS_X, 12.f, 12.f);
	ui_padd(frame->ui, AXIS_Y, 12.f, 12.f);
	ui_overflow(frame->ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_box_begin(frame->ui, 1, LIT("CPU viewport"));

	cpu_view_register_row_begin(frame->ui, 1);
	cpu_view_field(frame->ui, 1, label_style, value_style, LIT("$FF"), "A", "$%02X", cpu->A);
	cpu_view_field(frame->ui, 2, label_style, value_style, LIT("$FF"), "X", "$%02X", cpu->X);
	cpu_view_field(frame->ui, 3, label_style, value_style, LIT("$FF"), "Y", "$%02X", cpu->Y);
	ui_box_end(frame->ui);

	cpu_view_register_row_begin(frame->ui, 2);
	cpu_view_field(frame->ui, 1, label_style, value_style, LIT("$FF"), "S", "$%02X", cpu->S);
	cpu_view_field(frame->ui, 2, label_style, value_style, LIT("$FFFF"), "PC", "$%04X", cpu->PC);
	cpu_view_field(frame->ui, 3, label_style, value_style, LIT("$FF"), "P", "$%02X", cpu->P);
	ui_box_end(frame->ui);

	cpu_view_register_row_begin(frame->ui, 3);
	cpu_view_field(frame->ui, 1, label_style, value_style, LIT("$FFFF"), "STACK", "$%04X", 0x0100 | cpu->S);
	ui_box_end(frame->ui);

	ui_clean(frame->ui);
	ui_padd(frame->ui, AXIS_Y, 4.f, 4.f);
	ui_text(frame->ui, 4, section_style, LIT("STATUS FLAGS"));

	ui_clean(frame->ui);
	ui_axis(frame->ui, AXIS_X);
	ui_size(frame->ui, AXIS_X, ui_grow(1.f));
	ui_gap(frame->ui, 4.f);
	ui_box_begin(frame->ui, 5, LIT("CPU status flags"));

	static const char names[] = "NV1BDIZC";
	static const u8 masks[] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
	for (u32 index = 0; index < ArrayCount(masks); index++) {
		build_flag_box(frame->ui, index + 1, label_style, value_style, names[index], !!(cpu->P & masks[index]));
	}
	ui_box_end(frame->ui);

	ui_box_end(frame->ui);
	ui_scroll_box_end(scroll);
	ui_box_end(frame->ui);
}

void cpu_view_build_ui(ViewFrameData *frame)
{
	ViewFrameData content = view_begin_frame(frame, LIT("CPU"));
	cpu_view_content(&content);
	view_end_frame(&content);
}
