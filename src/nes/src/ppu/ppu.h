#ifndef NES_INTERNAL_PPU_H
#define NES_INTERNAL_PPU_H

#include "nes/emulator.h"
#include "../bus/bus.h"

void nes_ppu_power_on(NES_PPUState *ppu);
void nes_ppu_reset(NES_PPUState *ppu);
NES_BusResult nes_ppu_register_access(NES_Emulator *core, NES_BusMode mode, u32 address, u8 value);
u32 nes_ppu_step(NES_Emulator *core);

#endif
