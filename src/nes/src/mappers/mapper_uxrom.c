
#include "mapper.h"
#include "../emulator_internal.h"

NES_MAPPER_RSET_FUNC(uxrom_reset) {
	return true;
}

NES_BusAccess uxrom_ppu(NES_Emulator *emulator, NES_BusAccess access) {
	switch (access.address >> 12) {
		case 0: case 1: {
			access = nes_chr_ram_access(emulator, access);
		} break;
		case 2: {
			b32 v = emulator->vmirror;
			access.address = access.address & 0x3FF | (access.address >> !v & 0x400);
			access = nes_vram_access(emulator, access);
		} break;
	}
	return access;
}

NES_BusAccess uxrom_cpu(NES_Emulator *nes, NES_BusAccess access) {
	/* prg rom, 0x8000 + */
	if ((access.address >> 15) == 1) {
		if (access.kind == NES_BUS_ACCESS_WRITE) {
			/* Todo: check spec about something
				about open bus conflicts... */
			nes_mapper_set_value(nes, 0, access.value);
		}
		else {
			i32 b = nes->values[0];
			i32 k = nes->num_prg_banks - 1;
			if (access.address < 0xC000)
			{
				access.address = (access.address & 0x3FFF) + (b << 14);
			}
			else
			{
				access.address = (access.address & 0x3FFF) + (k << 14);
			}
			access = nes_prg_rom_access(nes, access);
		}
	}
	return access;
}
// NES UxROM mapper.
