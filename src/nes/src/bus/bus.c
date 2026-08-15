#include "bus.h"
#include "../emulator_internal.h"
#include "../ppu/ppu.h"
#include "../apu/apu.h"




static NES_BusResult nes_cpu_bus_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value)
{
	Assert(address <= MAX_VALUE_U16);
	Assert(mode >= NES_BUS_READ && mode <= NES_BUS_PEEK);
	NES_BusResult result = nes_bus_result(NES_DEVICE_CPU, address, value);

	switch (address >> 12)
	{
		case 0: case 1:
		{
			return nes_wram_access(nes, mode, address & 0x7FF, value);
		}

		case 2: case 3:
		{
			return nes_ppu_register_access(nes, mode, address & 7, value);
		}

		case 4:
		{
			// $4020-$4FFF is cartridge expansion space, not a built-in APU or
			// controller register. The legacy router reached the mapper after
			// stripping its side-effect flag, so expansion writes could not work.
			if (address >= 0x4020)
			{
				return nes->mapper.cpu_bus(nes, mode, address, value);
			}

			if (mode == NES_BUS_PEEK) return result;

			switch (address)
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
					result = nes_apu_register_access(nes, mode, address, value);
				} break;

				case 0x4014:
				{
					if (mode == NES_BUS_WRITE)
					{
						for (i32 i = 0; i < 256; ++i)
						{
							u32 source = nes_cpu_bus_read(nes, value * 256 + i);
							// OAM DMA is implemented by the hardware as 256 writes to
							// OAMDATA. Routing it through the same register path makes the
							// transfer begin at OAMADDR and wrap after 256 bytes. The old
							// direct indexing always began at OAM byte zero.
							nes_ppu_register_access(nes, NES_BUS_WRITE, 4, (u8)source);
						}
						nes->cpu_stall_cycles += 513;
					}
				} break;
			}

			switch (address)
			{
				case 0x4016:
				case 0x4017:
				{
					i32 i = address & 1;
					if (mode == NES_BUS_WRITE)
					{
						if (value & 1)
						{
							nes->controllers[i] = nes->input_state.inputs[i];
						}
					}
					else
					{
						result.value = nes->controllers[i] & 1;
						nes->controllers[i] >>= 1;
					}
				} break;
			}

			return result;
		}
	}

	return nes->mapper.cpu_bus(nes, mode, address, value);
}

u8 nes_cpu_bus_read(NES_Emulator *core, u16 address)
{
	return nes_cpu_bus_access(core, NES_BUS_READ, address, 0).value;
}


void nes_cpu_bus_write(NES_Emulator *core, u16 address, u8 value)
{
	nes_cpu_bus_access(core, NES_BUS_WRITE, address, value);
}

u8 nes_cpu_bus_peek(NES_Emulator *core, u16 address)
{
	return nes_cpu_bus_access(core, NES_BUS_PEEK, address, 0).value;
}

NES_BusResult nes_cpu_bus_peek_mapped(NES_Emulator *core, u16 address)
{
	return nes_cpu_bus_access(core, NES_BUS_PEEK, address, 0);
}

NES_MapAddr nes_cpu_bus_map(NES_Emulator *core, u16 address)
{
	NES_BusResult result = nes_cpu_bus_peek_mapped(core, address);
	return nes_map_addr((NES_DeviceId)result.device, result.address);
}

static NES_BusResult nes_ppu_bus_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value)
{
	address &= 0x3FFF;

	if (address >= 0x3F00)
	{
		return nes_pram_access(nes, mode, address & 0x1F, value);
	}

	return nes->mapper.ppu_bus(nes, mode, address, value);
}

u8 nes_ppu_bus_read(NES_Emulator *core, u16 address)
{
	return nes_ppu_bus_access(core, NES_BUS_READ, address, 0).value;
}

void nes_ppu_bus_write(NES_Emulator *core, u16 address, u8 value)
{
	nes_ppu_bus_access(core, NES_BUS_WRITE, address, value);
}

u8 nes_ppu_bus_peek(NES_Emulator *core, u16 address)
{
	return nes_ppu_bus_access(core, NES_BUS_PEEK, address, 0).value;
}

NES_MapAddr nes_ppu_bus_map(NES_Emulator *core, u16 address)
{
	NES_BusResult result = nes_ppu_bus_access(core, NES_BUS_PEEK, address, 0);
	return nes_map_addr((NES_DeviceId)result.device, result.address);
}

NES_BusResult nes_oam_mem_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value)
{
	if (mode == NES_BUS_WRITE)
	{
		nes->ppu.primary_oam_bytes[address] = value;
	}
	else
	{
		value = nes->ppu.primary_oam_bytes[address];
	}
	return nes_bus_result(NES_DEVICE_OAM, address, value);
}

NES_BusResult nes_pram_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value)
{
	// address should be 0 - 31
	// includes its own little mirroring thing
	// palette bytes are 6 bits! and we mask on read and write?
	address &= 0x0F | (!!(address & 3) << 4);
	if (mode == NES_BUS_WRITE)
	{
		nes->ppu.palette_ram[address] = value & 63;
	}
	else
	{
		value = nes->ppu.palette_ram[address] & 63;
	}
	return nes_bus_result(NES_DEVICE_PRAM, address, value);
}

NES_BusResult nes_vram_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value)
{
	if (mode == NES_BUS_WRITE)
	{
		nes->_vram[address] = value;
	}
	else
	{
		value = nes->_vram[address];
	}
	return nes_bus_result(NES_DEVICE_VRAM, address, value);
}

NES_BusResult nes_wram_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value)
{
	address &= 0x7FF;
	if (mode == NES_BUS_WRITE)
	{
		nes->_wram[address] = value;
	}
	else
	{
		value = nes->_wram[address];
	}
	return nes_bus_result(NES_DEVICE_WRAM, address, value);
}

NES_BusResult nes_chr_rom_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value)
{
	Assert(address < nes->chr_rom_size);
	if (mode != NES_BUS_WRITE) value = nes->chr_rom[address];
	return nes_bus_result(NES_DEVICE_CHR_ROM, address, value);
}

NES_BusResult nes_prg_rom_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value)
{
	Assert(address < nes->prg_rom_size);
	if (mode != NES_BUS_WRITE) value = nes->prg_rom[address];
	return nes_bus_result(NES_DEVICE_PRG_ROM, address, value);
}

NES_BusResult nes_chr_ram_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value)
{
	if (mode == NES_BUS_WRITE)
	{
		nes->chr_ram[address] = value;
	}
	else
	{
		value = nes->chr_ram[address];
	}
	return nes_bus_result(NES_DEVICE_CHR_RAM, address, value);
}

NES_BusResult nes_prg_ram_access(NES_Emulator *nes, NES_BusMode mode, u32 address, u8 value)
{
	if (mode == NES_BUS_WRITE)
	{
		nes->prg_ram[address] = value;
	}
	else
	{
		value = nes->prg_ram[address];
	}
	return nes_bus_result(NES_DEVICE_PRG_RAM, address, value);
}

// NES bus implementation.
