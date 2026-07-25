#include "debugger.h"
#include "views.h"

enum
{
	PRG_ACTIVITY_DEFAULT_CELL_SIZE = 256,
	PRG_ACTIVITY_MIN_CELL_SIZE = 4,
	PRG_ACTIVITY_MAX_CELL_SIZE = KiB(8),
	PRG_ACTIVITY_VISIBLE_EDGE_COUNT = 64,
};

typedef struct
{
	rect_f32 rect;
	u32 cell_size;
	u32 rom_cell_count;
	u32 ram_cell_count;
	u32 cells_per_bank;
	u32 rom_bank_count;
	u32 block_count;
	u32 block_columns;
	u32 block_rows;
	u32 cell_columns;
	u32 cell_rows;
	f32 cell_extent;
	f32 cell_gap;
	f32 bank_gap;
}
PRGActivityGrid;

static rect_f32 prg_activity_cell_rect(const PRGActivityGrid *grid, u32 cell_index)
{
	b32 ram = cell_index >= grid->rom_cell_count;
	u32 device_cell = ram ? cell_index - grid->rom_cell_count : cell_index;
	u32 block = (ram ? grid->rom_bank_count : 0) + device_cell / grid->cells_per_bank;
	u32 local = device_cell % grid->cells_per_bank;
	u32 block_column = block % grid->block_columns;
	u32 block_row = block / grid->block_columns;
	u32 cell_column = local % grid->cell_columns;
	u32 cell_row = local / grid->cell_columns;
	f32 block_width = grid->cell_columns * grid->cell_extent + (grid->cell_columns - 1) * grid->cell_gap;
	f32 block_height = grid->cell_rows * grid->cell_extent + (grid->cell_rows - 1) * grid->cell_gap;
	return (rect_f32) {
		.x = grid->rect.x + block_column * (block_width + grid->bank_gap) + cell_column * (grid->cell_extent + grid->cell_gap),
		.y = grid->rect.y + block_row * (block_height + grid->bank_gap) + cell_row * (grid->cell_extent + grid->cell_gap),
		.w = grid->cell_extent,
		.h = grid->cell_extent,
	};
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

static vec2 prg_activity_edge_position(vec2 from, vec2 bend, vec2 to, f32 progress)
{
	f32 first_length = fabsf(bend.x - from.x) + fabsf(bend.y - from.y);
	f32 second_length = fabsf(to.x - bend.x) + fabsf(to.y - bend.y);
	f32 distance = progress * (first_length + second_length);
	if (distance <= first_length && first_length > 0.f) {
		return v2(from.x + (bend.x - from.x) * distance / first_length, from.y + (bend.y - from.y) * distance / first_length);
	}
	f32 second_progress = second_length > 0.f ? (distance - first_length) / second_length : 1.f;
	return v2(bend.x + (to.x - bend.x) * second_progress, bend.y + (to.y - bend.y) * second_progress);
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

static void prg_activity_update_residual(PRGActivityViewState *state, NES_ExecutionHistory history, const Program *program, b32 include_prg_ram)
{
	ActivityTracker *tracker = &state->tracker;
	if (history.total_count < tracker->consumed_history_count) {
		activity_tracker_reset(tracker, 0);
	}
	u64 oldest_sequence = history.total_count - history.count;
	u64 first_destination = Max(tracker->consumed_history_count, oldest_sequence + 1);
	for (u64 sequence = first_destination; sequence < history.total_count; ++sequence)
	{
		const NES_ExecutionMapping *source = &history.entries[(sequence - 1) % history.capacity];
		const NES_ExecutionMapping *destination = &history.entries[sequence % history.capacity];
		u32 source_offset = 0;
		u32 destination_offset = 0;
		if (prg_activity_storage_offset(program, source->destination, include_prg_ram, &source_offset) &&
			prg_activity_storage_offset(program, destination->destination, include_prg_ram, &destination_offset))
		{
			if (source_offset != destination_offset) {
				activity_tracker_record(tracker, source_offset, destination_offset, sequence);
			}
		}
	}
	tracker->consumed_history_count = history.total_count;
	activity_tracker_update(tracker, seconds_now().seconds);
}

static void prg_activity_draw_edge(UI_Context *ui, const PRGActivityGrid *grid, u32 source_cell, u32 destination_cell, f32 thickness, Color_SRGBA color)
{
	vec2 source = prg_activity_cell_center(grid, source_cell);
	vec2 destination = prg_activity_cell_center(grid, destination_cell);
	vec2 bend = v2(destination.x, source.y);
	prg_activity_draw_segment(ui, source, bend, thickness, color);
	prg_activity_draw_segment(ui, bend, destination, thickness, color);
}

static PRGActivityGrid prg_activity_grid(rect_f32 available, u32 rom_size, u32 ram_size, u32 bank_size, u32 cell_size)
{
	PRGActivityGrid best = {};
	best.cell_size = cell_size;
	best.rom_cell_count = (rom_size + cell_size - 1) / cell_size;
	best.ram_cell_count = (ram_size + cell_size - 1) / cell_size;
	best.cells_per_bank = Max(1u, (bank_size + cell_size - 1) / cell_size);
	best.rom_bank_count = (best.rom_cell_count + best.cells_per_bank - 1) / best.cells_per_bank;
	u32 ram_bank_count = (best.ram_cell_count + best.cells_per_bank - 1) / best.cells_per_bank;
	best.block_count = best.rom_bank_count + ram_bank_count;
	f32 available_aspect = available.h > 0.f ? available.w / available.h : 1.f;
	f32 best_error = 3.402823466e+38F;
	for (u32 cell_rows = 1; cell_rows <= best.cells_per_bank; ++cell_rows)
	{
		if (best.cells_per_bank % cell_rows) {
			continue;
		}
		u32 cell_columns = best.cells_per_bank / cell_rows;
		for (u32 block_columns = 1; block_columns <= best.block_count; ++block_columns)
		{
			u32 block_rows = (best.block_count + block_columns - 1) / block_columns;
			f32 aspect = (f32)(block_columns * cell_columns) / (block_rows * cell_rows);
			f32 error = fabsf(logf(aspect / Max(available_aspect, 0.01f)));
			if (error < best_error)
			{
				best.cell_columns = cell_columns;
				best.cell_rows = cell_rows;
				best.block_columns = block_columns;
				best.block_rows = block_rows;
				best_error = error;
			}
		}
	}
	best.cell_gap = 1.f;
	best.bank_gap = 5.f;
	u32 total_cell_columns = best.block_columns * best.cell_columns;
	u32 total_cell_rows = best.block_rows * best.cell_rows;
	f32 horizontal_gaps = best.block_columns * (best.cell_columns - 1) * best.cell_gap + (best.block_columns - 1) * best.bank_gap;
	f32 vertical_gaps = best.block_rows * (best.cell_rows - 1) * best.cell_gap + (best.block_rows - 1) * best.bank_gap;
	best.cell_extent = Max(0.f, Min((available.w - horizontal_gaps) / total_cell_columns, (available.h - vertical_gaps) / total_cell_rows));
	f32 width = total_cell_columns * best.cell_extent + horizontal_gaps;
	f32 height = total_cell_rows * best.cell_extent + vertical_gaps;
	best.rect = rect_f32_align(available, v2(width, height), v2(0.5f, 0.5f));
	return best;
}

static void prg_activity_draw_tooltip(ViewFrameData *frame, const PRGActivityGrid *grid, u32 cell_count, const Program *program, u32 program_size)
{
	UI_Context *ui = frame->ui;
	if (!rect_f32_contains(grid->rect, ui->mouse)) {
		return;
	}
	u32 cell_index = MAX_VALUE_U32;
	rect_f32 selected_cell = {};
	for (u32 index = 0; index < cell_count; ++index)
	{
		rect_f32 cell = prg_activity_cell_rect(grid, index);
		if (rect_f32_contains(rect_f32_inset(cell, grid->cell_gap * -0.5f), ui->mouse))
		{
			cell_index = index;
			selected_cell = cell;
			break;
		}
	}
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
	ui_tooltip_begin(ui, tooltip);
	rect_f32 text = rect_f32_inset(tooltip, padding);
	for (u32 index = 0; index < line_count; ++index)
	{
		ui_tooltip_draw_text(ui, text, style, lines[index]);
		text.y += line_height;
	}
}

static b32 prg_activity_cell_is_mapped(const Debugger *debugger, const Program *program, b32 include_prg_ram, u32 cell_begin, u32 cell_end)
{
	for (u32 chunk = 0; chunk < CPU_MAPPING_CHUNK_COUNT; ++chunk)
	{
		NES_MapAddr mapped = debugger_cpu_mapping_chunk(debugger, chunk);
		u32 mapped_begin = 0;
		if (!prg_activity_storage_offset(program, mapped, include_prg_ram, &mapped_begin)) {
			continue;
		}
		u32 mapped_end = mapped_begin + CPU_MAPPING_CHUNK_SIZE;
		if (cell_begin < mapped_end && cell_end > mapped_begin) {
			return true;
		}
	}
	return false;
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
		ui_draw_text(ui, layout, text_style, LIT("No cartridge loaded"));
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
	NES_ExecutionHistory history = frame->publication->execution_history;
	prg_activity_update_residual(state, history, program, include_prg_ram);
	const NES_ExecutionMapping *head = history.count ? &history.entries[(history.write_index + history.capacity - 1) % history.capacity] : 0;
	u32 active_offset = 0;
	b32 has_active_cell = head && prg_activity_storage_offset(program, head->destination, include_prg_ram, &active_offset);
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
	const char *active_device = has_active_cell && head->destination.device == NES_DEVICE_PRG_RAM ? "PRG RAM" : "PRG ROM";
	String label_text = has_active_cell
		? push_formatted(frame->scratch, "%u B / cell   PC $%04X -> %s $%X   active $%X-$%X   Ctrl+wheel zooms", cell_size, head->cpu_address, active_device, head->destination.offset, active_begin, active_end - 1)
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
	u32 rom_cell_count = (program->prg_rom_byte_count + cell_size - 1) / cell_size;
	u32 ram_size = include_prg_ram ? program->prg_ram_byte_count : 0;
	u32 bank_size = Max((u32)CPU_MAPPING_CHUNK_SIZE, cell_size);
	PRGActivityGrid grid = prg_activity_grid(layout, program->prg_rom_byte_count, ram_size, bank_size, cell_size);
	u32 *cell_ages = arena_push_fill(frame->scratch, cell_count * sizeof(*cell_ages), 0xFF);
	for (u32 age = 0; age < history.count; ++age)
	{
		u32 history_index = (history.write_index + history.capacity - 1 - age) % history.capacity;
		const NES_ExecutionMapping *mapping = &history.entries[history_index];
		u32 storage_offset = 0;
		if (!prg_activity_storage_offset(program, mapping->destination, include_prg_ram, &storage_offset)) {
			continue;
		}
		u32 cell_index = storage_offset / cell_size;
		if (cell_index < cell_count && cell_ages[cell_index] == MAX_VALUE_U32) {
			cell_ages[cell_index] = age;
		}
	}
	Color_SRGBA mapped_color = color_srgba_mix(ui->theme.slider_track, ui->theme.text_subtle, 0.05f);
	u32 edge_count = Min(history.count > 0 ? history.count - 1 : 0, (u32)PRG_ACTIVITY_VISIBLE_EDGE_COUNT);
	for (u32 cell_index = 0; cell_index < cell_count; ++cell_index)
	{
		rect_f32 cell = prg_activity_cell_rect(&grid, cell_index);
		u32 cell_begin = cell_index * cell_size;
		u32 cell_end = Min(cell_begin + cell_size, program_size);
		Color_SRGBA color = prg_activity_cell_is_mapped(debugger, program, include_prg_ram, cell_begin, cell_end) ? mapped_color : ui->theme.slider_track;
		if (include_prg_ram && cell_begin >= program->prg_rom_byte_count) {
			color = color_srgba_mix(color, ui->theme.program_bridge, 0.06f);
		}
		if (cell_ages[cell_index] != MAX_VALUE_U32)
		{
			f32 recency = history.count > 1 ? 1.f - (f32)cell_ages[cell_index] / (history.count - 1) : 1.f;
			color = color_srgba_mix(color, ui->theme.program_counter, 0.20f + recency * 0.80f);
		}
		ui_draw_rect(ui, cell, color);
		if (cell_index == active_cell && grid.cell_extent >= 4.f) {
			ui_draw_rect_outline(ui, cell, 1.f, ui->theme.text_vibrant);
		}
	}
	if (grid.cell_extent >= 1.f)
	{
		ActivityEdge *edges = arena_push(frame->scratch, sizeof(*edges) * ACTIVITY_TRACKER_SAMPLE_CAPACITY);
		u32 sampled_edge_count = activity_tracker_sample(&state->tracker, cell_size, edges, ACTIVITY_TRACKER_SAMPLE_CAPACITY);
		for (u32 index = 0; index < sampled_edge_count; ++index)
		{
			ActivityEdge *edge = &edges[index];
			u32 source_cell = edge->source_offset / cell_size;
			u32 destination_cell = edge->destination_offset / cell_size;
			f32 intensity = edge->pulse;
			if (source_cell >= cell_count || destination_cell >= cell_count || source_cell == destination_cell || intensity < 0.01f) {
				continue;
			}
			Color_SRGBA residual_color = prg_activity_edge_color(&ui->theme, edge->source_offset, edge->destination_offset);
			residual_color.a = 0.06f + intensity * 0.44f;
			f32 thickness = Max(1.f, Min(3.f, grid.cell_extent * (0.12f + intensity * 0.18f)));
			ui_push_emission(ui, ui->theme.palette.emission_high * intensity * 5.f);
			prg_activity_draw_edge(ui, &grid, source_cell, destination_cell, thickness, residual_color);
			ui_pop_emission(ui);
		}
	}
	if (grid.cell_extent >= 1.f)
	{
		for (u32 age = edge_count; age-- > 0;)
		{
			u32 destination_index = (history.write_index + history.capacity - 1 - age) % history.capacity;
			u32 source_index = (history.write_index + history.capacity - 2 - age) % history.capacity;
			const NES_ExecutionMapping *source_mapping = &history.entries[source_index];
			const NES_ExecutionMapping *destination_mapping = &history.entries[destination_index];
			u32 source_offset = 0;
			u32 destination_offset = 0;
			if (!prg_activity_storage_offset(program, source_mapping->destination, include_prg_ram, &source_offset) ||
				!prg_activity_storage_offset(program, destination_mapping->destination, include_prg_ram, &destination_offset)) {
				continue;
			}
			u32 source_cell = source_offset / cell_size;
			u32 destination_cell = destination_offset / cell_size;
			if (source_cell >= cell_count || destination_cell >= cell_count || source_cell == destination_cell) {
				continue;
			}
			vec2 source = prg_activity_cell_center(&grid, source_cell);
			vec2 destination = prg_activity_cell_center(&grid, destination_cell);
			vec2 bend = v2(destination.x, source.y);
			f32 recency = edge_count > 1 ? 1.f - (f32)age / (edge_count - 1) : 1.f;
			Color_SRGBA edge_color = prg_activity_edge_color(&ui->theme, source_offset, destination_offset);
			edge_color.a = 0.08f + recency * 0.42f;
			f32 thickness = Max(1.f, Min(2.f, grid.cell_extent * 0.2f));
			ui_push_emission(ui, ui->theme.palette.emission_high * recency);
			prg_activity_draw_segment(ui, source, bend, thickness, edge_color);
			prg_activity_draw_segment(ui, bend, destination, thickness, edge_color);
			f32 pulse_progress = fmodf((f32)frame->publication->generation * 0.12f + age * 0.173f, 1.f);
			vec2 pulse = prg_activity_edge_position(source, bend, destination, pulse_progress);
			f32 pulse_size = Max(2.f, Min(5.f, grid.cell_extent * 0.5f));
			Color_SRGBA pulse_color = color_srgba_mix(edge_color, ui->theme.palette.text, 0.35f);
			pulse_color.a = 0.35f + recency * 0.65f;
			ui_draw_rect(ui, (rect_f32) { pulse.x - pulse_size * 0.5f, pulse.y - pulse_size * 0.5f, pulse_size, pulse_size }, pulse_color);
			ui_pop_emission(ui);
		}
	}
	prg_activity_draw_crawler(frame, &grid, cell_count, crawler_cell);
	prg_activity_draw_tooltip(frame, &grid, cell_count, program, program_size);
}

void prg_activity_view_frame(ViewFrameData *frame)
{
	ViewFrameData content = view_begin_frame(frame, LIT("PRG ACTIVITY — EXECUTION HISTORY"));
	prg_activity_view_content(&content);
	view_end_frame(&content);
}
