#include "mapper.h"
#include "../emulator_internal.h"

/*
https://www.nesdev.org/wiki/NROM

NROM: where the term NROM refers to either of Nintendo's
NES-NROM-128 and NROM-256 cartridge board ROMs.
The number (128 or 256) refers to the size of the PRG-ROM in
kilobits (not kilobytes).

Whether the ROM is NROM-128 or NROM-256 is determined by
checking the number of PRG-ROM banks in the iNES header.
Each bank is 16 KiB:
- 1 bank (16 KiB) corresponds to NROM-128.
- 2 banks (32 KiB) correspond to NROM-256.
*/

NES_MAPPER_VALID_FUNC(nrom_valid) {
	return nes->prg_rom_size == KiB(16) || nes->prg_rom_size == KiB(32);
}

NES_MAPPER_RSET_FUNC(nrom_reset) {
	return true;
}

NES_BusAccess nrom_ppu(NES_Emulator *nes, NES_BusAccess access) {
	switch (access.address >> 12)
	{
		case 0: case 1: {
			access = nes->chr_rom_size ? nes_chr_rom_access(nes, access) : nes_chr_ram_access(nes, access);
		} break;
		case 2: {
			b32 v = nes->vmirror;
			// NOTE(RJ)
			// Here's how this works for when I forget:
			// https://www.nesdev.org/wiki/Mirroring#Nametable_Mirroring
			//
			// This mapper has a fixed nametable arrangement, on hardware, that means
			// that it's controlled by an actual physical configuration.
			//
			// vmirror tells us whether the configuration is vertical, otherwise horizontal.
			//
			// Nametables are 1024 bytes, 0x400.
			//
			// First mask the address to be within a nametable.
			//
			// Then determine where the address actually lands depending on the configuration.
			//
			// For horizontal mirroring, we have [A] | [A].
			//                                   [B] | [B]
			// 2000 -> 2400
			// 2800 -> 2C00
			//
			//
			access.address = access.address & 0x3FF | (access.address >> !v & 0x400);
			access = nes_vram_access(nes, access);
		} break;
	}
	return access;
}

// """
// CPU $6000-$7FFF: Unbanked PRG-RAM, mirrored as necessary to fill entire 8 KiB window, write protectable with an external switch. (Family BASIC only)
// CPU $8000-$BFFF: First 16 KiB of PRG-ROM.
// CPU $C000-$FFFF: Last 16 KiB of PRG-ROM (NROM-256) or mirror of $8000-$BFFF (NROM-128).
// """
NES_BusAccess nrom_cpu(NES_Emulator *nes, NES_BusAccess access) {
	switch (access.address >> 13)
	{
		case 3: {
			access.address &= KiB(8) - 1;
			access = nes_prg_ram_access(nes, access);
		}
		break;
		case 4: case 5: case 6: case 7: {
			Assert(nes->prg_rom_size == KiB(16) || nes->prg_rom_size == KiB(32));
			access.address &= nes->prg_rom_size - 1;
			access = nes_prg_rom_access(nes, access);
		}
		break;
	}
	return access;
}

// NES NROM mapper.
