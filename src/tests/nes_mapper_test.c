#include "base.h"
#include "nes/emulator.h"
#include "emulator_internal.h"
#include "bus/bus.h"
#include "mappers/mapper.h"
#include <stdio.h>

typedef struct
{
	Arena arena;
	NES_Emulator *core;
}
Mapper_TestFixture;

static u32 mapper_test_failures;
static const char *mapper_test_name;

static void mapper_expect_equal_(u64 expected, u64 actual,
	const char *expression, i32 line)
{
	if (expected != actual)
	{
		fprintf(stderr, "%s:%d: %s: expected 0x%llX, got 0x%llX\n",
			__FILE__, line, mapper_test_name, expected, actual);
		fprintf(stderr, "    %s\n", expression);
		++mapper_test_failures;
	}
}

#define MAPPER_EXPECT_EQUAL(expected, actual) \
	mapper_expect_equal_((u64)(expected), (u64)(actual), #actual, __LINE__)

static Mapper_TestFixture mapper_test_fixture_create(void)
{
	Mapper_TestFixture fixture = {};
	fixture.arena = arena_create(0, "Orbiter mapper tests");
	fixture.core = arena_push_zero(&fixture.arena, sizeof(NES_Emulator));
	Assert(fixture.core);
	return fixture;
}

static void mapper_test_prepare(Mapper_TestFixture *fixture,
	u32 prg_rom_size, u32 chr_rom_size)
{
	memory_zero(fixture->core, offsetof(NES_Emulator, mapper));
	fixture->core->prg_rom_size = prg_rom_size;
	fixture->core->chr_rom_size = chr_rom_size;
}

static NES_BusAccess mapper_access(NES_Emulator *core, NES_BusFunc bus,
	NES_BusAccessKind kind, u16 address, u8 value)
{
	return bus(core, (NES_BusAccess) {
		.kind = kind,
		.address = address,
		.value = value,
	});
}

static u8 mapper_read(NES_Emulator *core, NES_BusFunc bus, u16 address)
{
	return mapper_access(core, bus, NES_BUS_ACCESS_READ, address, 0).value;
}

static u8 mapper_peek(NES_Emulator *core, NES_BusFunc bus, u16 address)
{
	return mapper_access(core, bus, NES_BUS_ACCESS_PEEK, address, 0).value;
}

static void mapper_write(NES_Emulator *core, NES_BusFunc bus,
	u16 address, u8 value)
{
	mapper_access(core, bus, NES_BUS_ACCESS_WRITE, address, value);
}

static NES_MapAddr mapper_map(NES_Emulator *core, NES_BusFunc bus,
	u16 address)
{
	return mapper_access(core, bus, NES_BUS_ACCESS_MAP, address, 0).mapped;
}

static void mapper_mark_prg_banks(NES_Emulator *core, u32 bank_size)
{
	for (u32 bank = 0; bank * bank_size < core->prg_rom_size; ++bank)
	{
		core->prg_rom[bank * bank_size] = (u8)(0x40 + bank);
	}
}

static void mapper_mark_chr_banks(NES_Emulator *core, u32 bank_size)
{
	for (u32 bank = 0; bank * bank_size < core->chr_rom_size; ++bank) {
		core->chr_rom[bank * bank_size] = (u8)(0x60 + bank);
	}
}

static void mapper_test_nrom(Mapper_TestFixture *fixture)
{
	mapper_test_name = "NROM banking and mirroring";
	mapper_test_prepare(fixture, KiB(16), KiB(8));
	NES_Emulator *core = fixture->core;
	mapper_mark_prg_banks(core, KiB(16));
	MAPPER_EXPECT_EQUAL(true, nrom_valid(core));

	MAPPER_EXPECT_EQUAL(0x40, mapper_read(core, nrom_cpu, 0x8000));
	MAPPER_EXPECT_EQUAL(0x40, mapper_read(core, nrom_cpu, 0xC000));
	MAPPER_EXPECT_EQUAL(NES_DEVICE_PRG_ROM, mapper_map(core, nrom_cpu, 0xC123).device);
	MAPPER_EXPECT_EQUAL(0x0123, mapper_map(core, nrom_cpu, 0xC123).offset);
	mapper_write(core, nrom_cpu, 0x6000, 0xA5);
	mapper_write(core, nrom_cpu, 0x7FFF, 0x5A);
	MAPPER_EXPECT_EQUAL(0xA5, mapper_read(core, nrom_cpu, 0x6000));
	MAPPER_EXPECT_EQUAL(0x5A, mapper_peek(core, nrom_cpu, 0x7FFF));
	MAPPER_EXPECT_EQUAL(NES_DEVICE_PRG_RAM, mapper_map(core, nrom_cpu, 0x6123).device);
	MAPPER_EXPECT_EQUAL(0x0123, mapper_map(core, nrom_cpu, 0x6123).offset);

	mapper_test_prepare(fixture, KiB(32), KiB(8));
	mapper_mark_prg_banks(core, KiB(16));
	MAPPER_EXPECT_EQUAL(0x40, mapper_read(core, nrom_cpu, 0x8000));
	MAPPER_EXPECT_EQUAL(0x41, mapper_read(core, nrom_cpu, 0xC000));
	MAPPER_EXPECT_EQUAL(0x4000, mapper_map(core, nrom_cpu, 0xC000).offset);
	MAPPER_EXPECT_EQUAL(NES_DEVICE_CHR_ROM, mapper_map(core, nrom_ppu, 0x1ABC).device);
	MAPPER_EXPECT_EQUAL(0x1ABC, mapper_map(core, nrom_ppu, 0x1ABC).offset);
	core->prg_rom[0x0123] = 0xA5;
	core->chr_rom[0x1ABC] = 0x5A;
	mapper_write(core, nrom_cpu, 0x8123, 0x11);
	mapper_write(core, nrom_ppu, 0x1ABC, 0x22);
	MAPPER_EXPECT_EQUAL(0xA5, core->prg_rom[0x0123]);
	MAPPER_EXPECT_EQUAL(0x5A, core->chr_rom[0x1ABC]);

	core->vmirror = 1;
	MAPPER_EXPECT_EQUAL(0x000, mapper_map(core, nrom_ppu, 0x2000).offset);
	MAPPER_EXPECT_EQUAL(0x400, mapper_map(core, nrom_ppu, 0x2400).offset);
	MAPPER_EXPECT_EQUAL(0x000, mapper_map(core, nrom_ppu, 0x2800).offset);
	core->vmirror = 0;
	MAPPER_EXPECT_EQUAL(0x000, mapper_map(core, nrom_ppu, 0x2000).offset);
	MAPPER_EXPECT_EQUAL(0x000, mapper_map(core, nrom_ppu, 0x2400).offset);
	MAPPER_EXPECT_EQUAL(0x400, mapper_map(core, nrom_ppu, 0x2800).offset);
}

static void mapper_test_uxrom(Mapper_TestFixture *fixture)
{
	mapper_test_name = "UxROM switching and CHR RAM";
	mapper_test_prepare(fixture, KiB(64), 0);
	NES_Emulator *core = fixture->core;
	mapper_mark_prg_banks(core, KiB(16));
	MAPPER_EXPECT_EQUAL(true, uxrom_valid(core));

	mapper_write(core, uxrom_cpu, 0x8000, 1);
	MAPPER_EXPECT_EQUAL(0x41, mapper_read(core, uxrom_cpu, 0x8000));
	MAPPER_EXPECT_EQUAL(0x43, mapper_read(core, uxrom_cpu, 0xC000));
	MAPPER_EXPECT_EQUAL(KiB(16), mapper_map(core, uxrom_cpu, 0x8000).offset);

	u8 selected_bank = core->values[0];
	mapper_peek(core, uxrom_cpu, 0x8000);
	mapper_map(core, uxrom_cpu, 0x8000);
	MAPPER_EXPECT_EQUAL(selected_bank, core->values[0]);

	mapper_write(core, uxrom_cpu, 0x8000, 2);
	MAPPER_EXPECT_EQUAL(0x42, mapper_read(core, uxrom_cpu, 0x8000));
	mapper_write(core, uxrom_ppu, 0x1234, 0xA5);
	MAPPER_EXPECT_EQUAL(0xA5, mapper_read(core, uxrom_ppu, 0x1234));
	MAPPER_EXPECT_EQUAL(NES_DEVICE_CHR_RAM, mapper_map(core, uxrom_ppu, 0x1234).device);
	MAPPER_EXPECT_EQUAL(0x1234, mapper_map(core, uxrom_ppu, 0x1234).offset);
}

enum
{
	TEST_MMC1_CONTROL,
	TEST_MMC1_CHR0,
	TEST_MMC1_CHR1,
	TEST_MMC1_PRG,
	TEST_MMC1_SHIFT,
};

static void mapper_mmc1_serial_write(NES_Emulator *core,
	u16 address, u8 value)
{
	for (u32 bit = 0; bit < 5; ++bit)
	{
		mapper_write(core, mmc1_cpu, address, value >> bit & 1);
	}
}

static void mapper_test_mmc1(Mapper_TestFixture *fixture)
{
	mapper_test_name = "MMC1 PRG/CHR banking and mirroring";
	mapper_test_prepare(fixture, KiB(64), KiB(32));
	NES_Emulator *core = fixture->core;
	mapper_mark_prg_banks(core, KiB(16));
	mapper_mark_chr_banks(core, KiB(4));
	core->values[TEST_MMC1_CONTROL] = 0x0C;
	core->values[TEST_MMC1_SHIFT] = 0x10;
	MAPPER_EXPECT_EQUAL(true, mmc1_valid(core));

	mapper_mmc1_serial_write(core, 0xE000, 2);
	MAPPER_EXPECT_EQUAL(2, core->values[TEST_MMC1_PRG]);
	MAPPER_EXPECT_EQUAL(0x42, mapper_read(core, mmc1_cpu, 0x8000));
	MAPPER_EXPECT_EQUAL(0x43, mapper_read(core, mmc1_cpu, 0xC000));
	MAPPER_EXPECT_EQUAL(KiB(32), mapper_map(core, mmc1_cpu, 0x8000).offset);

	u8 shift = core->values[TEST_MMC1_SHIFT];
	mapper_peek(core, mmc1_cpu, 0xE000);
	mapper_map(core, mmc1_cpu, 0xE000);
	MAPPER_EXPECT_EQUAL(shift, core->values[TEST_MMC1_SHIFT]);

	core->values[TEST_MMC1_CONTROL] = 0x08;
	MAPPER_EXPECT_EQUAL(0x40, mapper_read(core, mmc1_cpu, 0x8000));
	MAPPER_EXPECT_EQUAL(0x42, mapper_read(core, mmc1_cpu, 0xC000));
	core->values[TEST_MMC1_CONTROL] = 0x00;
	core->values[TEST_MMC1_PRG] = 2;
	MAPPER_EXPECT_EQUAL(0x42, mapper_read(core, mmc1_cpu, 0x8000));
	MAPPER_EXPECT_EQUAL(0x43, mapper_read(core, mmc1_cpu, 0xC000));

	core->values[TEST_MMC1_CONTROL] = 0x0C;
	core->values[TEST_MMC1_CHR0] = 3;
	MAPPER_EXPECT_EQUAL(NES_DEVICE_CHR_ROM, mapper_map(core, mmc1_ppu, 0x0000).device);
	MAPPER_EXPECT_EQUAL(KiB(8), mapper_map(core, mmc1_ppu, 0x0000).offset);
	MAPPER_EXPECT_EQUAL(KiB(12), mapper_map(core, mmc1_ppu, 0x1000).offset);
	MAPPER_EXPECT_EQUAL(0x62, mapper_read(core, mmc1_ppu, 0x0000));
	MAPPER_EXPECT_EQUAL(0x63, mapper_read(core, mmc1_ppu, 0x1000));

	core->values[TEST_MMC1_CONTROL] = 0x1C;
	core->values[TEST_MMC1_CHR0] = 1;
	core->values[TEST_MMC1_CHR1] = 3;
	MAPPER_EXPECT_EQUAL(KiB(4), mapper_map(core, mmc1_ppu, 0x0000).offset);
	MAPPER_EXPECT_EQUAL(KiB(12), mapper_map(core, mmc1_ppu, 0x1000).offset);
	MAPPER_EXPECT_EQUAL(0x61, mapper_read(core, mmc1_ppu, 0x0000));
	MAPPER_EXPECT_EQUAL(0x63, mapper_read(core, mmc1_ppu, 0x1000));

	core->values[TEST_MMC1_CONTROL] = 0;
	MAPPER_EXPECT_EQUAL(0x000, mapper_map(core, mmc1_ppu, 0x2000).offset);
	MAPPER_EXPECT_EQUAL(0x000, mapper_map(core, mmc1_ppu, 0x2C00).offset);
	core->values[TEST_MMC1_CONTROL] = 1;
	MAPPER_EXPECT_EQUAL(0x400, mapper_map(core, mmc1_ppu, 0x2000).offset);
	MAPPER_EXPECT_EQUAL(0x400, mapper_map(core, mmc1_ppu, 0x2800).offset);
	core->values[TEST_MMC1_CONTROL] = 2;
	MAPPER_EXPECT_EQUAL(0x000, mapper_map(core, mmc1_ppu, 0x2000).offset);
	MAPPER_EXPECT_EQUAL(0x000, mapper_map(core, mmc1_ppu, 0x2800).offset);
	core->values[TEST_MMC1_CONTROL] = 3;
	MAPPER_EXPECT_EQUAL(0x000, mapper_map(core, mmc1_ppu, 0x2000).offset);
	MAPPER_EXPECT_EQUAL(0x000, mapper_map(core, mmc1_ppu, 0x2400).offset);

	memory_zero(core->values, sizeof(core->values));
	mmc1_reset(core);
	MAPPER_EXPECT_EQUAL(0x10, core->values[TEST_MMC1_SHIFT]);
	MAPPER_EXPECT_EQUAL(0x0C, core->values[TEST_MMC1_CONTROL]);

	core->values[TEST_MMC1_CONTROL] = 0x13;
	mapper_write(core, mmc1_cpu, 0x8000, 0x80);
	MAPPER_EXPECT_EQUAL(0x1F, core->values[TEST_MMC1_CONTROL]);

	mapper_test_prepare(fixture, KiB(32), 0);
	mmc1_reset(core);
	mapper_write(core, mmc1_ppu, 0x1234, 0xA5);
	MAPPER_EXPECT_EQUAL(NES_DEVICE_CHR_RAM, mapper_map(core, mmc1_ppu, 0x1234).device);
	MAPPER_EXPECT_EQUAL(0xA5, mapper_read(core, mmc1_ppu, 0x1234));
}

static void mapper_test_mmc2(Mapper_TestFixture *fixture)
{
	mapper_test_name = "MMC2 PRG banking and latches";
	mapper_test_prepare(fixture, KiB(128), KiB(128));
	NES_Emulator *core = fixture->core;
	mapper_mark_prg_banks(core, KiB(8));
	mmc2_reset(core);

	mapper_write(core, mmc2_cpu, 0xA000, 3);
	MAPPER_EXPECT_EQUAL(0x43, mapper_read(core, mmc2_cpu, 0x8000));
	MAPPER_EXPECT_EQUAL(3 * KiB(8), mapper_map(core, mmc2_cpu, 0x8000).offset);
	MAPPER_EXPECT_EQUAL(KiB(104), mapper_map(core, mmc2_cpu, 0xA000).offset);
	MAPPER_EXPECT_EQUAL(KiB(120), mapper_map(core, mmc2_cpu, 0xE000).offset);
	MAPPER_EXPECT_EQUAL(KiB(112), mapper_map(core, mmc2_cpu, 0xC000).offset);

	mapper_peek(core, mmc2_cpu, 0xA000);
	mapper_map(core, mmc2_cpu, 0xA000);
	MAPPER_EXPECT_EQUAL(3 * KiB(8), mapper_map(core, mmc2_cpu, 0x8000).offset);

	mapper_write(core, mmc2_cpu, 0xB000, 2);
	mapper_write(core, mmc2_cpu, 0xC000, 3);
	mapper_write(core, mmc2_cpu, 0xD000, 4);
	mapper_write(core, mmc2_cpu, 0xE000, 5);
	MAPPER_EXPECT_EQUAL(3 * KiB(4), mapper_map(core, mmc2_ppu, 0x0000).offset);
	MAPPER_EXPECT_EQUAL(5 * KiB(4), mapper_map(core, mmc2_ppu, 0x1000).offset);

	mapper_read(core, mmc2_ppu, 0x0FD8);
	MAPPER_EXPECT_EQUAL(2 * KiB(4), mapper_map(core, mmc2_ppu, 0x0000).offset);
	mapper_peek(core, mmc2_ppu, 0x0FE8);
	mapper_map(core, mmc2_ppu, 0x0FE8);
	MAPPER_EXPECT_EQUAL(2 * KiB(4), mapper_map(core, mmc2_ppu, 0x0000).offset);
	mapper_read(core, mmc2_ppu, 0x0FE8);
	MAPPER_EXPECT_EQUAL(3 * KiB(4), mapper_map(core, mmc2_ppu, 0x0000).offset);
	mapper_read(core, mmc2_ppu, 0x1FD8);
	MAPPER_EXPECT_EQUAL(4 * KiB(4), mapper_map(core, mmc2_ppu, 0x1000).offset);
	mapper_read(core, mmc2_ppu, 0x1FE8);
	MAPPER_EXPECT_EQUAL(5 * KiB(4), mapper_map(core, mmc2_ppu, 0x1000).offset);

	mapper_write(core, mmc2_cpu, 0xF000, 0);
	MAPPER_EXPECT_EQUAL(0x000, mapper_map(core, mmc2_ppu, 0x2000).offset);
	MAPPER_EXPECT_EQUAL(0x400, mapper_map(core, mmc2_ppu, 0x2400).offset);
	MAPPER_EXPECT_EQUAL(0x000, mapper_map(core, mmc2_ppu, 0x2800).offset);
	MAPPER_EXPECT_EQUAL(0x400, mapper_map(core, mmc2_ppu, 0x2C00).offset);

	mapper_write(core, mmc2_cpu, 0xF000, 1);
	MAPPER_EXPECT_EQUAL(0x000, mapper_map(core, mmc2_ppu, 0x2000).offset);
	MAPPER_EXPECT_EQUAL(0x000, mapper_map(core, mmc2_ppu, 0x2400).offset);
	MAPPER_EXPECT_EQUAL(0x400, mapper_map(core, mmc2_ppu, 0x2800).offset);
	MAPPER_EXPECT_EQUAL(0x400, mapper_map(core, mmc2_ppu, 0x2C00).offset);
	MAPPER_EXPECT_EQUAL(true, mmc2_valid(core));
}

int main(void)
{
	Mapper_TestFixture fixture = mapper_test_fixture_create();
	mapper_test_nrom(&fixture);
	mapper_test_uxrom(&fixture);
	mapper_test_mmc1(&fixture);
	mapper_test_mmc2(&fixture);

	if (mapper_test_failures)
	{
		fprintf(stderr, "Orbiter mapper tests failed: %u failure(s)\n", mapper_test_failures);
		return 1;
	}

	printf("Orbiter mapper tests passed\n");
	return 0;
}
