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

NES_MAPPER_VALID_FUNC(mmc1_valid) {
	if (nes->values[MMC1_CONTROL_R] > 0x1F) return false;
	if (nes->values[MMC1_CHR_BANK0_R] > 0x1F) return false;
	if (nes->values[MMC1_CHR_BANK1_R] > 0x1F) return false;
	if (nes->values[MMC1_PRG_BANK0_R] > 0x1F) return false;
	if (!nes->values[MMC1_LOAD_R] || nes->values[MMC1_LOAD_R] > 0x1F) return false;
	return true;
}

NES_MAPPER_RSET_FUNC(mmc1_reset) {
	nes_mapper_set_value(nes, MMC1_LOAD_R,    0x10);
	nes_mapper_set_value(nes, MMC1_CONTROL_R, 0x0C);
	return true;
}

NES_BusResult mmc1_ppu(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value) {
	u32 control = nes->values[MMC1_CONTROL_R];
	u32 table = address >> 12;
	switch (table) {
		case 0: case 1: {
			if (control & 0x10) {
				u32 bank = nes->values[MMC1_CHR_BANK0_R + table];
				address = bank * KiB(4) + (address & 0x0FFF);
			}
			else {
				u32 bank = nes->values[MMC1_CHR_BANK0_R] & 0x1E;
				address = bank * KiB(4) + (address & 0x1FFF);
			}
			return nes->chr_rom_size ? nes_chr_rom_access(nes, mode, address, value) : nes_chr_ram_access(nes, mode, address, value);
		} break;
		case 2: {
			switch (control & 3) {
				case 0: { address = address & 0x3FF; } break;
				case 1: { address = (address & 0x3FF) | 0x400; } break;
				case 2: case 3: {
					b32 v = control & 1;
					address = address & 0x3FF | (address >> v & 0x400);
				} break;
			}
			return nes_vram_access(nes, mode, address, value);
		} break;
	}
	return nes_bus_result(NES_DEVICE_PPU, address, value);
}

NES_BusResult mmc1_cpu(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value) {
	i32 c = nes->values[MMC1_CONTROL_R];

	// NOP
	if (address < 0x6000) return nes_bus_result(NES_DEVICE_CPU, address, value);
	if (address < 0x8000) {
		address &= 0x1FFF;
		return nes_prg_ram_access(nes, mode, address, value);
	}
	if (mode == NES_BUS_WRITE) {
		if (value & 128) {
			nes_mapper_set_value(nes, MMC1_LOAD_R, 16);
			nes_mapper_set_value(nes, MMC1_CONTROL_R, nes->values[MMC1_CONTROL_R] | 0x0C);
		}
		else {
			b32 r = nes->values[MMC1_LOAD_R] >> 1 | (value & 1) << 4;
			/* check reset bit */
			if (nes->values[MMC1_LOAD_R] & 1) {
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
				nes_mapper_set_value(nes, address >> 13 & 3, r);
			}
			else {
				nes_mapper_set_value(nes, MMC1_LOAD_R, r);
			}
		}
	}
	else {

		i32 b = nes->values[MMC1_PRG_BANK0_R] & 15;
		i32 j = (nes->prg_rom_size >> 14) - 1;

		switch (c >> 2 & 3) {
			case 0: case 1: {
				address = (address & 0x7FFF) + ((b >> 1) << 15);
			} break;
			case 2: {
				if (address < 0xC000) address &= 0x3FFF;
				else address = (address & 0x3FFF) | (b << 14);
			} break;
			case 3: {
				if (address < 0xC000)
				{
					address = (address & 0x3FFF) | (b << 14);
				}
				else
				{
					address = (address & 0x3FFF) | (j << 14);
				}
			} break;
		}

		return nes_prg_rom_access(nes, mode, address, value);
	}
	return nes_bus_result(NES_DEVICE_CPU, address, value);
}
// NES MMC1 mapper.
