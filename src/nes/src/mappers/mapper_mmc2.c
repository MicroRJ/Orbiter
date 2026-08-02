#include "mapper.h"
#include "../emulator_internal.h"

// I'll continue this later...
/* Note: So, if all state is random, or atleast
zero, why do some mappers from other implementations
require additional setup? */
enum {
	PRG_BANK = 0,
	CHR0_A, CHR0_B,
	CHR1_A, CHR1_B,
	LATCH0, LATCH1,
	MIRRORING,
};

NES_MAPPER_RSET_FUNC(mmc2_reset) {
	nes_mapper_set_value(nes, LATCH0, 0xFE);
	nes_mapper_set_value(nes, LATCH1, 0xFE);
	return true;
}

NES_BusAccess mmc2_ppu(NES_Emulator *nes, NES_BusAccess access) {
	u16 latch_address = (u16)access.address;
	switch (access.address >> 12) {
		case 0: case 1: {
			u32 table = access.address >> 12;
			u8 latch = nes->core.values[LATCH0 + table];
			Assert(latch == 0xFD || latch == 0xFE);
			u32 register_index = CHR0_A + table * 2 + (latch == 0xFE);
			u32 bank = nes->core.values[register_index];
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
			b32 v = nes->core.values[MIRRORING];
			Assert(v == 0 || v == 1);
			access.address = access.address & 0x3FF |
				(access.address >> !v & 0x400);
			access = nes_vram_access(nes, access);
		} break;
	}
	return access;
}

/* Nocturne - C Sharp Minor */
NES_BusAccess mmc2_cpu(NES_Emulator *nes, NES_BusAccess access) {
	if (access.address >= 0x6000) {
		/* handle reading / writting to prg ram */
		if (access.address < 0x8000) {
			// [0x6000 - 0x8000 - 1] (8 KiB) ram
			access.address &= 0x1FFF;
			access = nes_prg_ram_access(nes, access);
		} else {
			if (access.kind == NES_BUS_ACCESS_WRITE) {
				/* otherwise, handle writting, you can only write to the bank switching registers  */
				/* Todo: could be simplified */
				switch (access.address >> 12)
				{
					case 10: nes_mapper_set_value(nes, PRG_BANK,  access.value & 15); break;
					case 11: nes_mapper_set_value(nes, CHR0_A,    access.value & 31); break;
					case 12: nes_mapper_set_value(nes, CHR0_B,    access.value & 31); break;
					case 13: nes_mapper_set_value(nes, CHR1_A,    access.value & 31); break;
					case 14: nes_mapper_set_value(nes, CHR1_B,    access.value & 31); break;
					case 15: nes_mapper_set_value(nes, MIRRORING, access.value &  1); break;
				}
			} else {
					/* handle reading */
				if (access.address < 0xA000) {
					// [0x8000 - 0xA000 - 1] (8 KiB) switchable prg rom bank
					access.address = (access.address & 0x1FFF) +
						(nes->core.values[PRG_BANK] << 13);
					access = nes_prg_rom_access(nes, access);
				}
				else {
					u32 fixed_window = (access.address - 0xA000) >> 13;
					u32 prg_bank_count = nes->core.prg_rom_size / KiB(8);
					u32 bank = prg_bank_count - 3 + fixed_window;
					access.address = bank * KiB(8) + (access.address & 0x1FFF);
					access = nes_prg_rom_access(nes, access);
				}
			}
		}
	}
	return access;
}
// NES MMC2 mapper.
