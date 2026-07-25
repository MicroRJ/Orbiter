#ifndef NES_CARTRIDGE_H
#define NES_CARTRIDGE_H

#include "base.h"

typedef struct
{
	ByteSpan prg_rom;
	ByteSpan chr_rom;
	u32 mapper;
	b32 vertical_mirroring;
}
NES_CartridgeDesc;

// The returned descriptor borrows its PRG and CHR storage from file_data.
b32 nes_cartridge_parse_ines(ByteSpan file_data, NES_CartridgeDesc *cartridge);

#endif
