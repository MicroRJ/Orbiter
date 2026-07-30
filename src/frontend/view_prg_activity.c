#include "debugger.h"
#include "views.h"

enum
{
	PRG_ACTIVITY_DEFAULT_CELL_SIZE = 256,
	PRG_ACTIVITY_MIN_CELL_SIZE = 4,
	PRG_ACTIVITY_MAX_CELL_SIZE = KiB(8),
};

typedef struct
{
	rect_f32 rect;
	u32 cell_size;
	u32 rom_cell_count;
	u32 cells_per_page;
	u32 rom_page_count;
	u32 page_count;
	vec2i page_grid;
	vec2i page_cells;
	f32 cell_extent;
	f32 cell_gap;
	f32 page_gap;
}
PRGActivityGrid;

typedef struct
{
	u32 page;
	u32 local;
}
PRGActivityCellLocation;

static PRGActivityCellLocation prg_activity_cell_location(const PRGActivityGrid *grid, u32 cell_index)
{
	b32 ram = cell_index >= grid->rom_cell_count;
	u32 device_cell = ram ? cell_index - grid->rom_cell_count : cell_index;
	return (PRGActivityCellLocation) {
		.page = (ram ? grid->rom_page_count : 0) + device_cell / grid->cells_per_page,
		.local = device_cell % grid->cells_per_page,
	};
}

static rect_f32 prg_activity_cell_rect_from_location(const PRGActivityGrid *grid, PRGActivityCellLocation location)
{
	u32 page_column = location.page % grid->page_grid.x;
	u32 page_row = location.page / grid->page_grid.x;
	u32 cell_column = location.local % grid->page_cells.x;
	u32 cell_row = location.local / grid->page_cells.x;
	f32 page_width = grid->page_cells.x * grid->cell_extent + (grid->page_cells.x - 1) * grid->cell_gap;
	f32 page_height = grid->page_cells.y * grid->cell_extent + (grid->page_cells.y - 1) * grid->cell_gap;
	return (rect_f32) {
		.x = grid->rect.x + page_column * (page_width + grid->page_gap) + cell_column * (grid->cell_extent + grid->cell_gap),
		.y = grid->rect.y + page_row * (page_height + grid->page_gap) + cell_row * (grid->cell_extent + grid->cell_gap),
		.w = grid->cell_extent,
		.h = grid->cell_extent,
	};
}

static rect_f32 prg_activity_cell_rect(const PRGActivityGrid *grid, u32 cell_index)
{
	return prg_activity_cell_rect_from_location(grid, prg_activity_cell_location(grid, cell_index));
}

static vec2 prg_activity_cell_center(const PRGActivityGrid *grid, u32 cell_index)
{
	rect_f32 cell = prg_activity_cell_rect(grid, cell_index);
	return v2(cell.x + cell.w * 0.5f, cell.y + cell.h * 0.5f);
}

static void prg_activity_draw_segment(UI_Context *ui, vec2 from, vec2 to, f32 thickness, Color_SRGBA color)
{
	rect_f32 segment = {};
	if (from.x == to.x)
	{
		segment.x = from.x - thickness * 0.5f;
		segment.y = Min(from.y, to.y);
		segment.w = thickness;
		segment.h = fabsf(to.y - from.y);
	} else {
		segment.x = Min(from.x, to.x);
		segment.y = from.y - thickness * 0.5f;
		segment.w = fabsf(to.x - from.x);
		segment.h = thickness;
	}
	ui_draw_rect(ui, segment, color);
}

static Color_SRGBA prg_activity_edge_color(const UI_Theme *theme, u32 source_offset, u32 destination_offset)
{
	u64 hash = ((u64)source_offset << 32) | destination_offset;
	hash ^= hash >> 30;
	hash *= 0xBF58476D1CE4E5B9ull;
	hash ^= hash >> 27;
	hash *= 0x94D049BB133111EBull;
	hash ^= hash >> 31;
	f32 palette_position = (f32)(hash & 0xFFFFFF) / 16777216.f * 3.f;
	u32 first = (u32)palette_position;
	f32 blend = palette_position - first;
	Color_SRGBA palette[] = {
		theme->palette.teal,
		theme->palette.blue,
		theme->palette.violet,
		theme->palette.teal,
	};
	Color_SRGBA color = color_srgba_mix(palette[first], palette[first + 1], blend);
	return color_srgba_mix(color, theme->palette.text, 0.16f);
}

static b32 prg_activity_storage_offset(const Program *program, NES_MapAddr mapped, b32 include_prg_ram, u32 *offset)
{
	if (mapped.device == NES_DEVICE_PRG_ROM && mapped.offset < program->prg_rom_byte_count)
	{
		*offset = mapped.offset;
		return true;
	}
	if (include_prg_ram && mapped.device == NES_DEVICE_PRG_RAM && mapped.offset < program->prg_ram_byte_count)
	{
		*offset = program->prg_rom_byte_count + mapped.offset;
		return true;
	}
	return false;
}

static b32 prg_activity_has_mapped_ram(const Debugger *debugger)
{
	for (u32 chunk = 0; chunk < CPU_MAPPING_CHUNK_COUNT; ++chunk) {
		if (debugger_cpu_mapping_chunk(debugger, chunk).device == NES_DEVICE_PRG_RAM) {
			return true;
		}
	}
	return false;
}

static void prg_activity_draw_edge(UI_Context *ui, const PRGActivityGrid *grid, u32 source_cell, u32 destination_cell, f32 thickness, Color_SRGBA color)
{
	vec2 source = prg_activity_cell_center(grid, source_cell);
	vec2 destination = prg_activity_cell_center(grid, destination_cell);
	vec2 bend = v2(destination.x, source.y);
	prg_activity_draw_segment(ui, source, bend, thickness, color);
	prg_activity_draw_segment(ui, bend, destination, thickness, color);
}

static vec2i rectangle_dimensions_from_area(u32 area)
{
	u32 i, y;
	for (y = i = 1; i <= area / i; ++ i) if ((area % i) == 0) y = i;
	vec2i dimen = { y, area / y };
	return dimen;
}

static vec2i grid_dimensions_for_rectangles(u32 rectangle_count, vec2i rectangle)
{
	Assert(rectangle_count && rectangle.x > 0 && rectangle.y > 0);
	u32 square_extent = (u32)ceilf(sqrtf((f32)rectangle_count * rectangle.x * rectangle.y));
	u32 columns = Min(rectangle_count, (square_extent + rectangle.x - 1) / rectangle.x);
	return (vec2i) { columns, (rectangle_count + columns - 1) / columns };
}

static PRGActivityGrid prg_activity_grid(rect_f32 available, u32 rom_size, u32 ram_size, u32 cell_size)
{
	Assert(cell_size && cell_size <= CPU_MAPPING_CHUNK_SIZE && (CPU_MAPPING_CHUNK_SIZE % cell_size) == 0);
	PRGActivityGrid grid = {};
	grid.cell_size = cell_size;
	grid.rom_cell_count = (rom_size + cell_size - 1) / cell_size;
	grid.cells_per_page = CPU_MAPPING_CHUNK_SIZE / cell_size;
	grid.rom_page_count = (rom_size + CPU_MAPPING_CHUNK_SIZE - 1) / CPU_MAPPING_CHUNK_SIZE;
	u32 ram_page_count = (ram_size + CPU_MAPPING_CHUNK_SIZE - 1) / CPU_MAPPING_CHUNK_SIZE;
	grid.page_count = grid.rom_page_count + ram_page_count;
	grid.page_cells = rectangle_dimensions_from_area(grid.cells_per_page);
	grid.page_grid = grid_dimensions_for_rectangles(grid.page_count, grid.page_cells);
	grid.cell_gap = 1.f;
	grid.page_gap = 5.f;
	u32 total_cell_columns = grid.page_grid.x * grid.page_cells.x;
	u32 total_cell_rows = grid.page_grid.y * grid.page_cells.y;
	f32 horizontal_gaps = grid.page_grid.x * (grid.page_cells.x - 1) * grid.cell_gap + (grid.page_grid.x - 1) * grid.page_gap;
	f32 vertical_gaps = grid.page_grid.y * (grid.page_cells.y - 1) * grid.cell_gap + (grid.page_grid.y - 1) * grid.page_gap;
	grid.cell_extent = Max(0.f, Min((available.w - horizontal_gaps) / total_cell_columns, (available.h - vertical_gaps) / total_cell_rows));
	f32 width = total_cell_columns * grid.cell_extent + horizontal_gaps;
	f32 height = total_cell_rows * grid.cell_extent + vertical_gaps;
	grid.rect = rect_f32_align(available, v2(width, height), v2(0.5f, 0.5f));
	return grid;
}

static void prg_activity_draw_tooltip(ViewFrameData *frame, const PRGActivityGrid *grid, u32 cell_index, rect_f32 selected_cell, const Program *program, u32 program_size)
{
	UI_Context *ui = frame->ui;
	if (cell_index == MAX_VALUE_U32) {
		return;
	}
	u32 cell_begin = cell_index * grid->cell_size;
	u32 cell_end = Min(cell_begin + grid->cell_size, program_size);
	String lines[2] = {};
	u32 line_count = 0;
	lines[line_count++] = push_formatted(frame->scratch, "CELL %u   %u BYTES", cell_index, grid->cell_size);
	const char *device = cell_begin < program->prg_rom_byte_count ? "PRG ROM" : "PRG RAM";
	u32 device_base = cell_begin < program->prg_rom_byte_count ? 0 : program->prg_rom_byte_count;
	lines[line_count++] = push_formatted(frame->scratch, "DESTINATION  %s $%06X-$%06X", device, cell_begin - device_base, cell_end - 1 - device_base);
	ui_draw_rect_outline(ui, selected_cell, 2.f, ui->theme.text_neutral);
	UI_TextStyle style = ui->theme.code;
	style.color = ui->theme.text_neutral;
	f32 line_height = style.size + 4.f;
	f32 width = 0.f;
	for (u32 index = 0; index < line_count; ++index) {
		width = Max(width, ui_measure_text(ui, style, lines[index]).x);
	}
	f32 padding = 8.f;
	rect_f32 tooltip = {
		.x = ui->mouse.x + 14.f,
		.y = ui->mouse.y + 18.f,
		.w = width + padding * 2.f,
		.h = line_count * line_height + padding * 2.f,
	};
	tooltip.x = CLAMP(tooltip.x, frame->rect.x, Max(frame->rect.x, frame->rect.x + frame->rect.w - tooltip.w));
	tooltip.y = CLAMP(tooltip.y, frame->rect.y, Max(frame->rect.y, frame->rect.y + frame->rect.h - tooltip.h));
	ui_push_layer(ui, UI_LAYER_OVERLAY);
	ui_push_unclipped(ui);
	ui_draw_backdrop(ui, tooltip);
	rect_f32 text = rect_f32_inset(tooltip, padding);
	for (u32 index = 0; index < line_count; ++index)
	{
		ui_draw_text(ui, text, style, lines[index]);
		text.y += line_height;
	}
	ui_pop_unclipped(ui);
	ui_pop_layer(ui);
}

static void prg_activity_mapped_pages(b32 *mapped_pages, const PRGActivityGrid *grid, const Debugger *debugger, const Program *program, b32 include_prg_ram)
{
	for (u32 chunk = 0; chunk < CPU_MAPPING_CHUNK_COUNT; ++chunk)
	{
		NES_MapAddr mapped = debugger_cpu_mapping_chunk(debugger, chunk);
		u32 page = MAX_VALUE_U32;
		if (mapped.device == NES_DEVICE_PRG_ROM && mapped.offset < program->prg_rom_byte_count) {
			page = mapped.offset / CPU_MAPPING_CHUNK_SIZE;
		}
		else if (include_prg_ram && mapped.device == NES_DEVICE_PRG_RAM && mapped.offset < program->prg_ram_byte_count) {
			page = grid->rom_page_count + mapped.offset / CPU_MAPPING_CHUNK_SIZE;
		}
		if (page < grid->page_count) mapped_pages[page] = true;
	}
}

static void prg_activity_draw_crawler(ViewFrameData *frame, const PRGActivityGrid *grid, u32 cell_count, u32 crawler_cell)
{
	if (crawler_cell >= cell_count) {
		return;
	}
	rect_f32 cell = prg_activity_cell_rect(grid, crawler_cell);
	f32 pulse = 0.5f + 0.5f * sinf((f32)frame->publication->generation * 0.22f);
	Color_SRGBA color = frame->ui->theme.program_bridge;
	color.a = 0.55f + pulse * 0.45f;
	ui_draw_rect_outline(frame->ui, cell, Max(1.f, Min(3.f, grid->cell_extent * 0.16f)), color);
	f32 dot_size = Max(2.f, Min(6.f, grid->cell_extent * 0.35f));
	ui_draw_rect(frame->ui, (rect_f32) {
		.x = cell.x + (cell.w - dot_size) * 0.5f,
		.y = cell.y + (cell.h - dot_size) * 0.5f,
		.w = dot_size,
		.h = dot_size,
	}, color);
}

static void prg_activity_view_content(ViewFrameData *frame)
{
	Debugger *debugger = frame->debugger;
	UI_Context *ui = frame->ui;
	PRGActivityViewState *state = &frame->view->prg_activity;
	Assert(frame->execution_graph);
	Assert(frame->execution_activity);
	if (!state->cell_size) {
		state->cell_size = PRG_ACTIVITY_DEFAULT_CELL_SIZE;
	}
	UI_TextStyle text_style = ui->theme.code;
	text_style.color = ui->theme.text_subtle;
	rect_f32 layout = rect_f32_inset(frame->rect, 12.f);
	const Program *program = debugger_program(debugger);
	b32 include_prg_ram = prg_activity_has_mapped_ram(debugger);
	u32 program_size = program->prg_rom_byte_count + (include_prg_ram ? program->prg_ram_byte_count : 0);
	if (!program->prg_rom_byte_count)
	{
		ui_draw_text(ui, layout, text_style, LIT("No cartridge loaded - Ctrl+O to open an iNES ROM"));
		return;
	}

	u16 pc = frame->publication->state.cpu.PC;
	b32 hovered = rect_f32_contains(frame->rect, ui->mouse);
	b32 control = !!(ui->window->keys[OS_Key_LeftControl] & OS_KEY_DOWN) || !!(ui->window->keys[OS_Key_RightControl] & OS_KEY_DOWN);
	i32 wheel = ui->window->mouse_wheel.y;
	if (hovered && control && wheel)
	{
		if (wheel > 0) {
			state->cell_size = Min(state->cell_size * 2, PRG_ACTIVITY_MAX_CELL_SIZE);
		} else {
			state->cell_size = Max(state->cell_size / 2, PRG_ACTIVITY_MIN_CELL_SIZE);
		}
	}
	u32 cell_size = state->cell_size;
	NES_MapAddr active_mapping = debugger_cpu_map(debugger, pc);
	u32 active_offset = 0;
	b32 has_active_cell = prg_activity_storage_offset(program, active_mapping, include_prg_ram, &active_offset);
	u32 active_cell = has_active_cell ? active_offset / cell_size : MAX_VALUE_U32;
	u32 active_begin = active_cell * cell_size;
	u32 active_end = Min(active_begin + cell_size, program_size);
	u32 crawler_cpu = program->refinement_cpu_cursor;
	NES_MapAddr crawler_mapping = {};
	b32 crawler_in_cpu_space = crawler_cpu < NES_CPU_ADDRESS_SPACE;
	if (crawler_in_cpu_space) {
		crawler_mapping = debugger_cpu_map(debugger, (u16)crawler_cpu);
	}
	u32 crawler_offset = 0;
	b32 crawler_in_prg = crawler_in_cpu_space && prg_activity_storage_offset(program, crawler_mapping, include_prg_ram, &crawler_offset);
	u32 crawler_cell = crawler_in_prg ? crawler_offset / cell_size : MAX_VALUE_U32;
	f32 label_height = ui->theme.code.size + 10.f;
	rect_f32 label = rect_f32_slice(&layout, AXIS_Y, label_height);
	const char *active_device = has_active_cell && active_mapping.device == NES_DEVICE_PRG_RAM ? "PRG RAM" : "PRG ROM";
	String label_text = has_active_cell
		? push_formatted(frame->scratch, "%u B / cell   PC $%04X -> %s $%X   active $%X-$%X   Ctrl+wheel zooms", cell_size, pc, active_device, active_mapping.offset, active_begin, active_end - 1)
		: push_formatted(frame->scratch, "%u B / cell   PC $%04X is not mapped to program storage   Ctrl+wheel zooms", cell_size, pc);
	ui_draw_text(ui, label, text_style, label_text);
	label = rect_f32_slice(&layout, AXIS_Y, label_height);
	const char *crawler_pass = program->refinement_pass_count & 1 ? "BRIDGES" : "DISCOVERY";
	if (crawler_in_prg)
	{
		const char *crawler_device = crawler_mapping.device == NES_DEVICE_PRG_RAM ? "PRG RAM" : "PRG ROM";
		label_text = push_formatted(frame->scratch, "CRAWLER %s   LAP %llu   CPU $%04X -> %s $%X", crawler_pass, program->refinement_pass_count, crawler_cpu, crawler_device, crawler_mapping.offset);
	}
	else
	{
		label_text = push_formatted(frame->scratch, "CRAWLER %s   LAP %llu   CPU $%05X   NOT IN PROGRAM STORAGE", crawler_pass, program->refinement_pass_count, crawler_cpu);
	}
	UI_TextStyle crawler_style = text_style;
	crawler_style.color = ui->theme.program_bridge;
	ui_draw_text(ui, label, crawler_style, label_text);

	u32 cell_count = (program_size + cell_size - 1) / cell_size;
	u32 ram_size = include_prg_ram ? program->prg_ram_byte_count : 0;
	PRGActivityGrid grid = prg_activity_grid(layout, program->prg_rom_byte_count, ram_size, cell_size);
	b32 *mapped_pages = arena_push_zero(frame->scratch, sizeof(*mapped_pages) * grid.page_count);
	prg_activity_mapped_pages(mapped_pages, &grid, debugger, program, include_prg_ram);
	Color_SRGBA mapped_color = color_srgba_mix(ui->theme.slider_track, ui->theme.text_subtle, 0.05f);
	u32 hovered_cell = MAX_VALUE_U32;
	rect_f32 hovered_cell_rect = {};
	b32 grid_hovered = rect_f32_contains(grid.rect, ui->mouse);
	for (u32 cell_index = 0; cell_index < cell_count; ++cell_index)
	{
		PRGActivityCellLocation location = prg_activity_cell_location(&grid, cell_index);
		Assert(location.page < grid.page_count);
		rect_f32 cell = prg_activity_cell_rect_from_location(&grid, location);
		u32 cell_begin = cell_index * cell_size;
		Color_SRGBA color = mapped_pages[location.page] ? mapped_color : ui->theme.slider_track;
		if (include_prg_ram && cell_begin >= program->prg_rom_byte_count) {
			color = color_srgba_mix(color, ui->theme.program_bridge, 0.06f);
		}
		ui_draw_rect(ui, cell, color);
		if (cell_index == active_cell && grid.cell_extent >= 4.f) {
			ui_draw_rect_outline(ui, cell, 1.f, ui->theme.text_vibrant);
		}
		if (grid_hovered && hovered_cell == MAX_VALUE_U32 && rect_f32_contains(rect_f32_inset(cell, grid.cell_gap * -0.5f), ui->mouse))
		{
			hovered_cell = cell_index;
			hovered_cell_rect = cell;
		}
	}
	if (grid.cell_extent >= 1.f)
	{
		ExecutionActivitySample *edges = arena_push(frame->scratch, sizeof(*edges) * EXECUTION_ACTIVITY_SAMPLE_CAPACITY);
		u32 sampled_edge_count = execution_activity_sample(frame->execution_activity, frame->execution_graph, cell_size, edges, EXECUTION_ACTIVITY_SAMPLE_CAPACITY);
		for (u32 index = 0; index < sampled_edge_count; ++index)
		{
			ExecutionActivitySample *edge = &edges[index];
			u32 source_cell = edge->source_offset / cell_size;
			u32 destination_cell = edge->destination_offset / cell_size;
			f32 intensity = edge->intensity;
			if (source_cell >= cell_count || destination_cell >= cell_count || source_cell == destination_cell || intensity <= 0.f) {
				continue;
			}
			Color_SRGBA residual_color = prg_activity_edge_color(&ui->theme, edge->source_offset, edge->destination_offset);
			residual_color.a = intensity;
			f32 thickness = Max(1.f, Min(3.f, grid.cell_extent * (0.12f + intensity * 0.18f)));
			f32 emission = ui->theme.palette.emission_high * intensity * 5.f;
			ui_push_emission(ui, emission);
			prg_activity_draw_edge(ui, &grid, source_cell, destination_cell, thickness, residual_color);
			ui_pop_emission(ui);
		}
	}
	prg_activity_draw_crawler(frame, &grid, cell_count, crawler_cell);
	prg_activity_draw_tooltip(frame, &grid, hovered_cell, hovered_cell_rect, program, program_size);
}

void prg_activity_view_frame(ViewFrameData *frame)
{
	ViewFrameData content = view_begin_frame(frame, LIT("PRG ACTIVITY - EXECUTION FLOW"));
	PROF_BLOCK("prg activity content") prg_activity_view_content(&content);
	view_end_frame(&content);
}
