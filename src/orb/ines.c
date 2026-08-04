// #include "ines.h"

// enum
// {
// 	INES_HEADER_SIZE = 16,
// 	INES_TRAINER_SIZE = 512,
// 	INES_PRG_BANK_SIZE = KiB(16),
// 	INES_CHR_BANK_SIZE = KiB(8),
// };

// static b32 nes2_ram_layout_is_ines_compatible(u8 layout)
// {
// 	// No RAM, 8 KiB volatile RAM, or 8 KiB nonvolatile RAM.
// 	return layout == 0 || layout == 0x07 || layout == 0x70;
// }

// static b32 nes2_header_is_ines_compatible(const u8 *header)
// {
// 	u8 timing = header[12] & 0x03;
// 	return !(header[7] & 0x03) && !header[8] && !header[9] && nes2_ram_layout_is_ines_compatible(header[10]) && nes2_ram_layout_is_ines_compatible(header[11]) &&
// 		!(header[12] & ~0x03) && (timing == 0 || timing == 2) && !header[13] && !header[14] && header[15] <= 1;
// }

// b32 nes_cartridge_inspect_ines(ByteSpan file_header, u64 file_size, NES_CartridgeInfo *info)
// {
// 	if (info) *info = (NES_CartridgeInfo) {};
// 	if (!info || !file_header.data || file_header.size < INES_HEADER_SIZE || file_size < INES_HEADER_SIZE) return false;

// 	const u8 *header = file_header.data;
// 	const u8 magic[] = { 'N', 'E', 'S', 0x1A };
// 	if (!memory_match(header, magic, sizeof(magic))) return false;
// 	b32 nes2 = (header[7] & 0x0C) == 0x08;
// 	if (nes2 && !nes2_header_is_ines_compatible(header)) return false;

// 	u32 prg_rom_size = (u32)header[4] * INES_PRG_BANK_SIZE;
// 	u32 chr_rom_size = (u32)header[5] * INES_CHR_BANK_SIZE;
// 	u32 data_offset = INES_HEADER_SIZE;
// 	if (header[6] & 0x04) data_offset += INES_TRAINER_SIZE;
// 	if (data_offset > file_size || prg_rom_size > file_size - data_offset) return false;
// 	u64 chr_offset = (u64)data_offset + prg_rom_size;
// 	if (chr_rom_size > file_size - chr_offset) return false;

// 	*info = (NES_CartridgeInfo) {
// 		.prg_rom_size = prg_rom_size,
// 		.chr_rom_size = chr_rom_size,
// 		.data_offset = data_offset,
// 		.mapper = (header[6] >> 4) | (header[7] & 0xF0),
// 		.vmirror = !!(header[6] & 0x01),
// 		.has_trainer = !!(header[6] & 0x04),
// 		.four_screen = !!(header[6] & 0x08),
// 	};
// 	return true;
// }

// b32 nes_cartridge_parse_ines(ByteSpan file_data, NES_CartridgeDesc *cartridge)
// {
// 	if (cartridge) *cartridge = (NES_CartridgeDesc) {};
// 	NES_CartridgeInfo info = {};
// 	if (!cartridge || !nes_cartridge_inspect_ines(file_data, file_data.size, &info)) return false;

// 	cartridge->prg_rom = byte_span(file_data.data + info.data_offset, info.prg_rom_size);
// 	cartridge->chr_rom = byte_span(file_data.data + info.data_offset + info.prg_rom_size, info.chr_rom_size);
// 	cartridge->mapper = info.mapper;
// 	cartridge->vmirror = info.vmirror;
// 	cartridge->has_trainer = info.has_trainer;
// 	cartridge->four_screen = info.four_screen;
// 	return true;
// }
