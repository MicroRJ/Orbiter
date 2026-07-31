#include "debugger.h"
#include "views.h"

static String cpu_mapping_device_name(NES_DeviceId device)
{
	switch (device)
	{
		case NES_DEVICE_NONE: return LIT("NONE");
		case NES_DEVICE_CPU: return LIT("CPU");
		case NES_DEVICE_PPU: return LIT("PPU");
		case NES_DEVICE_OAM: return LIT("OAM");
		case NES_DEVICE_PRAM: return LIT("PRAM");
		case NES_DEVICE_VRAM: return LIT("VRAM");
		case NES_DEVICE_WRAM: return LIT("WRAM");
		case NES_DEVICE_CHR_ROM: return LIT("CHR ROM");
		case NES_DEVICE_CHR_RAM: return LIT("CHR RAM");
		case NES_DEVICE_PRG_ROM: return LIT("PRG ROM");
		case NES_DEVICE_PRG_RAM: return LIT("PRG RAM");
		case NES_DEVICE_COUNT:
		case NES_DEVICE_ENUM_FORCE_U32: break;
	}
	return LIT("INVALID");
}

static void cpu_mapping_set_header(UI_Table *table, u32 column, String text)
{
	UI_TextStyle style = table->ui->theme.code;
	style.color = table->ui->theme.text_neutral;
	ui_table_set_text(table, 0, column, style, text);
}

static void cpu_mapping_view_content(ViewFrameData *frame)
{
	Debugger *debugger = frame->debugger;
	UI_Context *ui = frame->ui;
	UI_TextStyle value_style = ui->theme.code;
	value_style.color = ui->theme.text_subtle;
	rect_f32 layout = rect_f32_inset(frame->rect, 12.f);
	f32 row_height = ui->theme.code.size + 8.f;

	UI_Table table = ui_table_begin(ui, frame->scratch, layout, CPU_MAPPING_CHUNK_COUNT + 1, 3, row_height);
	ui_table_set_column(&table, 0, ui_table_column_content());
	ui_table_set_column(&table, 1, ui_table_column_flex(1.f));
	ui_table_set_column(&table, 2, ui_table_column_content());
	cpu_mapping_set_header(&table, 0, LIT("CPU ADDRESS"));
	cpu_mapping_set_header(&table, 1, LIT("FINAL DEVICE"));
	cpu_mapping_set_header(&table, 2, LIT("ADDRESS"));

	for (u32 chunk = 0; chunk < CPU_MAPPING_CHUNK_COUNT; ++chunk)
	{
		u16 cpu_address = (u16)(chunk * CPU_MAPPING_CHUNK_SIZE);
		NES_MapAddr mapped = debugger_cpu_mapping_chunk(debugger, chunk);
		u32 row = chunk + 1;
		ui_table_set_text(&table, row, 0, value_style, push_formatted(frame->scratch, "$%04X", cpu_address));
		ui_table_set_text(&table, row, 1, value_style, cpu_mapping_device_name(mapped.device));
		ui_table_set_text(&table, row, 2, value_style, push_formatted(frame->scratch, "$%08X", mapped.address));
	}
	ui_table_draw(&table);
}

void cpu_mapping_view_build_ui(ViewFrameData *frame)
{
	ViewFrameData content = view_begin_frame(frame, LIT("CPU MAPPING — 8 KiB PROBES"));
	cpu_mapping_view_content(&content);
	view_end_frame(&content);
}
