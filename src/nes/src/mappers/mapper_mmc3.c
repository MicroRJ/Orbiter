// https://www.nesdev.org/wiki/MMC3
// iNES Mapper 004.


#include "mapper.h"
#include "../emulator_internal.h"

enum { R0, R1, R2, R3, R4, R5, R6, R7, R8, R9 };
typedef struct
{
	u8 banks[10];
	u8 bank_update;
	u8 prg_swap;
	u8 chr_invert;
	u8 horz_mirror;
	u8 ram_enable;
	u8 ram_protect;
	u8 irq_pending;
	u8 irq_reload;
	u8 irq_latch;
	u8 irq_enable;
	u8 irq_counter;
}
MapperState;
STATIC_ASSERT(sizeof(MapperState) <= sizeof(((NES_Emulator *)(0))->values));


NES_MAPPER_RSET_FUNC(mmc3_reset) {
	MapperState *state = (MapperState *) nes->values;
	state->banks[R8] = (nes->prg_rom_size >> 13) - 2;
	state->banks[R9] = (nes->prg_rom_size >> 13) - 1;
	return true;
}

NES_BusResult mmc3_ppu(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value)
{
	(void)nes; (void)mode;
	return nes_bus_result(NES_DEVICE_PPU, address, value);
}

NES_BusResult mmc3_cpu(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value)
{
	MapperState *state = (MapperState *) nes->values;

	if (mode == NES_BUS_WRITE)
	{
		switch (address & 0xE001)
		{
			case 0x8000:
			{
				state->bank_update = value & 0x07;
				state->prg_swap    = !! (value & 0x40);
				state->chr_invert  = !! (value & 0x80);
			}
			break;
			case 0x8001: {
				// """
				// R6 and R7 will ignore the top two bits, as the MMC3 has only 6 PRG ROM address lines.
				// R0 and R1 ignore the bottom bit, as the value written still counts banks in 1KB units
				// but odd numbered banks can't be selected
				// """
				u32 index = state->bank_update;
				// TODO(RJ) do we actually need the mask or no?
				u32 mask = (index == 0 || index == 1) ? ~0x01 : (index == 6 || index == 7) ? ~0xC0 : 0xFF;
				state->banks[index] = value & mask;
			}
			break;
			case 0xA000:
			{
				state->horz_mirror = value & 1;
			}
			break;
			case 0xA001:
			{
				state->ram_enable  = !! (value & 0x80);
            state->ram_protect = !! (value & 0x40);
			}
			break;
			case 0xC000: {
				state->irq_latch = value;
			}
			break;
			case 0xC001: {
				state->irq_counter = 0;
				state->irq_reload  = 1;
			}
			break;
			// """
			// Writing any value to this register will disable MMC3 interrupts AND acknowledge any pending interrupts.
			// """
			case 0xE000: {
				state->irq_enable  = 0;
				state->irq_pending = 0;
			}
			break;
			case 0xE001: {
				state->irq_enable  = 1;
			}
			break;
		}
	}
	else
	{
		if (address >= 0x8000) {
			// TODO(RJ): just precompute this!
			// Bit 6 of the last value written to $8000 swaps the PRG windows at $8000 and $C000
			u32 index = (address >> 13) - 4;
			if (~index & 1) index ^= state->prg_swap << 1;
			u32 bank = nes->values[R6 + index];
			address = (bank << 13) | (address & 0x1FFF);
			return nes_prg_rom_access(nes, mode, address, value);
		}
	}
	return nes_bus_result(NES_DEVICE_CPU, address, value);
}
