#include "debugger.h"
#include "ui_widgets.h"
#include "views.h"

static const u64 CPU_VIEW_SCROLLBAR_ID = 0x4350555343524F4Cull;

static void cpu_view_field(UI_Context *ui, u64 key, UI_TextStyle label_style, UI_TextStyle value_style, String sizing_text, const char *label, const char *format, ...)
{
	ui_box_push_id(ui, key);
	ui_push(ui);
	ui_axis(ui, AXIS_X);
	ui_size(ui, AXIS_X, ui_box_fill(1.f));
	ui_gap(ui, 8.f);
	ui_padd(ui, AXIS_Y, 4.f, 4.f);
	ui_box_begin(ui, 1, LIT("CPU field"));
	ui_pop(ui);

	ui_text_box(ui, 1, label_style, "%s", label);

	va_list arguments;
	va_start(arguments, format);
	String value = push_formatted_v(&ui->frame_arena, format, arguments);
	va_end(arguments);
	ui_push(ui);
	ui_size(ui, AXIS_X, ui_box_fill(1.f));
	if (sizing_text.size) ui_text_box_sized_string(ui, 2, value_style, sizing_text, value);
	else ui_text_box_string(ui, 2, value_style, value);
	ui_pop(ui);

	ui_box_end(ui);
	ui_box_pop_id(ui);
}

static void cpu_view_register_row_begin(UI_Context *ui, u64 key)
{
	ui_push(ui);
	ui_axis(ui, AXIS_X);
	ui_size(ui, AXIS_X, ui_box_fill(1.f));
	ui_box_begin(ui, key, LIT("CPU register row"));
	ui_pop(ui);
}

static UI_Box *build_flag_box(UI_Context *ui, u64 key, UI_TextStyle label_style, UI_TextStyle value_style, char name, b32 set)
{
	ui_push(ui);
	ui_axis(ui, AXIS_Y);
	ui_size(ui, AXIS_X, ui_box_flex(1.f, 1.f));
	ui_min_size(ui, AXIS_X, 12.f);
	ui_max_size(ui, AXIS_X, 44.f);
	ui_padd(ui, AXIS_X, 4.f, 4.f);
	ui_padd(ui, AXIS_Y, 4.f, 4.f);
	ui_gap(ui, 3.f);
	UI_Box *cell = ui_box_begin(ui, key, LIT("CPU status flag"));
	ui_pop(ui);

	ui_push(ui);
	ui_size(ui, AXIS_X, ui_box_fill(1.f));
	label_style.align.x = 0.5f;
	ui_text_box(ui, 1, label_style, "%c", name);
	value_style.align.x = 0.5f;
	ui_text_box(ui, 2, value_style, "%u", set);
	ui_pop(ui);

	ui_box_end(ui);
	return cell;
}

static void cpu_view_content(ViewFrameData *frame)
{
	Assert(frame->debugger);
	Assert(frame->publication->valid);
	Assert(frame->draw_box_tree);

	// Published state is observational. A future editable CPU view must send a
	// debugger command instead of modifying this copy or the core directly.
	const NES_CPUState *cpu = &frame->publication->state.cpu;
	UI_TextStyle label_style = frame->ui->theme.code;
	label_style.color = frame->ui->theme.text_subtle;
	UI_TextStyle value_style = frame->ui->theme.code;
	value_style.color = frame->ui->theme.text_vibrant;
	UI_TextStyle section_style = frame->ui->theme.code;
	section_style.color = frame->ui->theme.text_neutral;
	CPUViewState *state = &frame->view->cpu;

	UI_BoxDesc root_desc = ui_box_desc();
	root_desc.axis = AXIS_Y;
	root_desc.size[AXIS_X] = ui_box_fill(1.f);
	root_desc.size[AXIS_Y] = ui_box_fill(1.f);
	root_desc.horz_padd[0] = root_desc.horz_padd[1] = 12.f;
	root_desc.vert_padd[0] = root_desc.vert_padd[1] = 12.f;
	root_desc.overflow[AXIS_X] = UI_BOX_OVERFLOW_CLIP;
	root_desc.overflow[AXIS_Y] = UI_BOX_OVERFLOW_SCROLL;
	UI_Box *root = ui_build_begin(frame->ui, ui_key_child(UI_KEY("CPU view"), frame->view->id), LIT("CPU view"), root_desc);

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

	ui_push(frame->ui);
	ui_padd(frame->ui, AXIS_Y, 4.f, 4.f);
	ui_text_box_string(frame->ui, 4, section_style, LIT("STATUS FLAGS"));
	ui_pop(frame->ui);

	ui_push(frame->ui);
	ui_axis(frame->ui, AXIS_X);
	ui_size(frame->ui, AXIS_X, ui_box_fill(1.f));
	ui_gap(frame->ui, 4.f);
	ui_box_begin(frame->ui, 5, LIT("CPU status flags"));
	ui_pop(frame->ui);

	static const char names[] = "NV1BDIZC";
	static const u8 masks[] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
	UI_Box *flags[8];
	for (u32 index = 0; index < ArrayCount(flags); index++) {
		flags[index] = build_flag_box(frame->ui, index + 1, label_style, value_style, names[index], !!(cpu->P & masks[index]));
	}
	ui_box_end(frame->ui);

	ui_build_end(frame->ui);
	root->scroll_offset.y = state->scroll;
	ui_box_measure(root, (UI_BoxConstraints) { .min = frame->rect.size, .max = frame->rect.size });
	ui_box_layout(root, frame->rect);
	state->scroll = root->scroll_offset.y;

	if (rect_f32_contains(root->rect, frame->ui->mouse) && frame->ui->window->mouse_wheel.y) {
		state->scroll = CLAMP(state->scroll - frame->ui->window->mouse_wheel.y * 48.f, root->scroll_min.y, root->scroll_max.y);
	}
	rect_f32 scrollbar_track = {
		.x = root->viewport.x + root->viewport.w,
		.y = root->viewport.y,
		.w = 12.f,
		.h = root->viewport.h,
	};
	f32 laid_out_scroll = root->scroll_offset.y;
	ui_push_layer(frame->ui, UI_LAYER_HEADER);
	ui_scrollbar(frame->ui, ui_id_child(root->id, CPU_VIEW_SCROLLBAR_ID), scrollbar_track, root->viewport.h, &state->scroll, root->content_size.y);
	ui_pop_layer(frame->ui);
	if (fabsf(state->scroll - laid_out_scroll) > 0.001f)
	{
		root->scroll_offset.y = state->scroll;
		ui_box_relayout(root);
		state->scroll = root->scroll_offset.y;
	}

	for (u32 index = 0; index < ArrayCount(flags); index++)
	{
		ui_draw_rect(frame->ui, flags[index]->rect, cpu->P & masks[index] ? frame->ui->theme.slider_thumb : frame->ui->theme.slider_track);
		ui_draw_rect_outline(frame->ui, flags[index]->rect, 1.f, frame->ui->theme.panel_outline);
	}
	frame->draw_box_tree(root);
}

void cpu_view_frame(ViewFrameData *frame)
{
	ViewFrameData content = view_begin_frame(frame, LIT("CPU"));
	cpu_view_content(&content);
	view_end_frame(&content);
}
