#include "mapper.h"
#include "../emulator_internal.h"

enum {
	CHR0_A, CHR0_B,
	CHR1_A, CHR1_B,
	LATCH0, LATCH1,
	MIRRORING,
	BANK_0,
	BANK_1,
	BANK_2,
	BANK_3,
};

NES_MAPPER_VALID_FUNC(mmc2_valid) {
	u32 bank_count = nes->prg_rom_size >> 13;
	if (bank_count < 4) return false;
	if (nes->values[LATCH0] != 0xFD && nes->values[LATCH0] != 0xFE) return false;
	if (nes->values[LATCH1] != 0xFD && nes->values[LATCH1] != 0xFE) return false;
	if (nes->values[MIRRORING] > 1) return false;
	if (nes->values[BANK_1] != bank_count - 3) return false;
	if (nes->values[BANK_2] != bank_count - 2) return false;
	if (nes->values[BANK_3] != bank_count - 1) return false;
	return true;
}

NES_MAPPER_RSET_FUNC(mmc2_reset) {
	nes_mapper_set_value(nes, LATCH0, 0xFE);
	nes_mapper_set_value(nes, LATCH1, 0xFE);
	nes_mapper_set_value(nes, BANK_1, (nes->prg_rom_size >> 13) - 3);
	nes_mapper_set_value(nes, BANK_2, (nes->prg_rom_size >> 13) - 2);
	nes_mapper_set_value(nes, BANK_3, (nes->prg_rom_size >> 13) - 1);
	return true;
}

NES_BusAccess mmc2_ppu(NES_Emulator *nes, NES_BusAccess access) {
	u16 latch_address = (u16)access.address;
	switch (access.address >> 12) {
		case 0: case 1: {
			u32 table = access.address >> 12;
			u8 latch = nes->values[LATCH0 + table];
			Assert(latch == 0xFD || latch == 0xFE);
			u32 register_index = CHR0_A + table * 2 + (latch == 0xFE);
			u32 bank = nes->values[register_index];
			access.address = (bank << 12) + (access.address & 0x0FFF);
			access = nes_chr_rom_access(nes, access);

			if (access.kind == NES_BUS_ACCESS_READ)
			{
				if (latch_address == 0x0FD8) {
					nes_mapper_set_value(nes, LATCH0, 0xFD);
				}
				else if (latch_address == 0x0FE8) {
					nes_mapper_set_value(nes, LATCH0, 0xFE);
				}
				else if (latch_address >= 0x1FD8 && latch_address <= 0x1FDF) {
					nes_mapper_set_value(nes, LATCH1, 0xFD);
				}
				else if (latch_address >= 0x1FE8 && latch_address <= 0x1FEF) {
					nes_mapper_set_value(nes, LATCH1, 0xFE);
				}
			}
		} break;
		case 2: {
			b32 v = nes->values[MIRRORING];
			Assert(v == 0 || v == 1);
			access.address = access.address & 0x3FF | (access.address >> v & 0x400);
			access = nes_vram_access(nes, access);
		} break;
	}
	return access;
}

NES_BusAccess mmc2_cpu(NES_Emulator *nes, NES_BusAccess access) {
	if (access.address >= 0x6000)
	{
		// CPU $6000-$7FFF: 8 KB PRG RAM bank
		if (access.address < 0x8000) {
			access.address &= 0x1FFF;
			access = nes_prg_ram_access(nes, access);
		}
		else
		{
			if (access.kind == NES_BUS_ACCESS_WRITE)
			{
				switch (access.address >> 12)
				{
					case 10: nes_mapper_set_value(nes, BANK_0,    access.value & 15); break;
					case 11: nes_mapper_set_value(nes, CHR0_A,    access.value & 31); break;
					case 12: nes_mapper_set_value(nes, CHR0_B,    access.value & 31); break;
					case 13: nes_mapper_set_value(nes, CHR1_A,    access.value & 31); break;
					case 14: nes_mapper_set_value(nes, CHR1_B,    access.value & 31); break;
					case 15: nes_mapper_set_value(nes, MIRRORING, access.value &  1); break;
				}
			}
			else
			{
				u32 window = (access.address - 0x8000) >> 13;
				u32 bank = nes->values[BANK_0 + window];
				access.address = (bank << 13) | (access.address & 0x1FFF);
				access = nes_prg_rom_access(nes, access);
			}
		}
	}
	return access;
}
// NES MMC2 mapper.
