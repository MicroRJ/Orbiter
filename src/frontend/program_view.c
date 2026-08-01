#include "debugger.h"
#include "nes/isa.h"
#include "program.h"
#include "views.h"

typedef struct
{
	ViewFrameData frame;
	rect_f32 clip;
}
ProgramBoxData;

static String program_format_operand(Arena *arena, u16 base,
	u32 type, u32 data)
{
	NES_InstructionDesc desc = nes_instruction_desc(type);
	switch (desc.mode)
	{
		case IMP: return push_append_string(arena, LIT(""));
		case ACC: return push_append_string(arena, LIT("A"));
		case IMM: return push_append_formatted(arena, "#$%02X", (u32)(u8)data);
		case ZPG: return push_append_formatted(arena, "$%02X", (u32)(u8)data);
		case ZPX: return push_append_formatted(arena, "$%02X,X", (u32)(u8)data);
		case ZPY: return push_append_formatted(arena, "$%02X,Y", (u32)(u8)data);
		case ABS: return push_append_formatted(arena, "$%04X", (u32)(u16)data);
		case ABX: return push_append_formatted(arena, "$%04X,X", (u32)(u16)data);
		case ABY: return push_append_formatted(arena, "$%04X,Y", (u32)(u16)data);
		case IND: return push_append_formatted(arena, "[$%04X]", (u32)(u16)data);
		case INX: return push_append_formatted(arena, "[$%02X,X]", (u32)(u8)data);
		case INY: return push_append_formatted(arena, "[$%02X],Y", (u32)(u8)data);
		case REL: return push_append_formatted(arena, "$%04X", (u32)(u16)(base + 2 + (i8)data));
	}
	return LIT("<internal error>");
}

static const char *program_addressing_mode_name(NES_AddressingMode mode)
{
	switch (mode)
	{
		case IMP: return "IMPLIED";
		case ACC: return "ACCUMULATOR";
		case IMM: return "IMMEDIATE";
		case REL: return "RELATIVE";
		case ZPG: return "ZERO PAGE";
		case ZPX: return "ZERO PAGE, X";
		case ZPY: return "ZERO PAGE, Y";
		case ABS: return "ABSOLUTE";
		case ABY: return "ABSOLUTE, Y";
		case ABX: return "ABSOLUTE, X";
		case INX: return "INDIRECT, X";
		case INY: return "INDIRECT, Y";
		case IND: return "INDIRECT";
	}
	return "UNKNOWN";
}

static const char *program_opcode_class_name(NES_OpcodeClass classification)
{
	switch (classification)
	{
		case NES_OPCODE_OFFICIAL: return "OFFICIAL";
		case NES_OPCODE_UNOFFICIAL: return "UNOFFICIAL";
		case NES_OPCODE_UNSTABLE: return "UNSTABLE";
		case NES_OPCODE_HALT: return "HALT";
	}
	return "UNKNOWN";
}

static void program_view_clamp_scroll(ViewState *state, rect_f32 viewport, f32 header_height, f32 row_height, u32 instruction_count)
{
	f32 max_scroll = Max(header_height + instruction_count * row_height - viewport.h, 0.f);
	state->scroll_target = Min(state->scroll_target, max_scroll);
	state->scroll = Min(state->scroll, max_scroll);
}

static void program_draw_instruction_tooltip(ViewFrameData *frame, rect_f32 hit_rect, ProgramInstruction instruction, NES_InstructionDesc desc, String formatted_instruction)
{
	UI_Context *ui = frame->ui;
	if (!rect_f32_contains(hit_rect, ui->mouse)) {
		return;
	}
	String lines[3] = {};
	lines[0] = push_formatted(frame->scratch, "$%04X   OPCODE $%02X   %.*s", instruction.cpu_address, instruction.type, formatted_instruction.size, formatted_instruction.text);
	lines[1] = push_formatted(frame->scratch, "%s   %s   %u BYTE%s", program_opcode_class_name(desc.classification), program_addressing_mode_name(desc.mode), desc.size, desc.size == 1 ? "" : "S");
	lines[2].text = begin_append_sequence(frame->scratch);
	push_append_formatted(frame->scratch, "%u CYCLE%s", desc.cycles, desc.cycles == 1 ? "" : "S");
	if (desc.page_cross_cycles) {
		push_append_formatted(frame->scratch, "   +%u PAGE CROSS", desc.page_cross_cycles);
	}
	if (desc.branch_taken_cycles) {
		push_append_formatted(frame->scratch, "   +%u BRANCH TAKEN", desc.branch_taken_cycles);
	}
	lines[2].size = end_append_sequence(frame->scratch, lines[2].text);
	UI_TextStyle style = ui->theme.code;
	style.color = ui->theme.text_neutral;
	f32 line_height = style.size + 4.f;
	f32 width = 0.f;
	for (u32 index = 0; index < ArrayCount(lines); ++index) {
		width = Max(width, ui_measure_text(ui, style, lines[index]).x);
	}
	f32 padding = 8.f;
	rect_f32 tooltip = {
		.x = ui->mouse.x + 14.f,
		.y = ui->mouse.y + 18.f,
		.w = width + padding * 2.f,
		.h = ArrayCount(lines) * line_height + padding * 2.f,
	};
	tooltip.x = CLAMP(tooltip.x, frame->rect.x, Max(frame->rect.x, frame->rect.x + frame->rect.w - tooltip.w));
	tooltip.y = CLAMP(tooltip.y, frame->rect.y, Max(frame->rect.y, frame->rect.y + frame->rect.h - tooltip.h));
	ui_push_z(ui, UI_Z_OVERLAY);
	ui_push_unclipped(ui);
	ui_draw_backdrop(ui, tooltip, 5.f);
	rect_f32 text = rect_f32_inset(tooltip, padding);
	for (u32 index = 0; index < ArrayCount(lines); ++index)
	{
		ui_draw_text(ui, text, style, lines[index]);
		text.y += line_height;
	}
	ui_pop_unclipped(ui);
	ui_pop_z(ui);
}

static void program_view_content(ViewFrameData *frame)
{
	Debugger *debugger = frame->debugger;
	Assert(debugger);

	ViewState *state = &frame->view->program;
	UI_Context *ui = frame->ui;
	rect_f32 main_rect = frame->rect;
	main_rect.y -= frame->header_height;
	main_rect.h += frame->header_height;
	Arena *scratch = frame->scratch;
	UI_TextStyle font = ui->theme.code;
	if (!frame->publication->valid || !debugger_armed(debugger))
	{
		ui_draw_text(ui, main_rect, font, LIT("No cartridge loaded - Ctrl+O to open an iNES ROM"));
		return;
	}

	const Program *program = debugger_program(debugger);
	u32 instruction_count = program_mapped_instruction_count(debugger);
	f32 row_height = font.size;

	NES_CPUState cpu = frame->publication->cpu;

	rect_f32_slice(&main_rect, AXIS_X, 32);
	f32 scroll_height = frame->header_height + instruction_count * row_height;
	f32 max_scroll = Max(scroll_height - main_rect.h, 0.f);
	UI_Id scroll_id = ui_id_child(ui_id_child(UI_ID_NONE, ui_key_child(UI_KEY("program view"), frame->view->id)), 1);
	b32 wheel_scroll = rect_f32_contains(main_rect, ui->mouse) && ui->window->mouse_wheel.y;
	if (wheel_scroll) {
		state->scroll_target = CLAMP(state->scroll_target - ui->window->mouse_wheel.y * 60.f, 0.f, max_scroll);
	}
	UI_Response scroll_response = {};
	if (max_scroll > 0.f)
	{
		rect_f32 scrollbar_viewport = rect_f32_inset(frame->rect, 32);
		rect_f32 track = rect_f32_slice(&scrollbar_viewport, AXIS_X, -24.f);
		scroll_response = ui_scrollbar(ui, scroll_id, track, scrollbar_viewport.h, &state->scroll_target, instruction_count * row_height);
	}
	Seconds now = seconds_now();
	if (wheel_scroll || scroll_response.pressed || scroll_response.held) {
		state->tracking_resume_time.seconds = now.seconds + 8.0;
	}
	if (now.seconds >= state->tracking_resume_time.seconds)
	{
		u32 tracked_index = 0;
		if (program_index_from_cpu_address(debugger, cpu.PC, &tracked_index))
		{
			state->tracking_failed = false;
			state->scroll_target = CLAMP(frame->header_height + (tracked_index + 0.5f) * row_height - main_rect.h * 0.5f, 0.f, max_scroll);
		}
		else if (!state->tracking_failed || state->tracking_failed_cpu_address != cpu.PC)
		{
			LOG_WARN("program view could not find CPU address $%04X", cpu.PC);
			state->tracking_failed_cpu_address = cpu.PC;
			state->tracking_failed = true;
		}
	}
	state->scroll = CLAMP(state->scroll, 0.f, max_scroll);
	state->scroll += (state->scroll_target - state->scroll) * 0.24f;
	if (fabsf(state->scroll_target - state->scroll) < 0.1f) {
		state->scroll = state->scroll_target;
	}

	f32 rows_scroll = Max(state->scroll - frame->header_height, 0.f);
	f32 first_visible_f = floorf(rows_scroll / row_height);
	rect_f32 scroll_rect = main_rect;
	scroll_rect.y += frame->header_height - state->scroll + first_visible_f * row_height;
	scroll_rect.h = main_rect.y + main_rect.h - scroll_rect.y;

	i64 first_visible = (i32)Max(0, first_visible_f);
	first_visible = Min(first_visible, (i64)instruction_count);
	f32 fitting = ceilf(scroll_rect.h / row_height);
	i64 row_count = Min((i64)instruction_count - first_visible, (i64)fitting);

	Color_SRGBA row_colors[] =
	{
		[PROGRAM_ROW_NONE]        = ui->theme.palette.error,
		[PROGRAM_ROW_INSTRUCTION] = ui->theme.text_vibrant,
		[PROGRAM_ROW_GUESS]       = ui->theme.text_subtle,
		[PROGRAM_ROW_ERROR]       = ui->theme.palette.error,
	};

	ProgramSlice rows = program_slice(debugger, scratch, (u32)first_visible, (u32)row_count);
	if (rows.count < (u32)row_count)
	{
		u32 actual_count = (u32)first_visible + rows.count;
		Assert(actual_count <= instruction_count);
		program_view_clamp_scroll(state, main_rect, frame->header_height, row_height, actual_count);
	}
	if (!rows.count) {
		return;
	}
	PROF_BLOCK("program view rows")
	for (u32 row_index = 0; row_index < rows.count; ++row_index)
	{
		ProgramInstruction instruction = rows.items[row_index];
		NES_InstructionDesc desc = nes_instruction_desc(instruction.type);
		u16 address = instruction.cpu_address;

		String formatted_address = push_formatted(scratch, "%04X", address);
		String formatted_instruction;
		formatted_instruction.text = begin_append_sequence(scratch);
		push_append_formatted(scratch, "%s ", desc.name);
		program_format_operand(scratch, address, instruction.type, instruction.data);
		formatted_instruction.size = end_append_sequence(scratch, formatted_instruction.text);

		u32 indent_size_px = 24;
		rect_f32 row_rect = rect_f32_slice(&scroll_rect, AXIS_Y, row_height);
		Color_SRGBA color = row_colors[instruction.status];
		Color_SRGBA address_color = address == cpu.PC ? ui->theme.program_counter : ui->theme.text_subtle;

		rect_f32 content_rect = row_rect;
		rect_f32 address_rect = rect_f32_slice(&content_rect, AXIS_X, 12 * 7);
		b32 has_breakpoint = debugger_has_program_breakpoint(debugger, instruction.map_addr);
		if (rect_f32_contains(address_rect, ui->mouse) && ui->window->keys[OS_Key_MouseLeft] & OS_KEY_PRESSED) {
			debugger_set_program_breakpoint(debugger, instruction.map_addr, !has_breakpoint);
			has_breakpoint = !has_breakpoint;
		}

		ui_push_emission(ui, address == cpu.PC ? ui->theme.palette.emission_high : has_breakpoint ? ui->theme.palette.emission_medium : 0.f);
		if (has_breakpoint)
		{
			ui_draw_rect(ui, (rect_f32) { row_rect.x - 12.f, row_rect.y + row_height * 0.35f, 4.f, 4.f }, ui->theme.program_counter);
		}
		UI_TextStyle address_style = font;
		address_style.color = address_color;
		ui_draw_text(ui, address_rect, address_style, formatted_address);

		rect_f32 instruction_rect = rect_f32_translate_axis(content_rect,
			AXIS_X, instruction.indent * indent_size_px);
		UI_TextStyle instruction_style = font;
		instruction_style.color = color;
		u32 program_offset = program_mapped_instruction_offset(program, instruction.map_addr);
		const ProgramByte *program_byte = program_offset != MAX_VALUE_U32 ? &program->bytes[program_offset] : 0;
		f32 opacity = 0.5f;
		if (program_byte && (program_byte->flags & PROGRAM_INSTRUCTION_EXECUTED)) {
			opacity = 1.f;
		} else if (program_byte && (program_byte->flags & PROGRAM_INSTRUCTION_STATIC)) {
			opacity = 0.7f;
		}
		instruction_style.color.a *= opacity;
		ui_draw_text(ui, instruction_rect, instruction_style,
			formatted_instruction);
		ui_pop_emission(ui);
		vec2 instruction_size = ui_measure_text(ui, instruction_style, formatted_instruction);
		rect_f32 instruction_hit_rect = { instruction_rect.x, instruction_rect.y, instruction_size.x, row_height };
		program_draw_instruction_tooltip(frame, instruction_hit_rect, instruction, desc, formatted_instruction);

		rect_f32 bridge_rect = rect_f32_from_slice(content_rect, AXIS_X, 2);
		for (i32 i = 0; instruction.bridges && i < 16;
			++i, instruction.bridges >>= 1)
		{
			if (instruction.bridges & 1)
			{
				ui_draw_rect(ui, rect_f32_translate_axis(bridge_rect,
					AXIS_X, i * indent_size_px),
					ui->theme.program_bridge);
			}
		}
	}
}

static void program_box_paint(UI_Box *box)
{
	ProgramBoxData *data = box->content;
	Assert(data);
	Assert(data->frame.ui == box->ui);
	data->frame.rect = box->viewport;
	data->frame.content_box = box;
	ui_push_clip(box->ui, data->clip);
	program_view_content(&data->frame);
	ui_pop_clip(box->ui);
}

static const UI_BoxHooks program_box_hooks = {
	.paint = program_box_paint,
};

void program_view_build_ui(ViewFrameData *frame)
{
	Debugger *debugger = frame->debugger;
	String title = LIT("PROGRAM");
	if (debugger_armed(debugger) && frame->publication->valid)
	{
		u16 cpu_address = frame->publication->cpu.PC;
		NES_MapAddr mapped = debugger_cpu_map(debugger, cpu_address);
		u32 instruction_index = 0;
		const char *mode = seconds_now().seconds >= frame->view->program.tracking_resume_time.seconds ? "tracking" : "manual";
		if (mapped.device == NES_DEVICE_PRG_ROM && program_index_from_cpu_address(debugger, cpu_address, &instruction_index)) {
			title = push_formatted(frame->scratch, "PROGRAM [IDX=%u, CPU=$%04X, PRG=$%05X] (%s)", instruction_index, cpu_address, mapped.address, mode);
		} else if (mapped.device == NES_DEVICE_PRG_ROM) {
			title = push_formatted(frame->scratch, "PROGRAM [IDX=?, CPU=$%04X, PRG=$%05X] (%s)", cpu_address, mapped.address, mode);
		} else if (mapped.device == NES_DEVICE_PRG_RAM && program_index_from_cpu_address(debugger, cpu_address, &instruction_index)) {
			title = push_formatted(frame->scratch, "PROGRAM [IDX=%u, CPU=$%04X, RAM=$%04X] (%s)", instruction_index, cpu_address, mapped.address, mode);
		} else {
			title = push_formatted(frame->scratch, "PROGRAM [IDX=?, CPU=$%04X, PRG=?] (%s)", cpu_address, mode);
		}
	}
	ViewFrameData content = view_begin_frame(frame, title);
	ProgramBoxData *data = arena_push_zero(&frame->ui->frame_arena, sizeof(*data));
	data->frame = content;
	data->clip = frame->rect;
	content.content_box->ops = &program_box_hooks;
	content.content_box->content = data;
	view_end_frame(&content);
}
