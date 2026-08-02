// https://www.nesdev.org/wiki/MMC3


#include "mapper.h"
#include "../emulator_internal.h"

enum {
	R0, R1, R2, R3, R4, R5, R6, R7,
	// hardware doesn't r8 and r9! we fix them to mean -2 and -1, that way the addressing logic is cleaner!
	R8, R9,
	R_MODE,
	R_VERT,
	R_RAM,
	R_IRQ_RELOAD,
	R_IRQ_COUNTER,
	R_IRQ_DISABLED,
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
			case 0xC000: {
				nes_mapper_set_value(nes, R_IRQ_RELOAD, access.value);
			}
			break;
			case 0xC001: {
				nes_mapper_set_value(nes, R_IRQ_COUNTER, 0);
			}
			break;
			// """
			// Writing any value to this register will disable MMC3 interrupts AND acknowledge any pending interrupts.
			// """
			case 0xE000: {
				nes_mapper_set_value(nes, R_IRQ_DISABLED, 1);
			}
			break;
			case 0xE001: {
				nes_mapper_set_value(nes, R_IRQ_DISABLED, 0);
			}
			break;
		}
	}
	else
	{
		if (access.address >= 0x8000) {
			// Bit 6 of the last value written to $8000 swaps the PRG windows at $8000 and $C000
			u32 index = (access.address >> 13) - 4;
			if (~index & 1) index ^= nes->core.values[R_MODE] >> 5 & 2;
			u32 bank = nes->core.values[R6 + index];
			access.address = (bank << 13) | (access.address & 0x1FFF);
			access = nes_prg_rom_access(nes, access);
		}
	}
	return access;
}
