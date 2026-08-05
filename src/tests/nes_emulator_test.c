#include "base.h"
#include "nes/emulator.h"
#include "nes/isa.h"

int main(void)
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
	u8 *prg_rom = arena_push_zero(&arena, KiB(16));
	u8 *chr_rom = arena_push_zero(&arena, KiB(8));
	prg_rom[0] = 0xEA; // NOP at $8000.
	prg_rom[0x3FFC] = 0x00;
	prg_rom[0x3FFD] = 0x80;
	chr_rom[0] = 0x80; // Leftmost pixel of CHR tile zero uses palette slot one.

	NES_SetupParams setup = {
		.mapper = 0,
		.prg_rom = byte_span(prg_rom, KiB(16)),
		.chr_rom = byte_span(chr_rom, KiB(8)),
	};
	NES_Emulator *core = arena_push_zero(&arena, sizeof(NES_Emulator));
	Assert(core);
	Assert(!nes_emulator_ready_to_run(core));

	NES_SetupParams invalid = setup;
	invalid.prg_rom = byte_span(arena_push_zero(&arena, KiB(48)), KiB(48));
	Assert(!nes_setup_emulator(core, invalid));
	Assert(!nes_emulator_ready_to_run(core));

	Assert(nes_setup_emulator(core, setup));
	Assert(nes_emulator_ready_to_run(core));
	Assert(nes_emulator_valid(core));

	NES_CPUState before = core->cpu;
	Assert(before.PC == 0x8000);

	NES_TraceEntry trace;
	Assert(nes_emulator_step(core, &trace) == 2);
	NES_CPUState after = core->cpu;
	Assert(after.PC == 0x8001);
	Assert(trace.cpu_address == before.PC);
	Assert(trace.cpu_mapped.device == NES_DEVICE_PRG_ROM);
	Assert(trace.cpu_mapped.offset == 0);
	Assert(trace.cpu_byte == 0xEA);

	NES_CPUState captured_cpu = core->cpu;
	NES_PPUState captured_ppu = core->ppu;
	NES_APUState captured_apu = core->apu;
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
	Assert(core->ppu.xtick > 0);
	Assert(core->apu.cpu_cycle_counter < 7457);

	// Setup is the single fresh-load boundary and resets all live device state.
	core->video[7][11] = 0x2A;
	core->_wram[5] = 0xA5;
	Assert(nes_setup_emulator(core, setup));
	Assert(core->cpu.PC == before.PC);
	Assert(core->ppu.xtick == 0);
	Assert(core->ppu.ytick == 0);
	Assert(core->apu.mode == 0);
	Assert(core->apu.step_index == 0);
	Assert(core->apu.cpu_cycle_counter == 0);
	Assert(core->video[7][11] == 0);
	Assert(core->_wram[5] == 0);

	// The reset boundary keeps writable memory so reset-driven test ROMs can
	// communicate across the reset, then reloads PC from the reset vector.
	core->_wram[5] = 0xA5;
	core->prg_ram[7] = 0x5A;
	core->cpu.A = 0x11;
	core->cpu.X = 0x22;
	core->cpu.Y = 0x33;
	core->cpu.S = 0x80;
	core->cpu.P = 0x61;
	core->cpu.PC = 0x8123;
	nes_reset_emulator(core);
	Assert(core->_wram[5] == 0xA5);
	Assert(core->prg_ram[7] == 0x5A);
	Assert(core->cpu.A == 0x11);
	Assert(core->cpu.X == 0x22);
	Assert(core->cpu.Y == 0x33);
	Assert(core->cpu.S == 0x7D);
	Assert(core->cpu.P == 0x65);
	Assert(core->cpu.PC == 0x8000);

	arena_destroy(&arena);
	return 0;
}
