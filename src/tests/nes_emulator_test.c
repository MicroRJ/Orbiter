#include "base.h"
#include "os.h"
#include "nes/emulator.h"
#include "nes/isa.h"
#include "nes_serialize.h"
#include "emulator_internal.h"

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

static NES_SetupParams test_setup_params(NES_CartridgeDesc cartridge)
{
	return (NES_SetupParams) {
		.mapper = cartridge.mapper,
		.vmirror = cartridge.vmirror,
		.four_screen = cartridge.four_screen,
		.has_trainer = cartridge.has_trainer,
		.prg_rom = cartridge.prg_rom,
		.chr_rom = cartridge.chr_rom,
	};
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
	Assert(!nes_emulator_ready_to_run(core));
	NES_CartridgeDesc oversized_nrom = parsed_cartridge;
	oversized_nrom.prg_rom = byte_span(arena_push_zero(&arena, KiB(48)), KiB(48));
	Assert(!nes_setup_emulator(core, test_setup_params(oversized_nrom)));
	Assert(!nes_emulator_ready_to_run(core));
	Assert(nes_setup_emulator(core, test_setup_params(parsed_cartridge)));
	Assert(nes_emulator_ready_to_run(core));
	//	NES_CHRMap chr_map = {};
	//	nes_emulator_capture_chr_map(core, &chr_map);
	//	Assert(chr_map.tiles[0].pixels[0][0] == 1);
	//	Assert(chr_map.tiles[0].pixels[0][1] == 0);
	//	Assert(chr_map.mappings[0].device == NES_DEVICE_CHR_ROM);
	//	Assert(chr_map.mappings[0].address == 0);
	u8 obsolete_state_header[12] = {};
	Assert(!orb_transfer_save_state_no_chunk(core, byte_span(obsolete_state_header, sizeof(obsolete_state_header))));
	Assert(nes_emulator_ready_to_run(core));

	NES_CPUState before = core->cpu;
	Assert(before.PC != 0);

	NES_TraceEntry trace;
	nes_emulator_step(core, &trace);
	NES_CPUState after = core->cpu;
	Assert(after.PC != before.PC);
	Assert(trace.cpu_address == before.PC);
	Assert(trace.cpu_mapped.device == NES_DEVICE_PRG_ROM);
	Assert(trace.cpu_mapped.offset == 0);

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
	nes_emulator_step(core, 0);
	Assert(core->cpu.PC != captured_cpu.PC);

	Assert(ArrayCount(core->video) == NES_VIDEO_HEIGHT);
	Assert(ArrayCount(core->video[0]) == NES_VIDEO_WIDTH);

	NES_PPUState state_before_save_ppu = core->ppu;
	NES_PPUState state_after_load_ppu = {};
	core->video[7][11] = 0x2A;
	SCRATCH_SCOPE(&arena)
	{
		ByteSpan state = orb_nes_state_encode(&arena, core);
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
			Assert(!orb_transfer_save_state_no_chunk(core,
				byte_span(state.data, truncated_sizes[index])));
			Assert(memory_match(core, before_failed_load, sizeof(*core)));
		}
		u8 *corrupt_state = arena_push_copy(&arena, state.size, state.data);
		corrupt_state[8] ^= 0x80;
		Assert(!orb_transfer_save_state_no_chunk(core,
			byte_span(corrupt_state, state.size)));
		Assert(memory_match(core, before_failed_load, sizeof(*core)));
		u32 valid_xtick = core->ppu.xtick;
		core->ppu.xtick = 341;
		ByteSpan invalid_state = orb_nes_state_encode(&arena, core);
		core->ppu.xtick = valid_xtick;
		Assert(!orb_transfer_save_state_no_chunk(core, invalid_state));
		Assert(memory_match(core, before_failed_load, sizeof(*core)));

		const char *test_state_path = "build/nes_emulator_test_state.nesstate";
		Assert(test_write_file(test_state_path, state.data, state.size));
		Str disk_state = test_read_file(&arena, test_state_path);
		Assert(disk_state.text && disk_state.size == state.size);
		Assert(memory_match(disk_state.text, state.data, state.size));
		nes_emulator_step(core, 0);
		core->video[7][11] = 0;
		Assert(orb_transfer_save_state_no_chunk(core, byte_span(disk_state.text, disk_state.size)));
		state_after_load_ppu = core->ppu;
	}
	Assert(memory_match(&state_before_save_ppu, &state_after_load_ppu, sizeof(state_before_save_ppu)));
	Assert(core->video[7][11] == 0x2A);

	Assert(core->ppu.xtick > 0);
	Assert(core->apu.cpu_cycle_counter < 7457);

	Assert(nes_setup_emulator(core, test_setup_params(parsed_cartridge)));
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
			Assert(nes_setup_emulator(external_core, test_setup_params(parsed_cartridge)));
			Assert(orb_transfer_save_state_no_chunk(external_core,
				byte_span(external_state.text, external_state.size)));
			Assert(external_core->cpu.PC != 0);
			Assert(external_core->ppu.xtick < 341);
			Assert(external_core->ppu.ytick < 262);
		}
	}

	return 0;
}
