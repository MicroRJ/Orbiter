#include "ppu.h"
#include "../emulator_internal.h"

enum
{
	PPUCTRL_SPRITE_SIZE_8X16 = 0x20,
	PPUCTRL_NMI_ENABLED      = 0x80,
	PPUMASK_BACKGROUND       = 0x08,
	PPUMASK_SPRITES          = 0x10,
};

static inline b32 ppu_rendering_enabled(const NES_PPUState *ppu)
{
	return !!(ppu->PPUMASK & (PPUMASK_BACKGROUND | PPUMASK_SPRITES));
}

static inline u8 ppu_bus_read(NES_Emulator *core, u16 address)
{
	return nes_ppu_bus_read(core, address);
}

static inline void ppu_bus_write(NES_Emulator *core, u16 address, u8 value)
{
	nes_ppu_bus_write(core, address, value);
}

// https://www.nesdev.org/wiki/PPU_power_up_state
void nes_ppu_power_on(NES_PPUState *ppu)
{
	memory_zero(ppu, sizeof(*ppu));
	nes_ppu_reset(ppu);
}

void nes_ppu_reset(NES_PPUState *ppu)
{
	NES_PPUState previous = *ppu;
	memory_zero(ppu, sizeof(*ppu));
	ppu->v = previous.v;
	ppu->PPUSTATUS = previous.PPUSTATUS;
	ppu->OAMADDR = previous.OAMADDR;
	memory_copy(ppu->_oam, previous._oam, sizeof(ppu->_oam));
	memory_copy(ppu->_pram, previous._pram, sizeof(ppu->_pram));
}

NES_BusResult nes_ppu_register_access(NES_Emulator *core, NES_BusMode mode, u32 address, u8 value)
{
	Assert(address < 8);
	if (mode == NES_BUS_PEEK) return nes_bus_result(NES_DEVICE_PPU, address, value);

	Assert(mode == NES_BUS_READ || mode == NES_BUS_WRITE);
	NES_PPUState *ppu = &core->ppu;
	b32 write = mode == NES_BUS_WRITE;

	switch (address)
	{
		case 0: // PPUCTRL
		{
			if (write)
			{
				core->ppu.PPUCTRL = (u8)(value);
				core->ppu.t = (u16)((ppu->t & ~(3 << 10)) | (value & 3) << 10);
			}
		} break;

		case 1: // PPUMASK
		{
			if (write) core->ppu.PPUMASK = (u8)(value);
		} break;

		case 2: // PPUSTATUS
		{
			if (!write)
			{
				value = ppu->PPUSTATUS;
				core->ppu.PPUSTATUS = (u8)(ppu->PPUSTATUS & ~0x80);
				core->ppu.w = (u8)(0);
			}
		} break;

		case 3: // OAMADDR
		{
			if (write) core->ppu.OAMADDR = (u8)(value);
		} break;

		case 4: // OAMDATA
		{
			if (write)
			{
				// Outside rendering, OAMDATA writes store at OAMADDR and then
				// increment its 8-bit address. This case used to be empty, so
				// CPU-driven OAM writes were silently discarded.
				core->ppu._oam[ppu->OAMADDR] = (u8)(value);
				core->ppu.OAMADDR = (u8)(ppu->OAMADDR + 1);
			}
			else
			{
				// OAMDATA reads do not increment OAMADDR. Attribute bytes have
				// only bits E3 physically implemented, so bits 2-4 read as zero.
				value = ppu->_oam[ppu->OAMADDR & 0xFF];
				if ((ppu->OAMADDR & 3) == 2) value &= 0xE3;
			}
		} break;

		case 5: // PPUSCROLL
		{
			if (write)
			{
				if (!ppu->w)
				{
					core->ppu.t = (u16)((ppu->t & ~0x001F) | (value >> 3));
					core->ppu.x = (u8)(value & 7);
				}
				else
				{
					u32 t = (ppu->t & ~0x03E0) | ((value >> 3 & 31) << 5);
					core->ppu.t = (u16)((t & ~0x7000) | ((value & 7) << 12));
				}
				core->ppu.w = (u8)(ppu->w ^ 1);
			}
		} break;

		case 6: // PPUADDR
		{
			if (write)
			{
				if (!ppu->w)
				{
					core->ppu.t = (u16)((ppu->t & 0x00FF) | ((value & 0x3F) << 8));
				}
				else
				{
					core->ppu.t = (u16)((ppu->t & 0x7F00) | value);
					core->ppu.v = (u16)(ppu->t);
				}
				core->ppu.w = (u8)(ppu->w ^ 1);
			}
		} break;

		case 7: // PPUDATA
		{
			u16 bus_address = ppu->v & 0x3FFF;
			if (write)
			{
				ppu_bus_write(core, bus_address, value);
			}
			else if (bus_address >= 0x3F00)
			{
				// Palette data bypasses the delayed buffer. Simultaneously, the
				// external bus reads the nametable address under the palette.
				value = ppu_bus_read(core, bus_address);
				core->ppu.data_read_buf = (u8)(ppu_bus_read(core, bus_address - 0x1000));
			}
			else
			{
				value = ppu->data_read_buf;
				core->ppu.data_read_buf = (u8)(ppu_bus_read(core, bus_address));
			}

			// v is 15 bits even though only its low 14 bits reach the bus.
			core->ppu.v = (u16)((ppu->v + ((ppu->PPUCTRL & 4) ? 32 : 1)) & 0x7FFF);
		} break;
	}

	return nes_bus_result(NES_DEVICE_PPU, address, value);
}

static void ppu_shift_background(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->ppu;
	core->ppu.atr_r0 = (u8)(ppu->atr_l0 << 7 | ppu->atr_r0 >> 1);
	core->ppu.atr_r1 = (u8)(ppu->atr_l1 << 7 | ppu->atr_r1 >> 1);
	core->ppu.chr_r0 = (u16)(ppu->chr_r0 << 1);
	core->ppu.chr_r1 = (u16)(ppu->chr_r1 << 1);

	if (ppu->spr0_2cycle_delay)
	{
		core->ppu.spr0_2cycle_delay = (u8)(false);
		core->ppu.PPUSTATUS = (u8)(ppu->PPUSTATUS | 0x40);
	}
}

static inline u8 ppu_fetch_nametable_byte(NES_Emulator *nes, u16 v)
{
	return ppu_bus_read(nes, 0x2000 | v & 0x0FFF);
}

static inline u8 ppu_fetch_attribute_byte(NES_Emulator *nes, u16 v)
{
	u16 coarse_x = v >> 0 & 31;
	u16 coarse_y = v >> 5 & 31;
	u16 nametable = v & 0x0C00;
	u16 address = 0x23C0 | nametable | ((coarse_y >> 2) << 3) | (coarse_x >> 2);
	return ppu_bus_read(nes, address);
}

static u16 ppu_background_pattern_address(const NES_PPUState *ppu, u16 plane)
{
	u16 pattern_table = (ppu->PPUCTRL & 0x10) << 8;
	u16 fine_y = (ppu->v >> 12) & 7;
	return pattern_table | ((u16)ppu->tile_id << 4) | plane | fine_y;
}

static void ppu_fetch_pattern_low(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->ppu;
	core->ppu.tile_lo = (u8)(ppu_bus_read(core, ppu_background_pattern_address(ppu, 0)));
}

static void ppu_fetch_pattern_high_and_reload(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->ppu;
	core->ppu.tile_hi = (u8)(ppu_bus_read(core, ppu_background_pattern_address(ppu, 8)));
	core->ppu.chr_r0 = (u16)(ppu->chr_r0 | ppu->tile_lo);
	core->ppu.chr_r1 = (u16)(ppu->chr_r1 | ppu->tile_hi);

	u32 attribute_shift = (((ppu->v >> 5) & 2) << 1) | (ppu->v & 2);
	core->ppu.atr_l0 = (u8)((ppu->atr_b >> attribute_shift) & 1);
	core->ppu.atr_l1 = (u8)((ppu->atr_b >> (attribute_shift + 1)) & 1);
}

// """
// The coarse X component of v needs to be incremented when the next tile is reached.
// Bits 0-4 are incremented, with overflow toggling bit 10.
// This means that bits 0-4 count from 0 to 31 across a single nametable, and bit 10 selects the
// current nametable horizontally.
// """
static inline u16 ppu_increment_horizontal(u16 v)
{
	return ((v & 31) == 31) ? ((v & ~31) ^ 0x0400) : (v + 1);
}

// """
// If rendering is enabled, fine Y is incremented at dot 256 of each scanline, overflowing to coarse Y,
// and finally adjusted to wrap among the nametables vertically.
// Bits 12-14 are fine Y. Bits 5-9 are coarse Y. Bit 11 selects the vertical nametable.
// """
static inline u16 ppu_increment_vertical(u16 v)
{
	if ((v & 0x7000) != 0x7000) return v + 0x1000;
	u32 y = (v >> 5) & 31;
	if      (y == 29) return (v & ~0x73E0) ^ 0x0800;
	else if (y == 31) return (v & ~0x73E0);
	else              return (v & ~0x7000) + 0x0020;
}

static u16 ppu_sprite_pattern_address(const NES_PPUState *ppu, u8 attr, u16 tile, i32 row, u16 plane)
{
	b32 vertical_flip = !!(attr & 0x80);
	if (ppu->PPUCTRL & PPUCTRL_SPRITE_SIZE_8X16)
	{
		u16 pattern_table = (tile & 1) << 12;
		tile = (tile & 0xFE) | (((row >> 3) & 1) ^ vertical_flip);
		u16 tile_row = (row & 7) ^ (vertical_flip ? 7 : 0);
		return pattern_table | (tile << 4) | plane | tile_row;
	}

	u16 pattern_table = (ppu->PPUCTRL & 0x08) << 9;
	u16 tile_row = (row & 7) ^ (vertical_flip ? 7 : 0);
	return pattern_table | ((u16)tile << 4) | plane | tile_row;
}

static inline u8 ppu_reverse_bits(u8 value)
{
	value = (value >> 4) | (value << 4);
	value = ((value & 0xCC) >> 2) | ((value & 0x33) << 2);
	return ((value & 0xAA) >> 1) | ((value & 0x55) << 1);
}

static inline u8 ppu_sprite_pixel(NES_PPUState *ppu, i32 screen_x, u8 background_pixel)
{
	u8 selected_pixel = 0;
	u8 selected_attributes = 0;
	u32 selected_unit = ArrayCount(ppu->spr_units);
	for (u32 index = 0; index < ArrayCount(ppu->spr_units); ++index)
	{
		NES_PPUSpriteUnit *unit = &ppu->spr_units[index];
		u8 pixel = 0;
		if (unit->x)
		{
			unit->x--;
		}
		else
		{
			pixel = ((unit->pattern_lo >> 7) & 1) | (((unit->pattern_hi >> 7) & 1) << 1);
			unit->pattern_lo <<= 1;
			unit->pattern_hi <<= 1;
		}

		if (selected_unit == ArrayCount(ppu->spr_units) && pixel)
		{
			selected_pixel = pixel;
			selected_attributes = unit->attributes;
			selected_unit = index;
		}
	}

	if (!(ppu->PPUMASK & PPUMASK_SPRITES) || !selected_pixel) return 0;
	if (background_pixel && screen_x < 255 && selected_unit == 0 && ppu->spr_units[0].index == 0) ppu->spr0_2cycle_delay = true;
	if (background_pixel && (selected_attributes & 0x20)) return 0;
	return 0x10 | ((selected_attributes & 3) << 2) | selected_pixel;
}

static inline void ppu_render_pixel(NES_Emulator *core, i32 screen_x, i32 screen_y)
{
	NES_PPUState *ppu = &core->ppu;

	u8 background_pixel = 0;

	if (ppu->PPUMASK & PPUMASK_BACKGROUND) {
		u32 pattern_bit = 15 - ppu->x;
		background_pixel =
		((ppu->chr_r0 >> pattern_bit) & 1) << 0|
		((ppu->chr_r1 >> pattern_bit) & 1) << 1;
	}

	u8 palette_index = ppu_sprite_pixel(ppu, screen_x, background_pixel);

	if (!palette_index && background_pixel)
	{
		palette_index = background_pixel |
		(((ppu->atr_r0 >> ppu->x) & 1) << 2) |
		(((ppu->atr_r1 >> ppu->x) & 1) << 3);
	}

	Assert(screen_x >= 0 && screen_x < 256);
	Assert(screen_y >= 0 && screen_y < 240);
	core->video[screen_y][screen_x] = ppu_bus_read(core, 0x3F00 + palette_index);
}

static inline b32 ppu_sprite_y_in_range(u8 ypos, u32 scanline, u32 sprite_height)
{
	return (u8)(scanline - ypos) < sprite_height;
}

static void ppu_step_sprite_evaluation(NES_PPUState *ppu, u32 dot, u32 scanline)
{
	Assert(dot >= 1 && dot <= 256);

	// Dots 1-64 clear the 32 bytes of secondary OAM. The odd dot performs
	// a forced $FF read and the even dot writes it to secondary OAM.
	if (dot <= 64)
	{
		if (dot == 1)
		{
			ppu->oam_address  = 0;
			ppu->oam_latch    = 0xFF;
			ppu->soam_address = 0;
			memory_fill(ppu->soam_indices, 0xFF, sizeof(ppu->soam_indices));
		}
		else if (!(dot & 1))
		{
			ppu->soam[(dot >> 1) - 1] = 0xFF;
		}
		return;
	}

	// Reaching the end of primary OAM leaves the evaluator idle for the
	// remainder of dots 65-256.
	if (ppu->oam_address >= 256) return;

	// Odd dots read one byte from primary OAM. Even dots consume that byte.
	if (dot & 1)
	{
		ppu->oam_latch = ppu->_oam[ppu->oam_address];
		return;
	}

	u32 sprite_height = (ppu->PPUCTRL & PPUCTRL_SPRITE_SIZE_8X16) ? 16 : 8;
	u32 oam_offset = ppu->oam_address & 3;
	if (ppu->soam_address < 32)
	{
		ppu->soam[ppu->soam_address] = ppu->oam_latch;

		if (oam_offset == 0)
		{
			if (ppu_sprite_y_in_range(ppu->oam_latch, scanline, sprite_height))
			{
				ppu->soam_indices[ppu->soam_address >> 2] = (u8)(ppu->oam_address >> 2);
				ppu->soam_address++;
				ppu->oam_address++;
			}
			else
			{
				ppu->oam_address += 4;
			}
		}
		else
		{
			ppu->soam_address++;
			ppu->oam_address++;
		}
		return;
	}

	// Secondary OAM is full. The hardware intends to examine only the Y byte
	// of each remaining sprite, but its broken address increment also advances
	// m when a value is out of range. Tile, attribute, and X bytes may therefore
	// be interpreted as Y coordinates and set sprite overflow.
	if (ppu_sprite_y_in_range(ppu->oam_latch, scanline, sprite_height))
	{
		ppu->PPUSTATUS |= 0x20;
		ppu->oam_address = 256;
	}
	else
	{
		u32 oam_index = ppu->oam_address >> 2;
		ppu->oam_address = ((oam_index + 1) << 2) | ((oam_offset + 1) & 3);
	}
}

static void ppu_step_sprite_units(NES_Emulator *nes, NES_PPUState *ppu, u32 dot, u32 scanline)
{
	Assert(dot >= 257 && dot <= 320);
	u32 unit_index = (dot - 257) >> 3;
	u32 fetch_phase = (dot - 257) & 7;
	NES_PPUSpriteUnit *unit = &ppu->spr_units[unit_index];

	switch (fetch_phase)
	{
		case 0: unit->index = ppu->soam_indices[unit_index]; ppu->spr_ypos_latch = ppu->SOAM[unit_index].ypos; break;
		case 1: ppu->spr_tile_latch = ppu->SOAM[unit_index].index; break;
		case 2: unit->attributes = ppu->SOAM[unit_index].attrs; break;
		default: unit->x = ppu->SOAM[unit_index].xpos; break;
	}

	i32 row = (i32)scanline - ppu->spr_ypos_latch;
	switch (fetch_phase)
	{
		case 4: ppu->address = ppu_sprite_pattern_address(ppu, unit->attributes, ppu->spr_tile_latch, row, 0); break;
		case 5:
		{
			u8 pattern = ppu_bus_read(nes, ppu->address);
			unit->pattern_lo = scanline >= 240 || unit->index == 0xFF ? 0 : (unit->attributes & 0x40) ? ppu_reverse_bits(pattern) : pattern;
		} break;
		case 6: ppu->address = ppu_sprite_pattern_address(ppu, unit->attributes, ppu->spr_tile_latch, row, 8); break;
		case 7:
		{
			u8 pattern = ppu_bus_read(nes, ppu->address);
			unit->pattern_hi = scanline >= 240 || unit->index == 0xFF ? 0 : (unit->attributes & 0x40) ? ppu_reverse_bits(pattern) : pattern;
		} break;
	}
}

static inline void ppu_advance_clock(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->ppu;
	core->ppu.dot = (u16)(ppu->dot + 1);
	if (ppu->dot < 341) return;

	core->ppu.dot = (u16)(0);
	core->ppu.scanline = (u16)(ppu->scanline + 1);
	if (ppu->scanline >= 262) core->ppu.scanline = (u16)(0);
}

u32 nes_ppu_step(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->ppu;
	u32 dot = ppu->dot;
	u32 scanline = ppu->scanline;
	u32 events = NES_PPU_EVENT_NONE;

	if (scanline == 241 && dot == 1)
	{
		core->ppu.PPUSTATUS = (u8)(ppu->PPUSTATUS | 0x80);
		prof_add_metric(PROF_METRIC_PPU_VBLANKS, 1);
		events |= NES_PPU_EVENT_FRAME;
		if (ppu->PPUCTRL & PPUCTRL_NMI_ENABLED) events |= NES_PPU_EVENT_NMI;
	}
	else if (scanline == 261 && dot == 1)
	{
		core->ppu.PPUSTATUS = (u8)(ppu->PPUSTATUS & 0x1F);
	}
	b32 visible_scanline   = scanline < 240;
	b32 prerender_scanline = scanline == 261;
	b32 rendering_scanline = visible_scanline || prerender_scanline;

	if (dot > 0 && rendering_scanline && ppu_rendering_enabled(ppu))
	{
		if (visible_scanline && dot <= 256) ppu_step_sprite_evaluation(ppu, dot, scanline);
		if (rendering_scanline && dot >= 257 && dot <= 320) ppu_step_sprite_units(core, ppu, dot, scanline);

		if (dot < 337) ppu_shift_background(core);

		b32 fetch_cycle = dot < 257 || dot > 320;
		u32 fetch_phase = (dot - 1) & 7;
		if (fetch_cycle) switch (fetch_phase)
		{
			case 1: ppu->tile_id = ppu_fetch_nametable_byte(core, ppu->v); break;
			case 3: ppu->atr_b = ppu_fetch_attribute_byte(core, ppu->v); break;
			case 5: ppu_fetch_pattern_low(core); break;
			case 7: ppu_fetch_pattern_high_and_reload(core); break;
		}

		if (visible_scanline && dot < 257)
		{
			ppu_render_pixel(core, (i32)dot - 1, (i32)scanline);
		}

		if (fetch_cycle && fetch_phase == 7)  ppu->v = ppu_increment_horizontal(ppu->v);
		// """ If rendering is enabled, the PPU increments the vertical position in v """
		if (dot == 256)                       ppu->v = ppu_increment_vertical(ppu->v);
		// """ If rendering is enabled, the PPU copies all bits related to horizontal position from t to v: """
		if (dot == 257)                       ppu->v = ppu->v & ~0x041F | ppu->t & 0x041F;
		if (dot == 304 && prerender_scanline) ppu->v = ppu->v & ~0x7BE0 | ppu->t & 0x7BE0;

	}

	ppu_advance_clock(core);
	return events;
}

// NES PPU implementation.
