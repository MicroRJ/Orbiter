#include "nes/cartridge.h"

enum
{
	INES_HEADER_SIZE = 16,
	INES_TRAINER_SIZE = 512,
	INES_PRG_BANK_SIZE = KiB(16),
	INES_CHR_BANK_SIZE = KiB(8),
};

b32 nes_cartridge_parse_ines(ByteSpan file_data, NES_CartridgeDesc *cartridge)
{
	if (!cartridge) return false;
	*cartridge = (NES_CartridgeDesc) {};
	if (!file_data.data || file_data.size < INES_HEADER_SIZE) return false;

	const u8 *header = file_data.data;
	const u8 magic[] = { 'N', 'E', 'S', 0x1A };
	if (!memory_match(header, magic, sizeof(magic))) return false;

	u32 prg_rom_size = (u32)header[4] * INES_PRG_BANK_SIZE;
	u32 chr_rom_size = (u32)header[5] * INES_CHR_BANK_SIZE;
	u32 data_offset = INES_HEADER_SIZE;
	if (header[6] & 0x04) data_offset += INES_TRAINER_SIZE;
	if (data_offset > file_data.size || prg_rom_size > file_data.size - data_offset) return false;
	u32 chr_offset = data_offset + prg_rom_size;
	if (chr_rom_size > file_data.size - chr_offset) return false;

	cartridge->prg_rom = byte_span(file_data.data + data_offset, prg_rom_size);
	cartridge->chr_rom = byte_span(file_data.data + chr_offset, chr_rom_size);
	cartridge->mapper = (header[6] >> 4) | (header[7] & 0xF0);
	cartridge->vertical_mirroring = !!(header[6] & 0x01);
	return true;
}
