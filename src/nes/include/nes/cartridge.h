#ifndef NES_CARTRIDGE_H
#define NES_CARTRIDGE_H

#include "base.h"

typedef struct
{
	ByteSpan prg_rom;
	ByteSpan chr_rom;
	u32 mapper;
	b32 vertical_mirroring;
	b32 has_trainer;
	b32 four_screen;
}
NES_CartridgeDesc;

typedef struct
{
	u32 prg_rom_size;
	u32 chr_rom_size;
	u32 data_offset;
	u32 mapper;
	b32 vertical_mirroring;
	b32 has_trainer;
	b32 four_screen;
}
NES_CartridgeInfo;

// The returned descriptor borrows its PRG and CHR storage from file_data.
b32 nes_cartridge_parse_ines(ByteSpan file_data, NES_CartridgeDesc *cartridge);
// Inspects only the 16-byte iNES header. file_size is used to validate that the
// payload described by the header is actually present.
b32 nes_cartridge_inspect_ines(ByteSpan file_header, u64 file_size, NES_CartridgeInfo *info);

#endif
