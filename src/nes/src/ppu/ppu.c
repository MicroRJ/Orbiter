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

void nes_ppu_reset(NES_PPUState *ppu)
{
	memory_zero(ppu, sizeof(*ppu));
}

NES_BusAccess nes_ppu_register_access(NES_Emulator *core, NES_BusAccess access)
{
	Assert(access.address < 8);
	access = nes_bus_access_mapped(access, NES_DEVICE_PPU);
	if (access.kind == NES_BUS_ACCESS_PEEK ||
		access.kind == NES_BUS_ACCESS_MAP)
	{
		return access;
	}

	Assert(access.kind == NES_BUS_ACCESS_READ || access.kind == NES_BUS_ACCESS_WRITE);
	NES_PPUState *ppu = &core->core.ppu;
	b32 write = access.kind == NES_BUS_ACCESS_WRITE;
	u8 value = access.value;

	switch (access.address)
	{
		case 0: // PPUCTRL
		{
			if (write)
			{
				core->core.ppu.PPUCTRL = (u8)(value);
				core->core.ppu.t = (u16)((ppu->t & ~(3 << 10)) | (value & 3) << 10);
			}
		} break;

		case 1: // PPUMASK
		{
			if (write) core->core.ppu.PPUMASK = (u8)(value);
		} break;

		case 2: // PPUSTATUS
		{
			if (!write)
			{
				value = ppu->PPUSTATUS;
				core->core.ppu.PPUSTATUS = (u8)(ppu->PPUSTATUS & ~0x80);
				core->core.ppu.w = (u8)(0);
			}
		} break;

		case 3: // OAMADDR
		{
			if (write) core->core.ppu.OAMADDR = (u8)(value);
		} break;

		case 4: // OAMDATA
		{
			if (write)
			{
				// Outside rendering, OAMDATA writes store at OAMADDR and then
				// increment its 8-bit address. This case used to be empty, so
				// CPU-driven OAM writes were silently discarded.
				core->core.ppu._oam[ppu->OAMADDR] = (u8)(value);
				core->core.ppu.OAMADDR = (u8)(ppu->OAMADDR + 1);
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
					core->core.ppu.t = (u16)((ppu->t & ~0x001F) | (value >> 3));
					core->core.ppu.x = (u8)(value & 7);
				}
				else
				{
					u32 t = (ppu->t & ~0x03E0) | ((value >> 3 & 31) << 5);
					core->core.ppu.t = (u16)((t & ~0x7000) | ((value & 7) << 12));
				}
				core->core.ppu.w = (u8)(ppu->w ^ 1);
			}
		} break;

		case 6: // PPUADDR
		{
			if (write)
			{
				if (!ppu->w)
				{
					core->core.ppu.t = (u16)((ppu->t & 0x00FF) | ((value & 0x3F) << 8));
				}
				else
				{
					core->core.ppu.t = (u16)((ppu->t & 0x7F00) | value);
					core->core.ppu.v = (u16)(ppu->t);
				}
				core->core.ppu.w = (u8)(ppu->w ^ 1);
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
				core->core.ppu.data_read_buf = (u8)(ppu_bus_read(core, bus_address - 0x1000));
			}
			else
			{
				value = ppu->data_read_buf;
				core->core.ppu.data_read_buf = (u8)(ppu_bus_read(core, bus_address));
			}

			// v is 15 bits even though only its low 14 bits reach the bus.
			core->core.ppu.v = (u16)((ppu->v + ((ppu->PPUCTRL & 4) ? 32 : 1)) & 0x7FFF);
		} break;
	}

	access.value = value;
	return access;
}

static void ppu_shift_background(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->core.ppu;
	core->core.ppu.atr_r0 = (u8)(ppu->atr_l0 << 7 | ppu->atr_r0 >> 1);
	core->core.ppu.atr_r1 = (u8)(ppu->atr_l1 << 7 | ppu->atr_r1 >> 1);
	core->core.ppu.chr_r0 = (u16)(ppu->chr_r0 << 1);
	core->core.ppu.chr_r1 = (u16)(ppu->chr_r1 << 1);

	if (ppu->spr0_2cycle_delay)
	{
		core->core.ppu.spr0_2cycle_delay = (u8)(false);
		core->core.ppu.PPUSTATUS = (u8)(ppu->PPUSTATUS | 0x40);
	}
}

static void ppu_fetch_nametable_byte(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->core.ppu;
	core->core.ppu.tile_id = (u8)(ppu_bus_read(core, 0x2000 | (ppu->v & 0x0FFF)));
}

static void ppu_fetch_attribute_byte(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->core.ppu;
	u16 coarse_x = ppu->v & 31;
	u16 coarse_y = (ppu->v >> 5) & 31;
	u16 nametable = ppu->v & 0x0C00;
	u16 address = 0x23C0 | nametable | ((coarse_y >> 2) << 3) | (coarse_x >> 2);
	core->core.ppu.atr_b = (u8)(ppu_bus_read(core, address));
}

static u16 ppu_background_pattern_address(const NES_PPUState *ppu, u16 plane)
{
	u16 pattern_table = (ppu->PPUCTRL & 0x10) << 8;
	u16 fine_y = (ppu->v >> 12) & 7;
	return pattern_table | ((u16)ppu->tile_id << 4) | plane | fine_y;
}

static void ppu_fetch_pattern_low(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->core.ppu;
	core->core.ppu.tile_lo = (u8)(ppu_bus_read(core, ppu_background_pattern_address(ppu, 0)));
}

static void ppu_fetch_pattern_high_and_reload(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->core.ppu;
	core->core.ppu.tile_hi = (u8)(ppu_bus_read(core, ppu_background_pattern_address(ppu, 8)));
	core->core.ppu.chr_r0 = (u16)(ppu->chr_r0 | ppu->tile_lo);
	core->core.ppu.chr_r1 = (u16)(ppu->chr_r1 | ppu->tile_hi);

	u32 attribute_shift = (((ppu->v >> 5) & 2) << 1) | (ppu->v & 2);
	core->core.ppu.atr_l0 = (u8)((ppu->atr_b >> attribute_shift) & 1);
	core->core.ppu.atr_l1 = (u8)((ppu->atr_b >> (attribute_shift + 1)) & 1);
}

static void ppu_increment_horizontal(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->core.ppu;
	if ((ppu->v & 31) == 31)
	{
		core->core.ppu.v = (u16)((ppu->v & ~31) ^ 0x0400);
	}
	else
	{
		core->core.ppu.v = (u16)(ppu->v + 1);
	}
}

static void ppu_increment_vertical(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->core.ppu;
	if (((ppu->v >> 12) & 7) != 7)
	{
		core->core.ppu.v = (u16)(ppu->v + 0x1000);
		return;
	}

	u32 v = ppu->v & ~0x7000;
	u32 coarse_y = (ppu->v >> 5) & 31;
	if (coarse_y == 29)
	{
		v = (v & ~0x03E0) ^ 0x0800;
	}
	else if (coarse_y == 31)
	{
		v &= ~0x03E0;
	}
	else
	{
		v += 0x0020;
	}
	core->core.ppu.v = (u16)(v);
}

static void ppu_copy_horizontal(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->core.ppu;
	core->core.ppu.v = (u16)((ppu->v & ~0x041F) | (ppu->t & 0x041F));
}

static void ppu_copy_vertical(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->core.ppu;
	core->core.ppu.v = (u16)((ppu->v & ~0x7BE0) | (ppu->t & 0x7BE0));
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
	NES_PPUState *ppu = &core->core.ppu;
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
			core->core.ppu.spr0_2cycle_delay = (u8)(ppu->spr0_enable);
		}

		if ((sprite.attrs & 0x20) && background_pixel) return 0;
		return 0x10 | ((sprite.attrs & 3) << 2) | sprite_pixel;
	}

	return 0;
}

static inline void ppu_render_pixel(NES_Emulator *core, i32 screen_x, i32 screen_y)
{
	NES_PPUState *ppu = &core->core.ppu;
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

static void ppu_evaluate_sprites(NES_Emulator *core, i32 scanline)
{
	NES_PPUState *ppu = &core->core.ppu;
	i32 sprite_height = (ppu->PPUCTRL & PPUCTRL_SPRITE_SIZE_8X16) ? 16 : 8;
	core->core.ppu.spr0_enable = (u8)(false);
	core->core.ppu.nsprs = (u8)(0);

	for (i32 i = 0; i < 64; ++i)
	{
		NES_PPUSprite sprite = ppu->OAM[i];
		i32 row = scanline - sprite.ypos;
		if (row < 0 || row >= sprite_height) continue;

		if (ppu->nsprs < ppu_max_nsprs_per_scanline)
		{
			if (i == 0) core->core.ppu.spr0_enable = (u8)(true);
			u32 sprite_index = ppu->nsprs;
			ppu->sprs[sprite_index] = sprite;
			core->core.ppu.nsprs = (u8)(sprite_index + 1);
		}
		else
		{
			core->core.ppu.PPUSTATUS = (u8)(ppu->PPUSTATUS | 0x20);
		}
	}
}

static inline void ppu_advance_clock(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->core.ppu;
	core->core.ppu.xtick = (u16)(ppu->xtick + 1);
	if (ppu->xtick < 341) return;

	core->core.ppu.xtick = (u16)(0);
	core->core.ppu.ytick = (u16)(ppu->ytick + 1);
	if (ppu->ytick >= 262) core->core.ppu.ytick = (u16)(0);
}

u32 nes_ppu_step(NES_Emulator *core)
{
	NES_PPUState *ppu = &core->core.ppu;
	u32 dot = ppu->xtick;
	u32 scanline = ppu->ytick;
	u32 events = NES_PPU_EVENT_NONE;

	if (scanline == 241 && dot == 1)
	{
		core->core.ppu.PPUSTATUS = (u8)(ppu->PPUSTATUS | 0x80);
		prof_add_metric(PROF_METRIC_PPU_VBLANKS, 1);
		events |= NES_PPU_EVENT_FRAME;
		if (ppu->PPUCTRL & PPUCTRL_NMI_ENABLED) events |= NES_PPU_EVENT_NMI;
	}
	else if (scanline == 261 && dot == 1)
	{
		core->core.ppu.PPUSTATUS = (u8)(ppu->PPUSTATUS & 0x1F);
	}
	b32 visible_scanline   = scanline < 240;
	b32 prerender_scanline = scanline == 261;
	b32 rendering_scanline = visible_scanline || prerender_scanline;

	if (dot > 0 && rendering_scanline && ppu_rendering_enabled(ppu))
	{
		if (dot < 337) ppu_shift_background(core);

		b32 fetch_cycle = dot < 257 || dot > 320;
		u32 fetch_phase = (dot - 1) & 7;
		if (fetch_cycle)
		{
			switch (fetch_phase)
			{
				case 1: ppu_fetch_nametable_byte(core); break;
				case 3: ppu_fetch_attribute_byte(core); break;
				case 5: ppu_fetch_pattern_low(core); break;
				case 7: ppu_fetch_pattern_high_and_reload(core); break;
			}
		}

		if (visible_scanline && dot < 257)
		{
			ppu_render_pixel(core, (i32)dot - 1, (i32)scanline);
		}

		if (fetch_cycle && fetch_phase == 7) ppu_increment_horizontal(core);
		if (dot == 256) ppu_increment_vertical(core);
		if (dot == 257) ppu_copy_horizontal(core);
		if (prerender_scanline && dot == 304) ppu_copy_vertical(core);

		if (visible_scanline && dot == 256 && (ppu->PPUMASK & PPUMASK_SPRITES))
		{
			ppu_evaluate_sprites(core, (i32)scanline);
		}
	}

	ppu_advance_clock(core);
	return events;
}

// NES PPU implementation.
