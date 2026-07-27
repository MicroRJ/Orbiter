#include "bus.h"
#include "../emulator_internal.h"
#include "../ppu/ppu.h"
#include "../apu/apu.h"

// address wiring

NES_MAPPER_INIT_FUNC(none_init)  { return false; }
NES_MAPPER_RSET_FUNC(none_reset) { return false; }
NES_BusAccess none_cpu(NES_Emulator *nes, NES_BusAccess access) { return access; }
NES_BusAccess none_ppu(NES_Emulator *nes, NES_BusAccess access) { return access; }

static NES_BusAccess nes_cpu_bus_access(NES_Emulator *nes, NES_BusAccess access)
{
	Assert(access.address <= MAX_VALUE_U16);
	Assert(access.kind >= NES_BUS_ACCESS_READ);
	Assert(access.kind <= NES_BUS_ACCESS_MAP);

	access.mapped = (NES_MapAddr) { NES_DEVICE_CPU, access.address };

	switch (access.address >> 12)
	{
		case 0: case 1:
		{
			return wram_mem(nes, access);
		}

		case 2: case 3:
		{
			access.address &= 7;
			return nes_ppu_register_access(nes, access);
		}

		case 4:
		{
			// $4020-$4FFF is cartridge expansion space, not a built-in APU or
			// controller register. The legacy router reached the mapper after
			// stripping its side-effect flag, so expansion writes could not work.
			if (access.address >= 0x4020)
			{
				return nes->mapper.cpu_bus(nes, access);
			}

			if (access.kind == NES_BUS_ACCESS_PEEK ||
				access.kind == NES_BUS_ACCESS_MAP)
			{
				return access;
			}

			switch (access.address)
			{
				case 0x4000: case 0x4004:
				case 0x4001: case 0x4005:
				case 0x4002: case 0x4006:
				case 0x4003: case 0x4007:
				case 0x4015: case 0x4017:
				case 0x4008:
				case 0x400A:
				case 0x400B:
				{
					access = nes_apu_register_access(nes, access);
				} break;

				case 0x4014:
				{
					if (access.kind == NES_BUS_ACCESS_WRITE)
					{
						for (i32 i = 0; i < 256; ++i)
						{
							u32 source = nes_cpu_bus_read(nes, access.value * 256 + i);
							// OAM DMA is implemented by the hardware as 256 writes to
							// OAMDATA. Routing it through the same register path makes the
							// transfer begin at OAMADDR and wrap after 256 bytes. The old
							// direct indexing always began at OAM byte zero.
							nes_ppu_register_access(nes, (NES_BusAccess) {
								.kind = NES_BUS_ACCESS_WRITE,
								.bus_address = 0x2004,
								.address = 4,
								.value = (u8)source,
							});
						}
						nes->core.cpu_stall_cycles += 513;
					}
				} break;
			}

			switch (access.address)
			{
				case 0x4016:
				case 0x4017:
				{
					i32 i = access.address & 1;
					if (access.kind == NES_BUS_ACCESS_WRITE)
					{
						if (access.value & 1)
						{
							nes->core.controllers[i] = nes->core.input_state.inputs[i];
						}
					}
					else
					{
						access.value = nes->core.controllers[i] & 1;
						nes->core.controllers[i] >>= 1;
					}
				} break;
			}

			return access;
		}
	}

	return nes->mapper.cpu_bus(nes, access);
}

NES_MappedRead nes_cpu_bus_read_mapped(NES_Emulator *core, u16 address)
{
	core->cpu_bus_metrics.reads += 1;
	NES_BusAccess access = nes_cpu_bus_access(core, (NES_BusAccess) { .kind = NES_BUS_ACCESS_READ, .bus_address = address, .address = address });
	return (NES_MappedRead) { access.mapped, access.value };
}

u8 nes_cpu_bus_read(NES_Emulator *core, u16 address)
{
	return nes_cpu_bus_read_mapped(core, address).value;
}

void nes_cpu_bus_write(NES_Emulator *core, u16 address, u8 value)
{
	core->cpu_bus_metrics.writes += 1;
	nes_cpu_bus_access(core, (NES_BusAccess) {
		.kind = NES_BUS_ACCESS_WRITE,
		.bus_address = address,
		.address = address,
		.value = value,
	});
}

u8 nes_cpu_bus_peek(NES_Emulator *core, u16 address)
{
	NES_BusAccess access = nes_cpu_bus_access(core, (NES_BusAccess) {
		.kind = NES_BUS_ACCESS_PEEK,
		.bus_address = address,
		.address = address,
	});
	return access.value;
}

NES_BusAccess nes_cpu_bus_peek_mapped(NES_Emulator *core, u16 address)
{
	NES_BusAccess access = nes_cpu_bus_access(core, (NES_BusAccess) {
		.kind = NES_BUS_ACCESS_PEEK,
		.bus_address = address,
		.address = address,
	});
	return access;
}

NES_MapAddr nes_cpu_bus_map(NES_Emulator *core, u16 address)
{
	NES_BusAccess access = nes_cpu_bus_access(core, (NES_BusAccess) {
		.kind = NES_BUS_ACCESS_MAP,
		.bus_address = address,
		.address = address,
	});
	return access.mapped;
}

static NES_BusAccess nes_ppu_bus_access(NES_Emulator *nes, NES_BusAccess access)
{
	access.bus_address &= 0x3FFF;
	access.address &= 0x3FFF;
	access.mapped = (NES_MapAddr) { NES_DEVICE_PPU, access.address };

	if (access.address >= 0x3F00)
	{
		access.address &= 0x1F;
		return pram_mem(nes, access);
	}

	return nes->mapper.ppu_bus(nes, access);
}

u8 nes_ppu_bus_read(NES_Emulator *core, u16 address)
{
	core->ppu_bus_metrics.reads += 1;
	NES_BusAccess access = nes_ppu_bus_access(core, (NES_BusAccess) {
		.kind = NES_BUS_ACCESS_READ,
		.bus_address = address,
		.address = address,
	});
	return access.value;
}

void nes_ppu_bus_write(NES_Emulator *core, u16 address, u8 value)
{
	core->ppu_bus_metrics.writes += 1;
	nes_ppu_bus_access(core, (NES_BusAccess) {
		.kind = NES_BUS_ACCESS_WRITE,
		.bus_address = address,
		.address = address,
		.value = value,
	});
}

u8 nes_ppu_bus_peek(NES_Emulator *core, u16 address)
{
	NES_BusAccess access = nes_ppu_bus_access(core, (NES_BusAccess) {
		.kind = NES_BUS_ACCESS_PEEK,
		.bus_address = address,
		.address = address,
	});
	return access.value;
}

NES_MapAddr nes_ppu_bus_map(NES_Emulator *core, u16 address)
{
	NES_BusAccess access = nes_ppu_bus_access(core, (NES_BusAccess) {
		.kind = NES_BUS_ACCESS_MAP,
		.bus_address = address,
		.address = address,
	});
	return access.mapped;
}

NES_BusAccess oam_mem(NES_Emulator *nes, NES_BusAccess access)
{
	access = nes_bus_access_mapped(access, NES_DEVICE_OAM);
	if (access.kind == NES_BUS_ACCESS_WRITE)
	{
		nes->core.ppu._oam[access.address] = access.value;
	}
	else
	{
		access.value = nes->core.ppu._oam[access.address];
	}
	return access;
}

NES_BusAccess pram_mem(NES_Emulator *nes, NES_BusAccess access)
{
	// address should be 0 - 31
	// includes its own little mirroring thing
	// palette bytes are 6 bits! and we mask on read and write?
	access.address &= 0x0F | (!!(access.address & 3) << 4);
	access = nes_bus_access_mapped(access, NES_DEVICE_PRAM);
	if (access.kind == NES_BUS_ACCESS_WRITE)
	{
		nes->core.ppu._pram[access.address] = access.value & 63;
	}
	else
	{
		access.value = nes->core.ppu._pram[access.address] & 63;
	}
	return access;
}

NES_BusAccess vram_mem(NES_Emulator *nes, NES_BusAccess access)
{
	access = nes_bus_access_mapped(access, NES_DEVICE_VRAM);
	if (access.kind == NES_BUS_ACCESS_WRITE)
	{
		nes->core._vram[access.address] = access.value;
	}
	else
	{
		access.value = nes->core._vram[access.address];
	}
	return access;
}

NES_BusAccess wram_mem(NES_Emulator *nes, NES_BusAccess access)
{
	access.address &= 0x7FF;
	access = nes_bus_access_mapped(access, NES_DEVICE_WRAM);
	if (access.kind == NES_BUS_ACCESS_WRITE)
	{
		nes->core._wram[access.address] = access.value;
	}
	else
	{
		access.value = nes->core._wram[access.address];
	}
	return access;
}

NES_BusAccess chr_rom_mem(NES_Emulator *nes, NES_BusAccess access)
{
	Assert(access.address < nes->core.chr_rom_size);
	access = nes_bus_access_mapped(access, NES_DEVICE_CHR_ROM);
	if (access.kind != NES_BUS_ACCESS_WRITE) {
		access.value = nes->core.chr_rom[access.address];
	}
	return access;
}

NES_BusAccess prg_rom_mem(NES_Emulator *nes, NES_BusAccess access)
{
	Assert(access.address < nes->core.prg_rom_size);
	access = nes_bus_access_mapped(access, NES_DEVICE_PRG_ROM);
	if (access.kind != NES_BUS_ACCESS_WRITE) {
		access.value = nes->core.prg_rom[access.address];
	}
	return access;
}

NES_BusAccess chr_ram_mem(NES_Emulator *nes, NES_BusAccess access)
{
	access = nes_bus_access_mapped(access, NES_DEVICE_CHR_RAM);
	if (access.kind == NES_BUS_ACCESS_WRITE)
	{
		nes->core.chr_ram[access.address] = access.value;
	}
	else
	{
		access.value = nes->core.chr_ram[access.address];
	}
	return access;
}

NES_BusAccess prg_ram_mem(NES_Emulator *nes, NES_BusAccess access)
{
	access = nes_bus_access_mapped(access, NES_DEVICE_PRG_RAM);
	if (access.kind == NES_BUS_ACCESS_WRITE)
	{
		nes->core.prg_ram[access.address] = access.value;
	}
	else
	{
		access.value = nes->core.prg_ram[access.address];
	}
	return access;
}

// NES bus implementation.
