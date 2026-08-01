// https://www.nesdev.org/wiki/MMC3
// assigned to iNES mapper 004.
// First appeared around 1988.
// Banks:
// CPU $6000-$7FFF: 8 KB PRG RAM bank (optional)
// CPU $8000-$9FFF (or $C000-$DFFF): 8 KB switchable PRG ROM bank
// CPU $A000-$BFFF: 8 KB switchable PRG ROM bank
// CPU $C000-$DFFF (or $8000-$9FFF): 8 KB PRG ROM bank, fixed to the second-last bank
// CPU $E000-$FFFF: 8 KB PRG ROM bank, fixed to the last bank
// PPU $0000-$07FF (or $1000-$17FF): 2 KB switchable CHR bank
// PPU $0800-$0FFF (or $1800-$1FFF): 2 KB switchable CHR bank
// PPU $1000-$13FF (or $0000-$03FF): 1 KB switchable CHR bank
// PPU $1400-$17FF (or $0400-$07FF): 1 KB switchable CHR bank
// PPU $1800-$1BFF (or $0800-$0BFF): 1 KB switchable CHR bank
// PPU $1C00-$1FFF (or $0C00-$0FFF): 1 KB switchable CHR bank

#include "mapper.h"
#include "../emulator_internal.h"

enum {
	R0, R1, R2, R3, R4, R5, R6, R7,
	// hardware doesn't r8 and r9! we fix them to mean -2 and -1, that way the addressing logic is cleaner!
	R8, R9,
	R_MODE,
	R_VERT,
	R_RAM,
};

NES_MAPPER_RSET_FUNC(mmc3_reset) {
	nes_mapper_set_value(nes, R8, (nes->core.prg_rom_size >> 13) - 2);
	nes_mapper_set_value(nes, R9, (nes->core.prg_rom_size >> 13) - 1);
	return true;
}

NES_BusAccess mmc3_ppu(NES_Emulator *nes, NES_BusAccess access)
{
}

NES_BusAccess mmc3_cpu(NES_Emulator *nes, NES_BusAccess access)
{
	if (access.kind == NES_BUS_ACCESS_WRITE)
	{
		switch (access.address & 0xE001)
		{
			case 0x8000:
			{
				nes_mapper_set_value(nes, R_MODE, access.value);
			}
			break;
			case 0xA000:
			{
				nes_mapper_set_value(nes, R_VERT, access.value);
			}
			break;
			case 0xA001:
			{
				nes_mapper_set_value(nes, R_RAM, access.value);
			}
			break;
			case 0x8001: {
				// """
				// R6 and R7 will ignore the top two bits, as the MMC3 has only 6 PRG ROM address lines.
				// R0 and R1 ignore the bottom bit, as the value written still counts banks in 1KB units
				// but odd numbered banks can't be selected
				// """
				u32 selected_register = nes->core.values[R_MODE] & 0x07;
				if (selected_register == R0 || selected_register == R1) access.value &= ~0x01; else
				if (selected_register == R6 || selected_register == R7) access.value &= ~0xC0;
				nes_mapper_set_value(nes, selected_register, access.value);
			}
			break;
		}
	}
	else
	{
		STATIC_ASSERT((0x0000 ^ 0x02) == 2);
		STATIC_ASSERT((0x0002 ^ 0x02) == 0);
		STATIC_ASSERT((0x8000 >> 13) == 4);
		STATIC_ASSERT((0x8000 ^ (0x40 << 8)) == 0xC000);
		STATIC_ASSERT((0xC000 ^ (0x40 << 8)) == 0x8000);
		if (access.address >= 0x8000) {
			// Bit 6 of the last value written to $8000 swaps the PRG windows at $8000 and $C000
			u32 index = (access.address >> 13) - 4;
			if (~index & 1) index ^= nes->core.values[R_MODE] >> 5 & 2;
			u32 bank = nes->core.values[R6 + index];
			access.address = (bank << 13) | (access.address & 0x1FFF);
			access = prg_rom_mem(nes, access);
		}
	}
	return access;
}
