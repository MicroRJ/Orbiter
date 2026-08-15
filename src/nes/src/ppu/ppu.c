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

static void ppu_fetch_nametable_byte(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->ppu;
	core->ppu.tile_id = (u8)(ppu_bus_read(core, 0x2000 | (ppu->v & 0x0FFF)));
}

static void ppu_fetch_attribute_byte(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->ppu;
	u16 coarse_x = ppu->v & 31;
	u16 coarse_y = (ppu->v >> 5) & 31;
	u16 nametable = ppu->v & 0x0C00;
	u16 address = 0x23C0 | nametable | ((coarse_y >> 2) << 3) | (coarse_x >> 2);
	core->ppu.atr_b = (u8)(ppu_bus_read(core, address));
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

static u8 ppu_background_pixel(const NES_PPUState *ppu)
{
	if (!(ppu->PPUMASK & PPUMASK_BACKGROUND)) return 0;
	u32 pattern_bit = 15 - ppu->x;
	return ((ppu->chr_r0 >> pattern_bit) & 1) |
	(((ppu->chr_r1 >> pattern_bit) & 1) << 1);
}

static u16 ppu_sprite_pattern_address(const NES_PPUState *ppu, NES_PPUSprite sprite, i32 row, u16 plane)
{
	b32 vertical_flip = !!(sprite.attrs & 0x80);
	if (ppu->PPUCTRL & PPUCTRL_SPRITE_SIZE_8X16)
	{
		u16 pattern_table = (sprite.index & 1) << 12;
		u16 tile = (sprite.index & 0xFE) | (((row >> 3) & 1) ^ vertical_flip);
		u16 tile_row = (row & 7) ^ (vertical_flip ? 7 : 0);
		return pattern_table | (tile << 4) | plane | tile_row;
	}

	u16 pattern_table = (ppu->PPUCTRL & 0x08) << 9;
	u16 tile_row = (row & 7) ^ (vertical_flip ? 7 : 0);
	return pattern_table | ((u16)sprite.index << 4) | plane | tile_row;
}

static inline u8 ppu_sprite_pixel(NES_Emulator *core, i32 screen_x, i32 screen_y, u8 background_pixel)
{
	NES_PPUState *ppu = &core->ppu;
	if (!(ppu->PPUMASK & PPUMASK_SPRITES)) return 0;

	for (i32 i = 0; i < ppu->nsprs; ++i)
	{
		NES_PPUSprite sprite = ppu->sprs[i];
		i32 sprite_x = screen_x - sprite.xpos;
		i32 sprite_y = screen_y - sprite.ypos - 1;
		if (sprite_x < 0 || sprite_x >= 8) continue;

		u8 pattern_low = ppu_bus_read(core, ppu_sprite_pattern_address(ppu, sprite, sprite_y, 0));
		u8 pattern_high = ppu_bus_read(core, ppu_sprite_pattern_address(ppu, sprite, sprite_y, 8));
		u32 pattern_bit = (sprite.attrs & 0x40) ? sprite_x : 7 - sprite_x;
		u8 sprite_pixel = ((pattern_low >> pattern_bit) & 1) | ((pattern_high >> pattern_bit) & 1) << 1;
		if (!sprite_pixel) continue;

		if (background_pixel && i == 0)
		{
			core->ppu.spr0_2cycle_delay = (u8)(ppu->spr0_enable);
		}

		if ((sprite.attrs & 0x20) && background_pixel) return 0;
		return 0x10 | ((sprite.attrs & 3) << 2) | sprite_pixel;
	}

	return 0;
}

static inline void ppu_render_pixel(NES_Emulator *core, i32 screen_x, i32 screen_y)
{
	NES_PPUState *ppu = &core->ppu;
	u8 background_pixel = ppu_background_pixel(ppu);
	u8 palette_index = ppu_sprite_pixel(core, screen_x, screen_y, background_pixel);

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
			ppu->oam_index  = 0;
			ppu->oam_offset = 0;
			ppu->oam_latch  = 0xFF;
			ppu->soam_index = 0;
		}
		else if (!(dot & 1))
		{
			ppu->soam[(dot >> 1) - 1] = 0xFF;
		}
		return;
	}

	// Reaching the end of primary OAM leaves the evaluator idle for the
	// remainder of dots 65-256.
	if (ppu->oam_index >= ArrayCount(ppu->OAM)) return;

	// Odd dots read one byte from primary OAM. Even dots consume that byte.
	if (dot & 1)
	{
		ppu->oam_latch = ppu->_oam[ppu->oam_index * sizeof(NES_PPUSprite) + ppu->oam_offset];
		return;
	}

	u32 sprite_height = (ppu->PPUCTRL & PPUCTRL_SPRITE_SIZE_8X16) ? 16 : 8;
	if (ppu->soam_index < sizeof(ppu->soam))
	{
		Assert(ppu->oam_offset < sizeof(NES_PPUSprite));
		ppu->soam[ppu->soam_index] = ppu->oam_latch;

		if (ppu->oam_offset == 0)
		{
			if (ppu_sprite_y_in_range(ppu->oam_latch, scanline, sprite_height))
			{
				ppu->soam_index++;
				ppu->oam_offset = 1;
			}
			else
			{
				ppu->oam_index++;
			}
		}
		else
		{
			ppu->soam_index++;
			ppu->oam_offset++;
			if (ppu->oam_offset == sizeof(NES_PPUSprite))
			{
				ppu->oam_offset = 0;
				ppu->oam_index++;
			}
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
		ppu->oam_index = ArrayCount(ppu->OAM);
	}
	else
	{
		ppu->oam_index++;
		ppu->oam_offset = (ppu->oam_offset + 1) & 3;
	}
}

static void ppu_evaluate_sprites(NES_Emulator *core, i32 scanline)
{
	NES_PPUState *ppu = &core->ppu;
	i32 sprite_height = (ppu->PPUCTRL & PPUCTRL_SPRITE_SIZE_8X16) ? 16 : 8;
	core->ppu.spr0_enable = (u8)(false);
	core->ppu.nsprs = (u8)(0);

	for (i32 i = 0; i < 64; ++i)
	{
		NES_PPUSprite sprite = ppu->OAM[i];
		i32 row = scanline - sprite.ypos;
		if (row < 0 || row >= sprite_height) continue;

		if (ppu->nsprs < ppu_max_nsprs_per_scanline)
		{
			if (i == 0) core->ppu.spr0_enable = (u8)(true);
			u32 sprite_index = ppu->nsprs;
			ppu->sprs[sprite_index] = sprite;
			core->ppu.nsprs = (u8)(sprite_index + 1);
		}
		else
		{
			core->ppu.PPUSTATUS = (u8)(ppu->PPUSTATUS | 0x20);
		}
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

		if (dot < 337) ppu_shift_background(core);

		b32 fetch_cycle = dot < 257 || dot > 320;
		u32 fetch_phase = (dot - 1) & 7;
		if (fetch_cycle) switch (fetch_phase)
		{
			case 1: ppu_fetch_nametable_byte(core); break;
			case 3: ppu_fetch_attribute_byte(core); break;
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

		if (visible_scanline && dot == 256 && (ppu->PPUMASK & PPUMASK_SPRITES))
		{
			ppu_evaluate_sprites(core, (i32)scanline);
		}
	}

	ppu_advance_clock(core);
	return events;
}

// NES PPU implementation.
