#include "base.h"
#include "nes/emulator.h"
#include "emulator_internal.h"
#include "bus/bus.h"
#include "ppu/ppu.h"
#include <stdio.h>

typedef struct
{
	Arena arena;
	NES_Emulator *core;
}
PPU_TestFixture;

static u32 ppu_test_failures;
static const char *ppu_test_name;

static void ppu_expect_equal_(u64 expected, u64 actual, const char *expression, i32 line)
{
	if (expected != actual)
	{
		fprintf(stderr, "%s:%d: %s: expected 0x%llX, got 0x%llX\n",
			__FILE__, line, ppu_test_name, expected, actual);
		fprintf(stderr, "    %s\n", expression);
		++ppu_test_failures;
	}
}

#define PPU_EXPECT_EQUAL(expected, actual) \
	ppu_expect_equal_((u64)(expected), (u64)(actual), #actual, __LINE__)

static PPU_TestFixture ppu_test_fixture_create(void)
{
	PPU_TestFixture fixture = {};
	fixture.arena = arena_create(0, "Orbiter PPU tests");

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

static void ppu_test_prepare(PPU_TestFixture *fixture)
{
	nes_ppu_power_on(&fixture->core->ppu);
	memory_zero(fixture->core->_wram, sizeof(fixture->core->_wram));
	memory_zero(fixture->core->_vram, sizeof(fixture->core->_vram));
	fixture->core->cpu_stall_cycles = 0;
}

static void ppu_test_power_and_reset(PPU_TestFixture *fixture)
{
	ppu_test_name = "PPU power and reset state";
	ppu_test_prepare(fixture);
	NES_PPUState *ppu = &fixture->core->ppu;
	ppu->dot = 123;
	ppu->scanline = 45;
	ppu->t = 0x3456;
	ppu->v = 0x2345;
	ppu->x = 7;
	ppu->w = 1;
	ppu->tile_id = 0x11;
	ppu->chr_r0 = 0x2222;
	ppu->spr0_enable = 1;
	ppu->PPUCTRL = 0xFF;
	ppu->PPUMASK = 0xFF;
	ppu->PPUSTATUS = 0xE0;
	ppu->OAMADDR = 0x44;
	ppu->data_read_buf = 0x55;
	ppu->nsprs = 8;
	ppu->_oam[9] = 0x66;
	ppu->_pram[7] = 0x77;

	nes_ppu_reset(ppu);
	PPU_EXPECT_EQUAL(0, ppu->dot);
	PPU_EXPECT_EQUAL(0, ppu->scanline);
	PPU_EXPECT_EQUAL(0, ppu->t);
	PPU_EXPECT_EQUAL(0x2345, ppu->v);
	PPU_EXPECT_EQUAL(0, ppu->x);
	PPU_EXPECT_EQUAL(0, ppu->w);
	PPU_EXPECT_EQUAL(0, ppu->tile_id);
	PPU_EXPECT_EQUAL(0, ppu->chr_r0);
	PPU_EXPECT_EQUAL(0, ppu->spr0_enable);
	PPU_EXPECT_EQUAL(0, ppu->PPUCTRL);
	PPU_EXPECT_EQUAL(0, ppu->PPUMASK);
	PPU_EXPECT_EQUAL(0xE0, ppu->PPUSTATUS);
	PPU_EXPECT_EQUAL(0x44, ppu->OAMADDR);
	PPU_EXPECT_EQUAL(0, ppu->data_read_buf);
	PPU_EXPECT_EQUAL(0, ppu->nsprs);
	PPU_EXPECT_EQUAL(0x66, ppu->_oam[9]);
	PPU_EXPECT_EQUAL(0x77, ppu->_pram[7]);

	nes_ppu_power_on(ppu);
	PPU_EXPECT_EQUAL(0, ppu->v);
	PPU_EXPECT_EQUAL(0, ppu->PPUSTATUS);
	PPU_EXPECT_EQUAL(0, ppu->OAMADDR);
	PPU_EXPECT_EQUAL(0, ppu->_oam[9]);
	PPU_EXPECT_EQUAL(0, ppu->_pram[7]);
}

static void ppu_cpu_write(PPU_TestFixture *fixture, u16 address, u8 value)
{
	nes_cpu_bus_write(fixture->core, address, value);
}

static u8 ppu_cpu_read(PPU_TestFixture *fixture, u16 address)
{
	return nes_cpu_bus_read(fixture->core, address);
}

static void ppu_bus_write(PPU_TestFixture *fixture, u16 address, u8 value)
{
	nes_ppu_bus_write(fixture->core, address, value);
}

static u8 ppu_bus_read(PPU_TestFixture *fixture, u16 address)
{
	return nes_ppu_bus_peek(fixture->core, address);
}

static void ppu_test_explicit_bus_operations(PPU_TestFixture *fixture)
{
	ppu_test_name = "explicit PPU bus operations";
	ppu_test_prepare(fixture);

	NES_MapAddr pattern = nes_ppu_bus_map(fixture->core, 0x0012);
	PPU_EXPECT_EQUAL(NES_DEVICE_CHR_ROM, pattern.device);
	PPU_EXPECT_EQUAL(0x0012, pattern.offset);

	NES_MapAddr palette = nes_ppu_bus_map(fixture->core, 0x3F10);
	PPU_EXPECT_EQUAL(NES_DEVICE_PRAM, palette.device);
	PPU_EXPECT_EQUAL(0, palette.offset);

	ppu_bus_write(fixture, 0x3F10, 0x2A);
	PPU_EXPECT_EQUAL(0x2A, ppu_bus_read(fixture, 0x3F00));
}

static void ppu_set_vram_address(PPU_TestFixture *fixture, u16 address)
{
	ppu_cpu_write(fixture, 0x2006, (u8)(address >> 8));
	ppu_cpu_write(fixture, 0x2006, (u8)address);
}

static void ppu_test_scroll_and_address_latch(PPU_TestFixture *fixture)
{
	ppu_test_name = "scroll and address latch";
	ppu_test_prepare(fixture);
	NES_PPUState *ppu = &fixture->core->ppu;

	ppu_cpu_write(fixture, 0x2000, 0x03);
	PPU_EXPECT_EQUAL(0x03, ppu->PPUCTRL);
	PPU_EXPECT_EQUAL(0x0C00, ppu->t & 0x0C00);

	ppu_cpu_write(fixture, 0x2005, 0x2D);
	PPU_EXPECT_EQUAL(5, ppu->t & 0x001F);
	PPU_EXPECT_EQUAL(5, ppu->x);
	PPU_EXPECT_EQUAL(1, ppu->w);

	ppu_cpu_write(fixture, 0x2005, 0x53);
	PPU_EXPECT_EQUAL(10, (ppu->t >> 5) & 0x1F);
	PPU_EXPECT_EQUAL(3, (ppu->t >> 12) & 7);
	PPU_EXPECT_EQUAL(0, ppu->w);

	ppu_set_vram_address(fixture, 0x2345);
	PPU_EXPECT_EQUAL(0x2345, ppu->t);
	PPU_EXPECT_EQUAL(0x2345, ppu->v);
	PPU_EXPECT_EQUAL(0, ppu->w);

	ppu->PPUSTATUS = 0xE0;
	ppu->w = 1;
	PPU_EXPECT_EQUAL(0xE0, ppu_cpu_read(fixture, 0x2002));
	PPU_EXPECT_EQUAL(0x60, ppu->PPUSTATUS);
	PPU_EXPECT_EQUAL(0, ppu->w);
}

static void ppu_test_data_port_address_increment(PPU_TestFixture *fixture)
{
	ppu_test_name = "PPUDATA uses and increments v";
	ppu_test_prepare(fixture);
	NES_PPUState *ppu = &fixture->core->ppu;

	ppu_cpu_write(fixture, 0x2000, 0x00);
	ppu_set_vram_address(fixture, 0x2000);
	ppu_cpu_write(fixture, 0x2007, 0xA5);
	PPU_EXPECT_EQUAL(0xA5, ppu_bus_read(fixture, 0x2000));
	PPU_EXPECT_EQUAL(0x2001, ppu->v);

	ppu_cpu_write(fixture, 0x2000, 0x04);
	ppu_set_vram_address(fixture, 0x2100);
	ppu_cpu_write(fixture, 0x2007, 0x5A);
	PPU_EXPECT_EQUAL(0x5A, ppu_bus_read(fixture, 0x2100));
	PPU_EXPECT_EQUAL(0x2120, ppu->v);

	ppu_cpu_write(fixture, 0x2000, 0x00);
	ppu_set_vram_address(fixture, 0x3FFF);
	ppu_cpu_write(fixture, 0x2007, 0x2A);
	// v is 15 bits; only its low 14 bits reach the external PPU bus.
	PPU_EXPECT_EQUAL(0x4000, ppu->v);
}

static void ppu_test_data_read_buffer(PPU_TestFixture *fixture)
{
	ppu_test_name = "PPUDATA delayed read buffer";
	ppu_test_prepare(fixture);
	NES_PPUState *ppu = &fixture->core->ppu;

	ppu_bus_write(fixture, 0x2000, 0x11);
	ppu_bus_write(fixture, 0x2001, 0x22);
	ppu->data_read_buf = 0x7E;
	ppu_set_vram_address(fixture, 0x2000);

	PPU_EXPECT_EQUAL(0x7E, ppu_cpu_read(fixture, 0x2007));
	PPU_EXPECT_EQUAL(0x11, ppu->data_read_buf);
	PPU_EXPECT_EQUAL(0x2001, ppu->v);
	PPU_EXPECT_EQUAL(0x11, ppu_cpu_read(fixture, 0x2007));
	PPU_EXPECT_EQUAL(0x22, ppu->data_read_buf);
	PPU_EXPECT_EQUAL(0x2002, ppu->v);

	ppu->data_read_buf = 0x4C;
	ppu_set_vram_address(fixture, 0x2010);
	ppu_cpu_write(fixture, 0x2007, 0x33);
	PPU_EXPECT_EQUAL(0x4C, ppu->data_read_buf);

	ppu_set_vram_address(fixture, 0x3F10);
	ppu_cpu_write(fixture, 0x2007, 0x2B);
	PPU_EXPECT_EQUAL(0x4C, ppu->data_read_buf);
}

static void ppu_test_palette_read_buffer(PPU_TestFixture *fixture)
{
	ppu_test_name = "palette reads bypass and refill the PPUDATA buffer";
	ppu_test_prepare(fixture);
	NES_PPUState *ppu = &fixture->core->ppu;

	ppu_bus_write(fixture, 0x2F05, 0x36);
	ppu_bus_write(fixture, 0x3F05, 0x25);
	ppu->data_read_buf = 0x7E;
	ppu_set_vram_address(fixture, 0x3F05);

	// Palette reads are immediate, but the internal buffer is refilled from
	// the nametable address underneath the palette range ($3F05 - $1000).
	PPU_EXPECT_EQUAL(0x25, ppu_cpu_read(fixture, 0x2007));
	PPU_EXPECT_EQUAL(0x36, ppu->data_read_buf);
}

static void ppu_test_palette_mirroring(PPU_TestFixture *fixture)
{
	ppu_test_name = "palette RAM mirroring";
	ppu_test_prepare(fixture);

	ppu_bus_write(fixture, 0x7F10, 0x2A);
	PPU_EXPECT_EQUAL(0x2A, ppu_bus_read(fixture, 0x3F00));
	ppu_bus_write(fixture, 0x3F00, 0x17);
	PPU_EXPECT_EQUAL(0x17, ppu_bus_read(fixture, 0x3F10));

	ppu_bus_write(fixture, 0x3F14, 0x31);
	PPU_EXPECT_EQUAL(0x31, ppu_bus_read(fixture, 0x3F04));
	ppu_bus_write(fixture, 0x3F20, 0x0D);
	PPU_EXPECT_EQUAL(0x0D, ppu_bus_read(fixture, 0x3F00));
}

static void ppu_test_oam_data_port(PPU_TestFixture *fixture)
{
	ppu_test_name = "OAMDATA read, write, increment, and wrapping";
	ppu_test_prepare(fixture);
	NES_PPUState *ppu = &fixture->core->ppu;

	ppu_cpu_write(fixture, 0x2003, 0x10);
	ppu_cpu_write(fixture, 0x2004, 0xAB);
	PPU_EXPECT_EQUAL(0xAB, ppu->_oam[0x10]);
	PPU_EXPECT_EQUAL(0x11, ppu->OAMADDR);

	ppu_cpu_write(fixture, 0x2003, 0x10);
	PPU_EXPECT_EQUAL(0xAB, ppu_cpu_read(fixture, 0x2004));
	PPU_EXPECT_EQUAL(0x10, ppu->OAMADDR);

	// OAM attribute bytes physically expose only E3; bits 2-4 read as zero.
	ppu->_oam[2] = 0xFF;
	ppu_cpu_write(fixture, 0x2003, 2);
	PPU_EXPECT_EQUAL(0xE3, ppu_cpu_read(fixture, 0x2004));

	ppu_cpu_write(fixture, 0x2003, 0xFF);
	ppu_cpu_write(fixture, 0x2004, 0x5C);
	PPU_EXPECT_EQUAL(0x5C, ppu->_oam[0xFF]);
	PPU_EXPECT_EQUAL(0x00, ppu->OAMADDR);
}

static void ppu_test_oam_dma(PPU_TestFixture *fixture)
{
	ppu_test_name = "OAM DMA begins at OAMADDR and wraps";
	ppu_test_prepare(fixture);
	NES_PPUState *ppu = &fixture->core->ppu;

	for (u32 i = 0; i < 256; ++i)
	{
		fixture->core->_wram[0x200 + i] = (u8)i;
	}

	ppu_cpu_write(fixture, 0x2003, 0xF8);
	ppu_cpu_write(fixture, 0x4014, 0x02);

	PPU_EXPECT_EQUAL(0x00, ppu->_oam[0xF8]);
	PPU_EXPECT_EQUAL(0x07, ppu->_oam[0xFF]);
	PPU_EXPECT_EQUAL(0x08, ppu->_oam[0x00]);
	PPU_EXPECT_EQUAL(0xFF, ppu->_oam[0xF7]);
	PPU_EXPECT_EQUAL(0xF8, ppu->OAMADDR);
	PPU_EXPECT_EQUAL(513, fixture->core->cpu_stall_cycles);
}

static void ppu_step_through_sprite_evaluation(PPU_TestFixture *fixture, u32 last_dot)
{
	NES_PPUState *ppu = &fixture->core->ppu;
	for (u32 dot = 1; dot <= last_dot; ++dot)
	{
		PPU_EXPECT_EQUAL(dot, ppu->dot);
		nes_ppu_step(fixture->core);
	}
}

static void ppu_test_sprite_evaluation(PPU_TestFixture *fixture)
{
	ppu_test_name = "dot-stepped sprite evaluation";
	ppu_test_prepare(fixture);
	NES_PPUState *ppu = &fixture->core->ppu;

	ppu->PPUMASK = 0x08;
	ppu->dot = 1;
	ppu->scanline = 20;
	memory_zero(ppu->soam, sizeof(ppu->soam));
	ppu_step_through_sprite_evaluation(fixture, 64);
	for (u32 index = 0; index < sizeof(ppu->soam); ++index) PPU_EXPECT_EQUAL(0xFF, ppu->soam[index]);

	ppu_test_prepare(fixture);
	ppu->PPUMASK = 0x08;
	ppu->dot = 1;
	ppu->scanline = 20;
	for (u32 index = 0; index < ArrayCount(ppu->OAM); ++index) ppu->OAM[index].ypos = 0x80;
	ppu->OAM[0] = (NES_PPUSprite) { .ypos = 20, .index = 0x11, .attrs = 0x22, .xpos = 0x33 };
	ppu->OAM[2] = (NES_PPUSprite) { .ypos = 14, .index = 0x44, .attrs = 0x55, .xpos = 0x66 };
	ppu_step_through_sprite_evaluation(fixture, 256);
	PPU_EXPECT_EQUAL(8, ppu->soam_index);
	PPU_EXPECT_EQUAL(20, ppu->SOAM[0].ypos);
	PPU_EXPECT_EQUAL(0x11, ppu->SOAM[0].index);
	PPU_EXPECT_EQUAL(0x22, ppu->SOAM[0].attrs);
	PPU_EXPECT_EQUAL(0x33, ppu->SOAM[0].xpos);
	PPU_EXPECT_EQUAL(14, ppu->SOAM[1].ypos);
	PPU_EXPECT_EQUAL(0x44, ppu->SOAM[1].index);
	PPU_EXPECT_EQUAL(0x55, ppu->SOAM[1].attrs);
	PPU_EXPECT_EQUAL(0x66, ppu->SOAM[1].xpos);

	ppu_test_prepare(fixture);
	ppu->PPUMASK = 0x08;
	ppu->dot = 1;
	ppu->scanline = 20;
	for (u32 index = 0; index < ArrayCount(ppu->OAM); ++index) ppu->OAM[index].ypos = 0x80;
	for (u32 index = 0; index < 9; ++index) ppu->OAM[index].ypos = 20;
	ppu_step_through_sprite_evaluation(fixture, 256);
	PPU_EXPECT_EQUAL(32, ppu->soam_index);
	PPU_EXPECT_EQUAL(0x20, ppu->PPUSTATUS & 0x20);
}

static void ppu_test_forced_blank_preserves_vram_address(PPU_TestFixture *fixture)
{
	ppu_test_name = "forced blank preserves the CPU VRAM address";
	ppu_test_prepare(fixture);
	NES_PPUState *ppu = &fixture->core->ppu;

	ppu->PPUMASK = 0;
	ppu->v = 0x2345;
	ppu->t = 0x1ABC;
	ppu->dot = 256;
	ppu->scanline = 0;
	nes_ppu_step(fixture->core);
	PPU_EXPECT_EQUAL(0x2345, ppu->v);

	ppu->dot = 257;
	ppu->v = 0x2345;
	nes_ppu_step(fixture->core);
	PPU_EXPECT_EQUAL(0x2345, ppu->v);

	ppu->dot = 304;
	ppu->scanline = 261;
	ppu->v = 0x2345;
	nes_ppu_step(fixture->core);
	PPU_EXPECT_EQUAL(0x2345, ppu->v);

	ppu->PPUMASK = 0x08;
	ppu->v = 0x1200;
	ppu->dot = 8;
	ppu->scanline = 0;
	nes_ppu_step(fixture->core);
	PPU_EXPECT_EQUAL(0x1201, ppu->v);
}

static void ppu_test_clock_and_vblank_events(PPU_TestFixture *fixture)
{
	ppu_test_name = "PPU clock and vblank events";
	ppu_test_prepare(fixture);
	NES_PPUState *ppu = &fixture->core->ppu;

	ppu->dot = 340;
	ppu->scanline = 0;
	PPU_EXPECT_EQUAL(NES_PPU_EVENT_NONE, nes_ppu_step(fixture->core));
	PPU_EXPECT_EQUAL(0, ppu->dot);
	PPU_EXPECT_EQUAL(1, ppu->scanline);

	ppu->dot = 340;
	ppu->scanline = 261;
	PPU_EXPECT_EQUAL(NES_PPU_EVENT_NONE, nes_ppu_step(fixture->core));
	PPU_EXPECT_EQUAL(0, ppu->dot);
	PPU_EXPECT_EQUAL(0, ppu->scanline);

	ppu->PPUCTRL = 0;
	ppu->PPUSTATUS = 0;
	ppu->dot = 1;
	ppu->scanline = 241;
	PPU_EXPECT_EQUAL(NES_PPU_EVENT_FRAME, nes_ppu_step(fixture->core));
	PPU_EXPECT_EQUAL(0x80, ppu->PPUSTATUS & 0x80);

	ppu->PPUCTRL = 0x80;
	ppu->PPUSTATUS = 0;
	ppu->dot = 1;
	ppu->scanline = 241;
	PPU_EXPECT_EQUAL(NES_PPU_EVENT_FRAME | NES_PPU_EVENT_NMI, nes_ppu_step(fixture->core));
	PPU_EXPECT_EQUAL(0x80, ppu->PPUSTATUS & 0x80);

	ppu->PPUSTATUS = 0xE0;
	ppu->dot = 1;
	ppu->scanline = 261;
	nes_ppu_step(fixture->core);
	PPU_EXPECT_EQUAL(0, ppu->PPUSTATUS & 0xE0);
}

static void ppu_test_video_is_published_separately(PPU_TestFixture *fixture)
{
	ppu_test_name = "PPU video output boundary";
	ppu_test_prepare(fixture);
	NES_PPUState *ppu = &fixture->core->ppu;

	ppu->_pram[0] = 0x2A;
	ppu->PPUMASK = 0x10;
	ppu->dot = 1;
	ppu->scanline = 0;
	nes_ppu_step(fixture->core);

	PPU_EXPECT_EQUAL(0x2A, fixture->core->video[0][0]);
	PPU_EXPECT_EQUAL(NES_VIDEO_WIDTH, ArrayCount(fixture->core->video[0]));
}

int main(void)
{
	PPU_TestFixture fixture = ppu_test_fixture_create();
	ppu_test_power_and_reset(&fixture);
	ppu_test_explicit_bus_operations(&fixture);
	ppu_test_scroll_and_address_latch(&fixture);
	ppu_test_data_port_address_increment(&fixture);
	ppu_test_data_read_buffer(&fixture);
	ppu_test_palette_read_buffer(&fixture);
	ppu_test_palette_mirroring(&fixture);
	ppu_test_oam_data_port(&fixture);
	ppu_test_oam_dma(&fixture);
	ppu_test_sprite_evaluation(&fixture);
	ppu_test_forced_blank_preserves_vram_address(&fixture);
	ppu_test_clock_and_vblank_events(&fixture);
	ppu_test_video_is_published_separately(&fixture);

	if (ppu_test_failures)
	{
		fprintf(stderr, "Orbiter PPU tests failed: %u failure(s)\n", ppu_test_failures);
		return 1;
	}

	printf("Orbiter PPU tests passed\n");
	return 0;
}
