#include "base.h"
#include "os.h"
#include "nes/emulator.h"
#include "nes/isa.h"
#include "nes/state_meta.h"
#include "emulator_internal.h"
#include <string.h>

static Str test_read_file(Arena *arena, const char *path)
{
	Str result = {};
	Platform_File file = platform_access_file(path, PLATFORM_FILE_OPEN_EXISTING, PLATFORM_FILE_READ | PLATFORM_FILE_SHARE_READ);
	if (!platform_file_is_valid(file)) return result;
	u64 size = 0;
	if (platform_get_file_size(file, &size) && size <= MAX_VALUE_U32)
	{
		u8 *data = arena_push(arena, size + 1);
		u64 bytes_read = 0;
		if (platform_read_file(file, data, size, &bytes_read) && bytes_read == size)
		{
			data[size] = 0;
			result = str_from_data((char *)data, (u32)size);
		}
	}
	platform_close_file(file);
	return result;
}

static b32 test_write_file(const char *path, const void *data, u32 size)
{
	Platform_File file = platform_access_file(path, PLATFORM_FILE_CREATE_ALWAYS, PLATFORM_FILE_WRITE);
	if (!platform_file_is_valid(file)) return false;
	u64 bytes_written = 0;
	b32 result = platform_write_file(file, data, size, &bytes_written) && bytes_written == size;
	platform_close_file(file);
	return result;
}

static const NES_StateField *find_state_field(
	const NES_StateRecord *record, const char *name)
{
	for (u32 index = 0; index < record->field_count; ++index)
	{
		if (strcmp(record->fields[index].name, name) == 0)
		{
			return &record->fields[index];
		}
	}
	return 0;
}

static void assert_serialized_fields_equal(const NES_StateRecord *record,
	const void *expected, const void *actual)
{
	for (u32 index = 0; index < record->field_count; ++index)
	{
		const NES_StateField *field = &record->fields[index];
		if (!(field->flags & NES_STATE_FIELD_SERIALIZED)) continue;
		Assert(memory_match(
			(const u8 *)expected + field->offset,
			(const u8 *)actual + field->offset,
			field->size));
	}
}

static void test_scheduler_trace_packing(void)
{
	NES_TraceEntry expected = {
		.scheduler_clock = 0x1234FFF0,
		.cpu_address = MAX_VALUE_U16,
		.cpu_mapped = nes_map_addr(NES_DEVICE_PRG_RAM, NES_SCHEDULER_TRACE_MAPPED_OFFSET_MASK),
		.cpu_byte = MAX_VALUE_U8,
	};
	NES_PackedTraceEntry entry = nes_scheduler_trace_pack(expected);
	NES_SchedulerTraceView view = {
		.trace = &entry,
		.index = 1,
		.scheduler_clock = 0x12350020,
	};
	NES_TraceEntry actual = nes_scheduler_trace_at(view, 0);
	Assert(sizeof(entry) == 8);
	Assert(actual.scheduler_clock == expected.scheduler_clock);
	Assert(actual.cpu_address == expected.cpu_address);
	Assert(actual.cpu_mapped.device == expected.cpu_mapped.device);
	Assert(actual.cpu_mapped.offset == expected.cpu_mapped.offset);
	Assert(actual.cpu_byte == expected.cpu_byte);
	Assert(nes_scheduler_trace_clock_reconstructable_since(view, expected.scheduler_clock));
	Assert(!nes_scheduler_trace_clock_reconstructable_since(view, view.scheduler_clock - ((u64)MAX_VALUE_U16 + 1)));

	for (u32 device = NES_DEVICE_NONE; device < NES_DEVICE_COUNT; ++device)
	{
		expected.cpu_mapped.device = (NES_DeviceId)device;
		entry = nes_scheduler_trace_pack(expected);
		actual = nes_scheduler_trace_at((NES_SchedulerTraceView) { .trace = &entry, .index = 1, .scheduler_clock = view.scheduler_clock }, 0);
		Assert(actual.cpu_mapped.device == expected.cpu_mapped.device);
		Assert(actual.cpu_mapped.offset == expected.cpu_mapped.offset);
	}
}

int main(int argc, char **argv)
{
	test_scheduler_trace_packing();

	for (u32 opcode = 0; opcode < 256; ++opcode)
	{
		NES_InstructionDesc instruction = nes_instruction_desc(opcode);
		Assert(instruction.name);
		Assert(instruction.mode <= IND);
		Assert(instruction.size >= 1 && instruction.size <= 3);
		Assert(instruction.classification <= NES_OPCODE_HALT);
		Assert(instruction.classification == NES_OPCODE_HALT || instruction.cycles > 0);
	}
	Assert(nes_instruction_desc(0xEA).classification == NES_OPCODE_OFFICIAL);
	Assert(nes_instruction_desc(0x03).classification == NES_OPCODE_UNOFFICIAL);
	Assert(nes_instruction_desc(0x8B).classification == NES_OPCODE_UNSTABLE);
	Assert(nes_instruction_desc(0x02).classification == NES_OPCODE_HALT);

	b32 seen_field_ids[NES_STATE_FIELD_ID_COUNT] = {};
	u32 field_id_count = 0;
	for (u32 record_id = NES_RECORD_EMULATOR; record_id < NES_RECORD_COUNT; ++record_id)
	{
		const NES_StateRecord *record = nes_state_record((NES_RecordId)record_id);
		Assert(record);
		for (u32 field_index = 0; field_index < record->field_count; ++field_index)
		{
			const NES_StateField *field = &record->fields[field_index];
			Assert(field->id > NES_STATE_FIELD_ID_NONE);
			Assert(field->id < NES_STATE_FIELD_ID_COUNT);
			Assert(!seen_field_ids[field->id]);
			seen_field_ids[field->id] = true;
			++field_id_count;
			Assert(nes_state_field_from_id(field->id) == field);
		}
	}
	Assert(field_id_count == NES_STATE_FIELD_ID_COUNT - 14);
	Assert(!nes_state_field_from_id(NES_STATE_FIELD_ID_NONE));
	Assert(!nes_state_field_from_id(NES_STATE_FIELD_ID_EMULATOR_AUDIO_SAMPLE_PHASE));
	Assert(!nes_state_field_from_id(NES_STATE_FIELD_ID_COUNT));

	const NES_StateRecord *cpu_record =
		nes_state_record(NES_RECORD_CPU);
	const NES_StateRecord *ppu_record =
		nes_state_record(NES_RECORD_PPU);
	const NES_StateRecord *emulator_record =
		nes_state_record(NES_RECORD_EMULATOR);
	Assert(cpu_record && cpu_record->size == sizeof(NES_CPUState));
	Assert(ppu_record && ppu_record->size == sizeof(NES_PPUState));
	Assert(emulator_record && emulator_record->id == NES_RECORD_EMULATOR);
	Assert(emulator_record->size == sizeof(NES_Emulator));
	Assert(serialize_record_from_id(nes_state_record_map(), cpu_record->id) == cpu_record);
	Assert(serialize_record_from_id(nes_state_record_map(), ppu_record->id) == ppu_record);
	Assert(nes_state_visible_field_count(cpu_record) == 6);
	const NES_StateField *program_counter = find_state_field(cpu_record, "PC");
	Assert(program_counter);
	Assert(program_counter->id == NES_STATE_FIELD_ID_CPU_PC);
	Assert(program_counter->wire_type == SERIALIZE_WIRE_U16);
	Assert(program_counter->record_id == 0);
	Assert(program_counter->offset == offsetof(NES_CPUState, PC));
	Assert(program_counter->size == sizeof(((NES_CPUState *)0)->PC));
	Assert(program_counter->flags & NES_STATE_FIELD_SERIALIZED);
	Assert(program_counter->flags & NES_STATE_FIELD_DEBUG_VISIBLE);
	const NES_StateField *emulator_ppu = find_state_field(emulator_record, "ppu");
	Assert(emulator_ppu);
	Assert(emulator_ppu->wire_type == SERIALIZE_WIRE_RECORD);
	Assert(emulator_ppu->record_id == NES_RECORD_PPU);

	Arena arena = arena_create(0, "Orbiter emulator test");
	u32 rom_size = 16 + KiB(16) + KiB(8);
	u8 *rom_data = arena_push_zero(&arena, rom_size);
	rom_data[0] = 'N'; rom_data[1] = 'E'; rom_data[2] = 'S'; rom_data[3] = 0x1A;
	rom_data[4] = 1; // One 16 KiB PRG bank.
	rom_data[5] = 1; // One 8 KiB CHR bank.
	rom_data[16] = 0xEA; // NOP at $8000.
	rom_data[16 + KiB(16)] = 0x80; // Leftmost pixel of CHR tile zero uses palette slot one.
	rom_data[16 + 0x3FFC] = 0x00;
	rom_data[16 + 0x3FFD] = 0x80;
	NES_CartridgeDesc parsed_cartridge = {};
	Assert(!nes_cartridge_parse_ines(byte_span(rom_data, 15), &parsed_cartridge));
	rom_data[7] = 0x08;
	rom_data[12] = 2;
	rom_data[15] = 1;
	Assert(nes_cartridge_parse_ines(byte_span(rom_data, rom_size), &parsed_cartridge));
	rom_data[10] = 0x07;
	rom_data[11] = 0x70;
	Assert(nes_cartridge_parse_ines(byte_span(rom_data, rom_size), &parsed_cartridge));
	rom_data[10] = 0x77;
	Assert(!nes_cartridge_parse_ines(byte_span(rom_data, rom_size), &parsed_cartridge));
	rom_data[10] = 0;
	rom_data[11] = 0;
	rom_data[8] = 1;
	Assert(!nes_cartridge_parse_ines(byte_span(rom_data, rom_size), &parsed_cartridge));
	rom_data[8] = 0;
	rom_data[9] = 1;
	Assert(!nes_cartridge_parse_ines(byte_span(rom_data, rom_size), &parsed_cartridge));
	rom_data[7] = 0;
	rom_data[9] = 0;
	rom_data[12] = 0;
	rom_data[15] = 0;
	Assert(nes_cartridge_parse_ines(byte_span(rom_data, rom_size), &parsed_cartridge));
	Assert(parsed_cartridge.prg_rom.data == rom_data + 16);
	Assert(parsed_cartridge.prg_rom.size == KiB(16));
	Assert(parsed_cartridge.chr_rom.size == KiB(8));

	NES_Emulator *core = arena_push_zero(&arena, sizeof(NES_Emulator));
	Assert(core);
	Assert(!nes_emulator_has_cartridge(core));
	NES_CartridgeDesc oversized_nrom = parsed_cartridge;
	oversized_nrom.prg_rom = byte_span(arena_push_zero(&arena, KiB(48)), KiB(48));
	Assert(!nes_emulator_load_cartridge(core, oversized_nrom));
	Assert(!nes_emulator_has_cartridge(core));
	Assert(nes_emulator_load_cartridge(core, parsed_cartridge));
	Assert(nes_emulator_has_cartridge(core));
	//	NES_CHRMap chr_map = {};
	//	nes_emulator_capture_chr_map(core, &chr_map);
	//	Assert(chr_map.tiles[0].pixels[0][0] == 1);
	//	Assert(chr_map.tiles[0].pixels[0][1] == 0);
	//	Assert(chr_map.mappings[0].device == NES_DEVICE_CHR_ROM);
	//	Assert(chr_map.mappings[0].address == 0);
	u8 obsolete_state_header[12] = {};
	Assert(!nes_emulator_load_state(core, byte_span(obsolete_state_header, sizeof(obsolete_state_header))));
	Assert(nes_emulator_has_cartridge(core));

	NES_CPUState before = core->cpu;
	Assert(before.PC != 0);

	nes_emulator_step(core);
	NES_CPUState after = core->cpu;
	Assert(after.PC != before.PC);
	NES_SchedulerTraceView trace = nes_emulator_scheduler_trace(core);
	Assert(trace.index > 0);
	Assert(!nes_scheduler_trace_dropped_since(trace, 0));
	NES_TraceEntry first = nes_scheduler_trace_at(trace, 0);
	Assert(first.cpu_address == before.PC);
	Assert(first.cpu_mapped.device == NES_DEVICE_PRG_ROM);
	Assert(first.cpu_mapped.offset == 0);
	NES_SchedulerTraceView boundaries = nes_emulator_scheduler_trace(core);
	Assert(boundaries.index > 0);
	first = nes_scheduler_trace_at(boundaries, 0);
	Assert(first.cpu_address == before.PC);
	Assert(first.cpu_mapped.device == NES_DEVICE_PRG_ROM);
	Assert(first.cpu_mapped.offset == 0);
	NES_SchedulerTraceSpans contiguous = nes_scheduler_trace_spans_since(trace, 0);
	Assert(!contiguous.dropped);
	Assert(contiguous.spans[0].entries == trace.trace);
	Assert(contiguous.spans[0].count == trace.index);
	Assert(!contiguous.spans[1].count);

	NES_SchedulerTraceView wrapped = { .trace = trace.trace, .index = NES_SCHEDULER_TRACE_CAPACITY_POW2 + 7, .scheduler_clock = trace.scheduler_clock };
	Assert(nes_scheduler_trace_first_since(wrapped, 0) == 7);
	Assert(nes_scheduler_trace_dropped_since(wrapped, 0) == 7);
	Assert(nes_scheduler_trace_entry_at(wrapped, 7) == &wrapped.trace[7]);
	Assert(nes_scheduler_trace_entry_at(wrapped, wrapped.index - 1) == &wrapped.trace[(wrapped.index - 1) & NES_SCHEDULER_TRACE_CAPACITY_MASK]);
	NES_SchedulerTraceSpans split = nes_scheduler_trace_spans_since(wrapped, 0);
	Assert(split.dropped == 7);
	Assert(split.spans[0].entries == wrapped.trace + 7);
	Assert(split.spans[0].count == NES_SCHEDULER_TRACE_CAPACITY_POW2 - 7);
	Assert(split.spans[1].entries == wrapped.trace);
	Assert(split.spans[1].count == 7);

	NES_CPUState captured_cpu = core->cpu;
	NES_PPUState captured_ppu = core->ppu;
	NES_APUState captured_apu = core->apu;
	Assert(captured_cpu.PC == after.PC);
	Assert(captured_ppu.xtick < 341);
	Assert(captured_ppu.ytick < 262);
	Assert(ArrayCount(captured_ppu.OAM) == 64);
	Assert(ArrayCount(captured_apu.pulse) == 2);

	// Captured values are copies of the actual device structs. Advancing the
	// emulator must not mutate a previously captured state value.
	nes_emulator_step(core);
	Assert(core->cpu.PC != captured_cpu.PC);

	Assert(ArrayCount(core->video) == NES_VIDEO_HEIGHT);
	Assert(ArrayCount(core->video[0]) == NES_VIDEO_WIDTH);

	NES_PPUState state_before_save_ppu = core->ppu;
	NES_PPUState state_after_load_ppu = {};
	core->video[7][11] = 0x2A;
	SCRATCH_SCOPE(&arena)
	{
		ByteSpan state = nes_emulator_save_state(core, &arena);
		Assert(state.data && state.size);
		NES_Emulator *before_failed_load = arena_push(&arena,
			sizeof(*before_failed_load));
		memory_copy(before_failed_load, core, sizeof(*core));
		u64 truncated_sizes[] = {
			0,
			1,
			11,
			12,
			state.size - 1,
		};
		for (u32 index = 0; index < ArrayCount(truncated_sizes); ++index)
		{
			Assert(!nes_emulator_load_state(core,
				byte_span(state.data, truncated_sizes[index])));
			Assert(memory_match(core, before_failed_load, sizeof(*core)));
		}
		u8 *corrupt_state = arena_push_copy(&arena, state.size, state.data);
		corrupt_state[8] ^= 0x80;
		Assert(!nes_emulator_load_state(core,
			byte_span(corrupt_state, state.size)));
		Assert(memory_match(core, before_failed_load, sizeof(*core)));
		u32 valid_xtick = core->ppu.xtick;
		core->ppu.xtick = 341;
		ByteSpan invalid_state = nes_emulator_save_state(core, &arena);
		core->ppu.xtick = valid_xtick;
		Assert(!nes_emulator_load_state(core, invalid_state));
		Assert(memory_match(core, before_failed_load, sizeof(*core)));

		const char *test_state_path = "build/nes_emulator_test_state.nesstate";
		Assert(test_write_file(test_state_path, state.data, state.size));
		Str disk_state = test_read_file(&arena, test_state_path);
		Assert(disk_state.text && disk_state.size == state.size);
		Assert(memory_match(disk_state.text, state.data, state.size));
		nes_emulator_step(core);
		core->video[7][11] = 0;
		Assert(nes_emulator_load_state(core, byte_span(disk_state.text, disk_state.size)));
		state_after_load_ppu = core->ppu;
	}
	assert_serialized_fields_equal(ppu_record, &state_before_save_ppu, &state_after_load_ppu);
	Assert(core->video[7][11] == 0x2A);

	Assert(core->ppu.xtick > 0);
	Assert(core->apu.cpu_cycle_counter < 7457);

	Assert(nes_emulator_load_cartridge(core, parsed_cartridge));
	Assert(core->cpu.PC == before.PC);
	Assert(core->ppu.xtick == 0);
	Assert(core->ppu.ytick == 0);
	Assert(core->apu.mode == 0);
	Assert(core->apu.step_index == 0);
	Assert(core->apu.cpu_cycle_counter == 0);

	if (argc > 1)
	{
		SCRATCH_SCOPE(&arena)
		{
			Str external_state = test_read_file(&arena, argv[1]);
			Assert(external_state.text && external_state.size);
			NES_Emulator *external_core = arena_push_zero(&arena, sizeof(NES_Emulator));
			Assert(nes_emulator_load_state(external_core,
				byte_span(external_state.text, external_state.size)));
			Assert(external_core->cpu.PC != 0);
			Assert(external_core->ppu.xtick < 341);
			Assert(external_core->ppu.ytick < 262);
		}
	}

	return 0;
}
