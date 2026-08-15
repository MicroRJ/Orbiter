#include "base.h"
#include "nes/emulator.h"
#include "emulator_internal.h"
#include "bus/bus.h"
#include "cpu/cpu.h"
#include <stdio.h>

typedef struct
{
	Arena arena;
	NES_Emulator *core;
}
CPU_TestFixture;

static u32 cpu_test_failures;
static const char *cpu_test_name;

static NES_BusResult cpu_test_expansion_bus(NES_Emulator *core,
	NES_BusMode mode, u32 address, u8 value)
{
	if (address == 0x4020 && mode == NES_BUS_WRITE)
	{
		core->values[31] = value;
	}
	return nes_bus_result(NES_DEVICE_CPU, address, value);
}

static void cpu_expect_equal_(u64 expected, u64 actual, const char *expression, i32 line)
{
	if (expected != actual)
	{
		fprintf(stderr, "%s:%d: %s: expected 0x%llX, got 0x%llX\n",
			__FILE__, line, cpu_test_name, expected, actual);
		fprintf(stderr, "    %s\n", expression);
		++cpu_test_failures;
	}
}

#define CPU_EXPECT_EQUAL(expected, actual) \
	cpu_expect_equal_((u64)(expected), (u64)(actual), #actual, __LINE__)

static CPU_TestFixture cpu_test_fixture_create(void)
{
	CPU_TestFixture fixture = {};
	fixture.arena = arena_create(0, "Orbiter CPU tests");

	u8 *prg_rom = arena_push_zero(&fixture.arena, KiB(16));
	u8 *chr_rom = arena_push_zero(&fixture.arena, KiB(8));
	prg_rom[0x3FFC] = 0x00;
	prg_rom[0x3FFD] = 0x80;

	fixture.core = arena_push_zero(&fixture.arena, sizeof(NES_Emulator));
	Assert(fixture.core);
	Assert(nes_setup_emulator(fixture.core, (NES_Game) {
		.metadata = { .mirroring = NES_MIRROR_HORIZONTAL, .prg_rom_size = KiB(16), .chr_rom_size = KiB(8) },
		.prg_rom = prg_rom,
		.chr_rom = chr_rom,
	}));
	return fixture;
}

static NES_CPUState *cpu_test_state(CPU_TestFixture *fixture)
{
	return &fixture->core->cpu;
}

static void cpu_test_prepare(CPU_TestFixture *fixture)
{
	NES_Emulator *core = fixture->core;
	memory_zero(&core->cpu, sizeof(core->cpu));
	memory_zero(&core->input_state, sizeof(core->input_state));
	memory_zero(&core->controllers, sizeof(core->controllers));
	memory_zero(core->_wram, sizeof(core->_wram));
	memory_zero(core->prg_rom, core->prg_rom_size);
	core->prg_rom[0x3FFC] = 0x00;
	core->prg_rom[0x3FFD] = 0x80;
	core->cpu.PC = 0x8000;
	core->cpu.S = 0xFD;
}

static void cpu_test_program(CPU_TestFixture *fixture, u16 address, const u8 *program, u32 size)
{
	Assert(address >= 0x8000);
	u32 offset = (address - 0x8000) & 0x3FFF;
	Assert(offset + size <= fixture->core->prg_rom_size);
	memory_copy(fixture->core->prg_rom + offset, program, size);
}

#define CPU_TEST_PROGRAM(fixture, address, ...) do \
{ \
	u8 program_[] = { __VA_ARGS__ }; \
	cpu_test_program((fixture), (address), program_, ArrayCount(program_)); \
} while (0)

static void cpu_test_write(CPU_TestFixture *fixture, u16 address, u8 value)
{
	nes_cpu_bus_write(fixture->core, address, value);
}

static u8 cpu_test_read(CPU_TestFixture *fixture, u16 address)
{
	return nes_cpu_bus_peek(fixture->core, address);
}

static void cpu_test_explicit_bus_operations(CPU_TestFixture *fixture)
{
	cpu_test_name = "explicit CPU bus operations";
	cpu_test_prepare(fixture);

	// CPU-facing code no longer constructs mode flags or mapping outputs for
	// ordinary accesses. Mirroring and physical-address reporting remain bus
	// responsibilities behind the explicit operations.
	nes_cpu_bus_write(fixture->core, 0x1805, 0xA5);
	CPU_EXPECT_EQUAL(0xA5, nes_cpu_bus_read(fixture->core, 0x0005));
	CPU_EXPECT_EQUAL(0xA5, nes_cpu_bus_peek(fixture->core, 0x0805));

	NES_MapAddr wram = nes_cpu_bus_map(fixture->core, 0x1805);
	CPU_EXPECT_EQUAL(NES_DEVICE_WRAM, wram.device);
	CPU_EXPECT_EQUAL(5, wram.offset);

	NES_MapAddr prg = nes_cpu_bus_map(fixture->core, 0x8000);
	CPU_EXPECT_EQUAL(NES_DEVICE_PRG_ROM, prg.device);
	CPU_EXPECT_EQUAL(0, prg.offset);

	NES_BusResult routed = nrom_cpu(fixture->core, NES_BUS_PEEK, 0x8000, 0);
	CPU_EXPECT_EQUAL(0, routed.address);
	CPU_EXPECT_EQUAL(NES_DEVICE_PRG_ROM, routed.device);

	NES_BusFunc saved_cpu_bus = fixture->core->mapper.cpu_bus;
	u8 saved_expansion_value = fixture->core->values[31];
	fixture->core->mapper.cpu_bus = cpu_test_expansion_bus;
	nes_cpu_bus_write(fixture->core, 0x4020, 0x6C);
	CPU_EXPECT_EQUAL(0x6C, fixture->core->values[31]);
	fixture->core->mapper.cpu_bus = saved_cpu_bus;
	fixture->core->values[31] = saved_expansion_value;

	// A real read also carries internal side-effect policy. MMC2 used to test
	// the complete mode as a boolean, misclassify that read as a write, and
	// overwrite its selected PRG bank with the read placeholder byte.
	NES_MapperClass saved_mapper = fixture->core->mapper;
	u32 saved_prg_rom_size = fixture->core->prg_rom_size;
	u8 saved_mapper_values[ArrayCount(fixture->core->values)];
	memory_copy(saved_mapper_values, fixture->core->values, sizeof(saved_mapper_values));
	fixture->core->mapper.cpu_bus = mmc2_cpu;
	fixture->core->prg_rom_size = KiB(256);
	Assert(mmc2_reset(fixture->core));
	nes_cpu_bus_write(fixture->core, 0xA000, 5);
	nes_cpu_bus_read(fixture->core, 0x8000);
	NES_MapAddr mmc2_prg = nes_cpu_bus_map(fixture->core, 0x8000);
	CPU_EXPECT_EQUAL(NES_DEVICE_PRG_ROM, mmc2_prg.device);
	CPU_EXPECT_EQUAL(5 * KiB(8), mmc2_prg.offset);
	fixture->core->mapper = saved_mapper;
	fixture->core->prg_rom_size = saved_prg_rom_size;
	memory_copy(fixture->core->values, saved_mapper_values, sizeof(saved_mapper_values));

	fixture->core->ppu.PPUSTATUS = 0xE0;
	NES_MapAddr status = nes_cpu_bus_map(fixture->core, 0x2002);
	CPU_EXPECT_EQUAL(NES_DEVICE_PPU, status.device);
	CPU_EXPECT_EQUAL(2, status.offset);
	CPU_EXPECT_EQUAL(0xE0, fixture->core->ppu.PPUSTATUS);
	CPU_EXPECT_EQUAL(0xE0, nes_cpu_bus_read(fixture->core, 0x2002));
	CPU_EXPECT_EQUAL(0x60, fixture->core->ppu.PPUSTATUS);
}

static void cpu_test_input_and_controllers(CPU_TestFixture *fixture)
{
	cpu_test_name = "input and controllers";
	cpu_test_prepare(fixture);
	nes_emulator_set_input(fixture->core, 0, NES_INPUT_A | NES_INPUT_RIGHT);
	nes_cpu_bus_write(fixture->core, 0x4016, 1);
	CPU_EXPECT_EQUAL(1, nes_cpu_bus_read(fixture->core, 0x4016));

}

static u32 cpu_test_step(CPU_TestFixture *fixture)
{
	return nes_cpu_step(fixture->core);
}

static b32 cpu_test_flag(NES_CPUState *cpu, u32 flag)
{
	return !!(cpu->P & (1 << flag));
}
static void cpu_test_reset_vector(CPU_TestFixture *fixture)
{
	cpu_test_name = "reset vector";
	cpu_test_prepare(fixture);
	fixture->core->prg_rom[0x3FFC] = 0x23;
	fixture->core->prg_rom[0x3FFD] = 0x81;
	NES_CPUState *cpu = cpu_test_state(fixture);
	cpu->A = 0x11;
	cpu->X = 0x22;
	cpu->Y = 0x33;
	cpu->S = 0x44;
	cpu->P = cpu_status_mask(CPU_STAT_C);
	nes_cpu_power_on(fixture->core);
	CPU_EXPECT_EQUAL(0, cpu->A);
	CPU_EXPECT_EQUAL(0, cpu->X);
	CPU_EXPECT_EQUAL(0, cpu->Y);
	CPU_EXPECT_EQUAL(0x8123, cpu->PC);
	CPU_EXPECT_EQUAL(0xFD, cpu->S);
	CPU_EXPECT_EQUAL(0x24, cpu->P);

	cpu->A = 0x11;
	cpu->X = 0x22;
	cpu->Y = 0x33;
	cpu->S = 0x80;
	cpu->P = cpu_status_mask(CPU_STAT_C) | cpu_status_mask(CPU_STAT_V) | cpu_status_mask(CPU_STAT_1);
	nes_cpu_reset(fixture->core);
	CPU_EXPECT_EQUAL(0x11, cpu->A);
	CPU_EXPECT_EQUAL(0x22, cpu->X);
	CPU_EXPECT_EQUAL(0x33, cpu->Y);
	CPU_EXPECT_EQUAL(0x8123, cpu->PC);
	CPU_EXPECT_EQUAL(0x7D, cpu->S);
	CPU_EXPECT_EQUAL(0x65, cpu->P);
}

static void cpu_test_load_flags(CPU_TestFixture *fixture)
{
	cpu_test_name = "load flags";
	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x8000, 0xA9, 0x00, 0xA9, 0x80);
	NES_CPUState *cpu = cpu_test_state(fixture);

	CPU_EXPECT_EQUAL(2, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x00, cpu->A);
	CPU_EXPECT_EQUAL(1, cpu_test_flag(cpu, CPU_STAT_Z));
	CPU_EXPECT_EQUAL(0, cpu_test_flag(cpu, CPU_STAT_N));

	CPU_EXPECT_EQUAL(2, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x80, cpu->A);
	CPU_EXPECT_EQUAL(0, cpu_test_flag(cpu, CPU_STAT_Z));
	CPU_EXPECT_EQUAL(1, cpu_test_flag(cpu, CPU_STAT_N));
	CPU_EXPECT_EQUAL(0x8004, cpu->PC);
}

static void cpu_test_store_and_zero_page_wrap(CPU_TestFixture *fixture)
{
	cpu_test_name = "store and zero-page wrap";
	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x8000,
		0xA9, 0x42,       // LDA #$42
		0x85, 0x10,       // STA $10
		0xA2, 0x01,       // LDX #$01
		0xB5, 0xFF);      // LDA $FF,X
	cpu_test_write(fixture, 0x0000, 0x7E);
	NES_CPUState *cpu = cpu_test_state(fixture);

	CPU_EXPECT_EQUAL(2, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(3, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x42, cpu_test_read(fixture, 0x0010));
	CPU_EXPECT_EQUAL(2, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(4, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x7E, cpu->A);
}

static void cpu_test_adc_sbc(CPU_TestFixture *fixture)
{
	cpu_test_name = "ADC and SBC";
	NES_CPUState *cpu;

	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x8000, 0x69, 0x50);
	cpu = cpu_test_state(fixture);
	cpu->A = 0x50;
	cpu_test_step(fixture);
	CPU_EXPECT_EQUAL(0xA0, cpu->A);
	CPU_EXPECT_EQUAL(0, cpu_test_flag(cpu, CPU_STAT_C));
	CPU_EXPECT_EQUAL(1, cpu_test_flag(cpu, CPU_STAT_V));
	CPU_EXPECT_EQUAL(1, cpu_test_flag(cpu, CPU_STAT_N));

	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x8000, 0x69, 0x01);
	cpu = cpu_test_state(fixture);
	cpu->A = 0xFF;
	cpu_test_step(fixture);
	CPU_EXPECT_EQUAL(0x00, cpu->A);
	CPU_EXPECT_EQUAL(1, cpu_test_flag(cpu, CPU_STAT_C));
	CPU_EXPECT_EQUAL(1, cpu_test_flag(cpu, CPU_STAT_Z));
	CPU_EXPECT_EQUAL(0, cpu_test_flag(cpu, CPU_STAT_V));

	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x8000, 0xE9, 0x10);
	cpu = cpu_test_state(fixture);
	cpu->A = 0x50;
	cpu->P = 1 << CPU_STAT_C;
	cpu_test_step(fixture);
	CPU_EXPECT_EQUAL(0x40, cpu->A);
	CPU_EXPECT_EQUAL(1, cpu_test_flag(cpu, CPU_STAT_C));
}

static void cpu_test_shifts_and_rotates(CPU_TestFixture *fixture)
{
	cpu_test_name = "shifts and rotates";
	NES_CPUState *cpu;

	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x8000, 0x0A);
	cpu = cpu_test_state(fixture);
	cpu->A = 0x81;
	CPU_EXPECT_EQUAL(2, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x02, cpu->A);
	CPU_EXPECT_EQUAL(1, cpu_test_flag(cpu, CPU_STAT_C));
	CPU_EXPECT_EQUAL(0, cpu_test_flag(cpu, CPU_STAT_N));

	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x8000, 0x6A);
	cpu = cpu_test_state(fixture);
	cpu->A = 0x01;
	cpu->P = 1 << CPU_STAT_C;
	CPU_EXPECT_EQUAL(2, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x80, cpu->A);
	CPU_EXPECT_EQUAL(1, cpu_test_flag(cpu, CPU_STAT_C));
	CPU_EXPECT_EQUAL(1, cpu_test_flag(cpu, CPU_STAT_N));
}

static void cpu_test_branches(CPU_TestFixture *fixture)
{
	cpu_test_name = "branches";
	NES_CPUState *cpu;

	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x8000, 0xD0, 0x02); // BNE +2
	cpu = cpu_test_state(fixture);
	CPU_EXPECT_EQUAL(3, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x8004, cpu->PC);

	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x8000, 0xD0, 0x02);
	cpu = cpu_test_state(fixture);
	cpu->P = 1 << CPU_STAT_Z;
	CPU_EXPECT_EQUAL(2, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x8002, cpu->PC);

	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x80FD, 0xD0, 0x01); // PC after operand is $80FF; target is $8100
	cpu = cpu_test_state(fixture);
	cpu->PC = 0x80FD;
	CPU_EXPECT_EQUAL(4, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x8100, cpu->PC);
}

static void cpu_test_indexed_addressing(CPU_TestFixture *fixture)
{
	cpu_test_name = "indexed addressing and page-cross cycles";
	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x8000,
		0xA2, 0x01,             // LDX #$01
		0xBD, 0xFE, 0x80,       // LDA $80FE,X: no page crossing
		0xBD, 0xFF, 0x80);      // LDA $80FF,X: crosses into $8100
	CPU_TEST_PROGRAM(fixture, 0x80FF, 0x31);
	CPU_TEST_PROGRAM(fixture, 0x8100, 0x42);
	NES_CPUState *cpu = cpu_test_state(fixture);

	CPU_EXPECT_EQUAL(2, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(4, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x31, cpu->A);
	CPU_EXPECT_EQUAL(5, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x42, cpu->A);

	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x8000, 0xA1, 0xFF); // LDA ($FF,X), X=1: pointer wraps to $00
	cpu = cpu_test_state(fixture);
	cpu->X = 1;
	cpu_test_write(fixture, 0x0000, 0x34);
	cpu_test_write(fixture, 0x0001, 0x12);
	cpu_test_write(fixture, 0x1234, 0x7E);
	CPU_EXPECT_EQUAL(6, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x7E, cpu->A);

	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x8000, 0xB1, 0xFF); // LDA ($FF),Y: high pointer byte wraps to $00
	cpu = cpu_test_state(fixture);
	cpu->Y = 1;
	cpu_test_write(fixture, 0x00FF, 0xFF);
	cpu_test_write(fixture, 0x0000, 0x12);
	cpu_test_write(fixture, 0x1300, 0x55);
	CPU_EXPECT_EQUAL(6, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x55, cpu->A);
}

static void cpu_test_memory_read_modify_write(CPU_TestFixture *fixture)
{
	cpu_test_name = "memory read-modify-write";
	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x8000,
		0x06, 0x10,       // ASL $10
		0xE6, 0x10,       // INC $10
		0xC6, 0x10);      // DEC $10
	cpu_test_write(fixture, 0x0010, 0x81);
	NES_CPUState *cpu = cpu_test_state(fixture);

	CPU_EXPECT_EQUAL(5, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x02, cpu_test_read(fixture, 0x0010));
	CPU_EXPECT_EQUAL(1, cpu_test_flag(cpu, CPU_STAT_C));
	CPU_EXPECT_EQUAL(5, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x03, cpu_test_read(fixture, 0x0010));
	CPU_EXPECT_EQUAL(5, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x02, cpu_test_read(fixture, 0x0010));
}

static void cpu_test_brk_rti(CPU_TestFixture *fixture)
{
	cpu_test_name = "BRK and RTI stack behavior";
	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x8000, 0x00, 0xEA); // BRK has a padding byte
	CPU_TEST_PROGRAM(fixture, 0x8100, 0x40);       // RTI
	fixture->core->prg_rom[0x3FFE] = 0x00;
	fixture->core->prg_rom[0x3FFF] = 0x81;
	NES_CPUState *cpu = cpu_test_state(fixture);
	cpu->P = 1 << CPU_STAT_C;

	CPU_EXPECT_EQUAL(7, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x8100, cpu->PC);
	CPU_EXPECT_EQUAL(0xFA, cpu->S);
	CPU_EXPECT_EQUAL(0x80, cpu_test_read(fixture, 0x01FD));
	CPU_EXPECT_EQUAL(0x02, cpu_test_read(fixture, 0x01FC));
	CPU_EXPECT_EQUAL(0x31, cpu_test_read(fixture, 0x01FB));
	CPU_EXPECT_EQUAL(1, cpu_test_flag(cpu, CPU_STAT_I));

	CPU_EXPECT_EQUAL(6, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x8002, cpu->PC);
	CPU_EXPECT_EQUAL(0xFD, cpu->S);
	CPU_EXPECT_EQUAL(0x21, cpu->P);
}

static void cpu_test_subroutine_stack(CPU_TestFixture *fixture)
{
	cpu_test_name = "JSR and RTS stack behavior";
	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x8000, 0x20, 0x00, 0x81); // JSR $8100
	CPU_TEST_PROGRAM(fixture, 0x8100, 0x60);             // RTS
	NES_CPUState *cpu = cpu_test_state(fixture);

	CPU_EXPECT_EQUAL(6, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x8100, cpu->PC);
	CPU_EXPECT_EQUAL(0xFB, cpu->S);
	CPU_EXPECT_EQUAL(0x80, cpu_test_read(fixture, 0x01FD));
	CPU_EXPECT_EQUAL(0x02, cpu_test_read(fixture, 0x01FC));

	CPU_EXPECT_EQUAL(6, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x8003, cpu->PC);
	CPU_EXPECT_EQUAL(0xFD, cpu->S);
}

static void cpu_test_jmp_indirect_wrap(CPU_TestFixture *fixture)
{
	cpu_test_name = "JMP indirect page wrap";
	cpu_test_prepare(fixture);
	CPU_TEST_PROGRAM(fixture, 0x8000, 0x6C, 0xFF, 0x10); // JMP ($10FF)
	cpu_test_write(fixture, 0x10FF, 0x34);
	cpu_test_write(fixture, 0x1000, 0x12);
	CPU_EXPECT_EQUAL(5, cpu_test_step(fixture));
	CPU_EXPECT_EQUAL(0x1234, cpu_test_state(fixture)->PC);
}

int main(void)
{
	CPU_TestFixture fixture = cpu_test_fixture_create();
	cpu_test_explicit_bus_operations(&fixture);
	cpu_test_input_and_controllers(&fixture);
	cpu_test_reset_vector(&fixture);
	cpu_test_load_flags(&fixture);
	cpu_test_store_and_zero_page_wrap(&fixture);
	cpu_test_adc_sbc(&fixture);
	cpu_test_shifts_and_rotates(&fixture);
	cpu_test_branches(&fixture);
	cpu_test_indexed_addressing(&fixture);
	cpu_test_memory_read_modify_write(&fixture);
	cpu_test_subroutine_stack(&fixture);
	cpu_test_jmp_indirect_wrap(&fixture);
	cpu_test_brk_rti(&fixture);

	if (cpu_test_failures)
	{
		fprintf(stderr, "Orbiter CPU tests failed: %u failure(s)\n", cpu_test_failures);
		return 1;
	}

	printf("Orbiter CPU tests passed\n");
	return 0;
}
