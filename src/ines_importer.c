#include "ines_importer.h"

enum
{
	INES_HEADER_SIZE = 16,
	INES_TRAINER_SIZE = 512,
	INES_PRG_BANK_SIZE = KiB(16),
	INES_CHR_BANK_SIZE = KiB(8),
};



static b32 nes2_ram_layout_is_ines_compatible(u8 layout)
{
	return layout == 0 || layout == 0x07 || layout == 0x70;
}

static b32 nes2_header_is_ines_compatible(const u8 *header)
{
	u8 timing = header[12] & 0x03;
	return !(header[7] & 0x03) && !header[8] && !header[9] && nes2_ram_layout_is_ines_compatible(header[10]) && nes2_ram_layout_is_ines_compatible(header[11]) &&
		!(header[12] & ~0x03) && (timing == 0 || timing == 2) && !header[13] && !header[14] && header[15] <= 1;
}

b32 ines_import(ByteSpan source, NES_Game *game)
{
	Assert(game);
	*game = (NES_Game) {};
	ByteStream stream = byte_stream_reader(source);
	const u8 magic[] = { 'N', 'E', 'S', 0x1A };
	ByteSpan header_bytes = byte_stream_take(&stream, INES_HEADER_SIZE);
	if (stream.failed || !memory_match(header_bytes.data, magic, sizeof(magic))) return false;
	const u8 *header = header_bytes.data;
	if ((header[7] & 0x0C) == 0x08 && !nes2_header_is_ines_compatible(header)) return false;

	NES_Game result = {
		.metadata = {
			.mapper = (header[6] >> 4) | (header[7] & 0xF0),
			.mirroring = (header[6] & 0x08) ? NES_MIRROR_FOUR_SCREEN :
				((header[6] & 0x01) ? NES_MIRROR_VERTICAL : NES_MIRROR_HORIZONTAL),
			.trainer_size = (header[6] & 0x04) ? INES_TRAINER_SIZE : 0,
			.prg_rom_size = (u32)header[4] * INES_PRG_BANK_SIZE,
			.chr_rom_size = (u32)header[5] * INES_CHR_BANK_SIZE,
		},
	};
	result.trainer = byte_stream_take(&stream, result.metadata.trainer_size).data;
	result.prg_rom = byte_stream_take(&stream, result.metadata.prg_rom_size).data;
	result.chr_rom = byte_stream_take(&stream, result.metadata.chr_rom_size).data;
	if (stream.failed) return false;
	*game = result;
	return true;
}
