#ifndef NES_INTERNAL_MAPPER_H
#define NES_INTERNAL_MAPPER_H

#include "../bus/bus.h"


NES_BusResult nes_oam_mem_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value);
NES_BusResult nes_pram_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value);
NES_BusResult nes_vram_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value);
NES_BusResult nes_wram_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value);
NES_BusResult nes_chr_rom_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value);
NES_BusResult nes_prg_rom_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value);
NES_BusResult nes_chr_ram_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value);
NES_BusResult nes_prg_ram_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value);


void nes_mapper_set_value(NES_Emulator *core, u32 index, u8 value);

NES_MAPPER_VALID_FUNC(nrom_valid);
NES_MAPPER_RSET_FUNC(nrom_reset);
NES_BusResult nrom_cpu(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value);
NES_BusResult nrom_ppu(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value);

NES_MAPPER_VALID_FUNC(mmc1_valid);
NES_MAPPER_RSET_FUNC(mmc1_reset);
NES_BusResult mmc1_cpu(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value);
NES_BusResult mmc1_ppu(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value);

NES_MAPPER_VALID_FUNC(mmc2_valid);
NES_MAPPER_RSET_FUNC(mmc2_reset);
NES_BusResult mmc2_cpu(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value);
NES_BusResult mmc2_ppu(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value);

NES_MAPPER_VALID_FUNC(uxrom_valid);
NES_MAPPER_RSET_FUNC(uxrom_reset);
NES_BusResult uxrom_cpu(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value);
NES_BusResult uxrom_ppu(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value);

#endif
