#include "base.h"
#include "os.h"
#include "nes/emulator.h"
#include "nes/isa.h"
#include "nes/state_meta.h"
#include "emulator_internal.h"
#include <string.h>

static String test_read_file(Arena *arena, const char *path)
{
	String result = {};
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
			result = string_from_data((char *)data, (u32)size);
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

int main(int argc, char **argv)
{
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
	for (u32 record_id = NES_RECORD_MACHINE; record_id < NES_RECORD_COUNT; ++record_id)
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
	Assert(field_id_count == NES_STATE_FIELD_ID_COUNT - 13);
	Assert(!nes_state_field_from_id(NES_STATE_FIELD_ID_NONE));
	Assert(!nes_state_field_from_id(NES_STATE_FIELD_ID_COUNT));

	const NES_StateRecord *cpu_record =
		nes_state_record(NES_RECORD_CPU);
	const NES_StateRecord *ppu_record =
		nes_state_record(NES_RECORD_PPU);
	const NES_StateRecord *machine_record =
		nes_state_record(NES_RECORD_MACHINE);
	Assert(cpu_record && cpu_record->size == sizeof(NES_CPUState));
	Assert(ppu_record && ppu_record->size == sizeof(NES_PPUState));
	Assert(machine_record && machine_record->id == NES_RECORD_MACHINE);
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
	const NES_StateField *machine_ppu = find_state_field(machine_record, "ppu");
	Assert(machine_ppu);
	Assert(machine_ppu->wire_type == SERIALIZE_WIRE_RECORD);
	Assert(machine_ppu->record_id == NES_RECORD_PPU);

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
	Assert(nes_cartridge_parse_ines(byte_span(rom_data, rom_size), &parsed_cartridge));
	Assert(parsed_cartridge.prg_rom.data == rom_data + 16);
	Assert(parsed_cartridge.prg_rom.size == KiB(16));
	Assert(parsed_cartridge.chr_rom.size == KiB(8));

	NES_Emulator *core = nes_emulator_create(&arena, (NES_EmulatorDesc) {
		.enable_instruction_trace = true,
		.enable_instruction_boundaries = true,
	});
	Assert(core);
	Assert(!nes_emulator_has_cartridge(core));
	Assert(nes_emulator_cpu_map(core, 0x8000).device == NES_DEVICE_CPU);
	Assert(nes_emulator_load_cartridge(core, parsed_cartridge));
	Assert(nes_emulator_has_cartridge(core));
	NES_CHRMap chr_map = {};
	nes_emulator_capture_chr_map(core, &chr_map);
	Assert(chr_map.tiles[0].pixels[0][0] == 1);
	Assert(chr_map.tiles[0].pixels[0][1] == 0);
	Assert(chr_map.mappings[0].device == NES_DEVICE_CHR_ROM);
	Assert(chr_map.mappings[0].address == 0);
	u8 obsolete_state_header[12] = {};
	Assert(!nes_emulator_load_state(core, byte_span(obsolete_state_header, sizeof(obsolete_state_header))));
	Assert(nes_emulator_has_cartridge(core));

	nes_emulator_reset(core);
	NES_CPUState before = nes_emulator_cpu_state(core);
	Assert(before.PC != 0);

	nes_emulator_run(core, 3);
	NES_CPUState after = nes_emulator_cpu_state(core);
	Assert(after.PC != before.PC);
	NES_InstructionTraceSpan trace = nes_emulator_instruction_trace(core);
	Assert(trace.count > 0);
	Assert(trace.dropped == 0);
	Assert(trace.events[0].cpu_address == before.PC);
	Assert(trace.events[0].size == 1);
	Assert(trace.events[0].mappings[0].device == NES_DEVICE_PRG_ROM);
	Assert(trace.events[0].mappings[0].offset == 0);
	NES_InstructionBoundarySpan boundaries = nes_emulator_instruction_boundaries(core);
	Assert(boundaries.count > 0);
	Assert(boundaries.items[0].cpu_address == before.PC);
	Assert(boundaries.items[0].program_address.device == NES_DEVICE_PRG_ROM);
	Assert(boundaries.items[0].program_address.offset == 0);

	NES_CPUState captured_cpu = nes_emulator_cpu_state(core);
	NES_PPUState captured_ppu = nes_emulator_ppu_state(core);
	NES_APUState captured_apu = nes_emulator_apu_state(core);
	Assert(captured_cpu.PC == after.PC);
	Assert(captured_ppu.xtick < 341);
	Assert(captured_ppu.ytick < 262);
	Assert(ArrayCount(captured_ppu.OAM) == 64);
	Assert(ArrayCount(captured_apu.pulse) == 2);

	// Published values are copies of the actual device structs. Advancing the
	// emulator must not mutate a state value already handed to the frontend.
	nes_emulator_run(core, 3);
	Assert(nes_emulator_cpu_state(core).PC != captured_cpu.PC);

	NES_VideoFrame video = nes_emulator_video_frame(core);
	Assert(video.pixels);
	Assert(video.width == NES_VIDEO_WIDTH);
	Assert(video.height == NES_VIDEO_HEIGHT);
	Assert(video.stride >= video.width);

	NES_PPUState state_before_save_ppu = nes_emulator_ppu_state(core);
	NES_PPUState state_after_load_ppu = {};
	core->video[7][11] = 0x2A;
	u64 saved_audio_sample_phase = core->core.audio_sample_phase;
	ARENA_SCOPE(&arena)
	{
		ByteSpan state = nes_emulator_save_state(core, &arena);
		Assert(state.data && state.size);
		const char *test_state_path = "build/nes_emulator_test_state.nesstate";
		Assert(test_write_file(test_state_path, state.data, state.size));
		String disk_state = test_read_file(&arena, test_state_path);
		Assert(disk_state.text && disk_state.size == state.size);
		Assert(memory_match(disk_state.text, state.data, state.size));
		nes_emulator_run(core, 64);
		core->video[7][11] = 0;
		core->core.audio_sample_phase = 0;
		Assert(nes_emulator_load_state(core,
			byte_span(disk_state.text, disk_state.size)));
		state_after_load_ppu = nes_emulator_ppu_state(core);
	}
	assert_serialized_fields_equal(ppu_record, &state_before_save_ppu, &state_after_load_ppu);
	Assert(core->video[7][11] == 0x2A);
	Assert(core->core.audio_sample_phase == saved_audio_sample_phase);

	Assert(nes_emulator_ppu_state(core).xtick > 0);
	Assert(nes_emulator_apu_state(core).cpu_cycle_counter < 7457);

	nes_emulator_reset(core);
	Assert(nes_emulator_cpu_state(core).PC == before.PC);
	Assert(nes_emulator_ppu_state(core).xtick == 0);
	Assert(nes_emulator_ppu_state(core).ytick == 0);
	Assert(nes_emulator_apu_state(core).mode == 0);
	Assert(nes_emulator_apu_state(core).step_index == 0);
	Assert(nes_emulator_apu_state(core).cpu_cycle_counter == 0);

	if (argc > 1)
	{
		ARENA_SCOPE(&arena)
		{
			String external_state = test_read_file(&arena, argv[1]);
			Assert(external_state.text && external_state.size);
			NES_Emulator *external_core = nes_emulator_create(&arena, (NES_EmulatorDesc) {});
			Assert(nes_emulator_load_state(external_core,
				byte_span(external_state.text, external_state.size)));
			Assert(nes_emulator_cpu_state(external_core).PC != 0);
			Assert(nes_emulator_ppu_state(external_core).xtick < 341);
			Assert(nes_emulator_ppu_state(external_core).ytick < 262);
		}
	}

	return 0;
}
