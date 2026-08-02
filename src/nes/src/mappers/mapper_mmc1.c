#include "mapper.h"
#include "../emulator_internal.h"

// Legend Of Zelda
enum {
	MMC1_CONTROL_R = 0,
	MMC1_CHR_BANK0_R,
	MMC1_CHR_BANK1_R,
	MMC1_PRG_BANK0_R,
	MMC1_LOAD_R,
};

NES_MAPPER_RSET_FUNC(mmc1_reset) {
	nes_mapper_set_value(nes, MMC1_LOAD_R,    0x10);
	nes_mapper_set_value(nes, MMC1_CONTROL_R, 0x0C);
	return true;
}

NES_BusAccess mmc1_ppu(NES_Emulator *nes, NES_BusAccess access) {
	u32 control = nes->core.values[MMC1_CONTROL_R];
	u32 table = access.address >> 12;
	switch (table) {
		case 0: case 1: {
			if (control & 0x10) {
				u32 bank = nes->core.values[MMC1_CHR_BANK0_R + table];
				access.address = bank * KiB(4) + (access.address & 0x0FFF);
			}
			else {
				u32 bank = nes->core.values[MMC1_CHR_BANK0_R] & 0x1E;
				access.address = bank * KiB(4) + (access.address & 0x1FFF);
			}
			access = nes->core.chr_rom_size ? nes_chr_rom_access(nes, access) : nes_chr_ram_access(nes, access);
		} break;
		case 2: {
			switch (control & 3) {
				case 0: { access.address = access.address & 0x3FF; } break;
				case 1: { access.address = (access.address & 0x3FF) | 0x400; } break;
				case 2: case 3: {
					b32 v = control & 1;
					access.address = access.address & 0x3FF | (access.address >> v & 0x400);
				} break;
			}
			access = nes_vram_access(nes, access);
		} break;
	}
	return access;
}

NES_BusAccess mmc1_cpu(NES_Emulator *nes, NES_BusAccess access) {
	i32 c = nes->core.values[MMC1_CONTROL_R];

	// NOP
	if (access.address < 0x6000) {
		goto esc;
	}
	if (access.address < 0x8000) {
		access.address &= 0x1FFF;
		access = nes_prg_ram_access(nes, access);
		goto esc;
	}
	if (access.kind == NES_BUS_ACCESS_WRITE) {
		if (access.value & 128) {
			nes_mapper_set_value(nes, MMC1_LOAD_R, 16);
			nes_mapper_set_value(nes, MMC1_CONTROL_R, nes->core.values[MMC1_CONTROL_R] | 0x0C);
		}
		else {
			b32 r = nes->core.values[MMC1_LOAD_R] >> 1 | (access.value & 1) << 4;
			/* check reset bit */
			if (nes->core.values[MMC1_LOAD_R] & 1) {
				/* the shift register is cleared automatically */
				nes_mapper_set_value(nes, MMC1_LOAD_R, 16);
				// 0x8 1 00 0 .... ....
				// 0x9 1 00 1 .... ....
				// 0xA 1 01 0 .... ....
				// 0xB 1 01 1 .... ....
				// 0xC 1 10 0 .... ....
				// 0xD 1 10 1 .... ....
				// 0xE 1 11 0 .... ....
				// 0xF 1 11 1 .... ....
				nes_mapper_set_value(nes, access.address >> 13 & 3, r);
			}
			else {
				nes_mapper_set_value(nes, MMC1_LOAD_R, r);
			}
		}
	}
	else {

		i32 b = nes->core.values[MMC1_PRG_BANK0_R] & 15;
		i32 j = nes->core.num_prg_banks - 1;

		switch (c >> 2 & 3) {
			case 0: case 1: {
				access.address = (access.address & 0x7FFF) + ((b >> 1) << 15);
			} break;
			case 2: {
				if (access.address < 0xC000) access.address &= 0x3FFF;
				else access.address = (access.address & 0x3FFF) | (b << 14);
			} break;
			case 3: {
				if (access.address < 0xC000)
				{
					access.address = (access.address & 0x3FFF) | (b << 14);
				}
				else
				{
					access.address = (access.address & 0x3FFF) | (j << 14);
				}
			} break;
		}

		access = nes_prg_rom_access(nes, access);
	}
	esc:
	return access;
}
// NES MMC1 mapper.
