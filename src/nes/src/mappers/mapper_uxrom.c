
#include "mapper.h"
#include "../emulator_internal.h"

NES_MAPPER_VALID_FUNC(uxrom_valid) {
	return nes->chr_rom_size == 0;
}

NES_MAPPER_RSET_FUNC(uxrom_reset) {
	return true;
}

NES_BusResult uxrom_ppu(NES_Emulator *emulator, NES_BusMode mode, u32 address, u8 value) {
	switch (address >> 12) {
		case 0: case 1: {
			return nes_chr_ram_access(emulator, mode, address, value);
		} break;
		case 2: {
			b32 v = emulator->vmirror;
			address = address & 0x3FF | (address >> !v & 0x400);
			return nes_vram_access(emulator, mode, address, value);
		} break;
	}
	return nes_bus_result(NES_DEVICE_PPU, address, value);
}

NES_BusResult uxrom_cpu(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value) {
	/* prg rom, 0x8000 + */
	if ((address >> 15) == 1) {
		if (mode == NES_BUS_WRITE) {
			/* Todo: check spec about something about open bus conflicts... */
			nes_mapper_set_value(nes, 0, value);
		}
		else {
			i32 b = nes->values[0];
			i32 k = (nes->prg_rom_size >> 14) - 1;
			if (address < 0xC000)
			{
				address = (address & 0x3FFF) + (b << 14);
			}
			else
			{
				address = (address & 0x3FFF) + (k << 14);
			}
			return nes_prg_rom_access(nes, mode, address, value);
		}
	}
	return nes_bus_result(NES_DEVICE_CPU, address, value);
}
// NES UxROM mapper.
