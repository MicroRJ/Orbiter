#ifndef NES_INTERNAL_BUS_H
#define NES_INTERNAL_BUS_H

#include "nes/emulator.h"

static inline NES_BusAccess nes_bus_access_mapped(NES_BusAccess access,
	NES_DeviceId device)
{
	access.mapped.device = device;
	access.mapped.offset = access.address;
	return access;
}

typedef struct
{
	NES_MapAddr mapped;
	u8 value;
}
NES_MappedRead;

NES_BusAccess oam_mem(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess pram_mem(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess vram_mem(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess wram_mem(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess chr_rom_mem(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess prg_rom_mem(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess chr_ram_mem(NES_Emulator *nes, NES_BusAccess access);
NES_BusAccess prg_ram_mem(NES_Emulator *nes, NES_BusAccess access);

// The CPU address space exposes operations with explicit semantics. The
// generic bus callback remains an implementation detail of devices and
// mappers; CPU execution and debugger code should not construct bus modes.
u8          nes_cpu_bus_read(NES_Emulator *core, u16 address);
NES_MappedRead nes_cpu_bus_read_mapped(NES_Emulator *core, u16 address);
void        nes_cpu_bus_write(NES_Emulator *core, u16 address, u8 value);
u8          nes_cpu_bus_peek(NES_Emulator *core, u16 address);
NES_BusAccess nes_cpu_bus_peek_mapped(NES_Emulator *core, u16 address);
NES_MapAddr nes_cpu_bus_map(NES_Emulator *core, u16 address);

u8          nes_ppu_bus_read(NES_Emulator *core, u16 address);
void        nes_ppu_bus_write(NES_Emulator *core, u16 address, u8 value);
u8          nes_ppu_bus_peek(NES_Emulator *core, u16 address);
NES_MapAddr nes_ppu_bus_map(NES_Emulator *core, u16 address);

#endif
