#ifndef NES_INTERNAL_MAPPER_H
#define NES_INTERNAL_MAPPER_H

#include "../bus/bus.h"


void nes_mapper_set_value(NES_Emulator *core, u32 index, u8 value);

NES_MAPPER_RSET_FUNC(nrom_reset);
NES_BusAccess nrom_cpu(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess nrom_ppu(NES_Emulator *nes, NES_BusAccess access);

NES_MAPPER_RSET_FUNC(mmc1_reset);
NES_BusAccess mmc1_cpu(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess mmc1_ppu(NES_Emulator *nes, NES_BusAccess access);

NES_MAPPER_RSET_FUNC(mmc2_reset);
NES_BusAccess mmc2_cpu(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess mmc2_ppu(NES_Emulator *nes, NES_BusAccess access);

NES_MAPPER_RSET_FUNC(uxrom_reset);
NES_BusAccess uxrom_cpu(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess uxrom_ppu(NES_Emulator *nes, NES_BusAccess access);

#endif
