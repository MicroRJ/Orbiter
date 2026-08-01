#ifndef FRONTEND_NES_TARGET_H
#define FRONTEND_NES_TARGET_H

#include "nes/emulator.h"
#include "color.h"

enum
{
	NES_TARGET_CHR_WIDTH          = 256,
	NES_TARGET_CHR_PATTERN_HEIGHT = 128,
	NES_TARGET_CHR_SPRITE_HEIGHT  = 64,
	NES_TARGET_CHR_HEIGHT         = NES_TARGET_CHR_PATTERN_HEIGHT + NES_TARGET_CHR_SPRITE_HEIGHT,
	NES_PATTERN_TILE_SIZE         = 8,
	NES_PATTERN_TABLE_TILE_COUNT  = 256,
	NES_PATTERN_TILE_COUNT        = NES_PATTERN_TABLE_TILE_COUNT * 2,
	NES_PALETTE_RAM_SIZE          = 32,
};

typedef struct
{
	rect_i32      texture_region;
	rect_i32    selection_region;
	NES_MapAddr  pattern_mapping;
	u16              ppu_address;
	u8                 oam_index;
	u8                         x;
	u8                         y;
	u8                      tile;
	u8                   palette;
	u8         behind_background;
	u8           flip_horizontal;
	u8             flip_vertical;
}
NES_TargetSprite;

typedef struct
{
	Color_RGBA8               color;
	u8              palette_address;
	u8                  color_index;
}
NES_TargetPaletteColor;

typedef struct
{
	NES_TargetPaletteColor colors[4];
	u8                         index;
	u8                     is_sprite;
}
NES_TargetPalette;

typedef struct
{
	u8 pixels[8][8];
}
NES_PatternTile;

typedef struct
{
	NES_PatternTile tiles[NES_PATTERN_TILE_COUNT];
	NES_MapAddr  mappings[NES_PATTERN_TILE_COUNT];
	u8              palette[NES_PALETTE_RAM_SIZE];
}
NES_CHRMap;

typedef struct NES_TargetPublication NES_TargetPublication;
struct NES_TargetPublication
{
	NES_CPUState cpu;
	NES_PPUState ppu;
	NES_APUState apu;
	u8 palletised_video[NES_VIDEO_WIDTH * NES_VIDEO_HEIGHT];
	Color_RGBA8 video[NES_VIDEO_WIDTH * NES_VIDEO_HEIGHT];
	NES_CHRMap chr_map;
	Color_RGBA8 chr_image[NES_TARGET_CHR_WIDTH * NES_TARGET_CHR_HEIGHT];
	Color_RGBA8 palette[64];
	NES_TargetSprite sprites[64];
	NES_TargetPalette palettes[8];
	u64 generation;
	b32 valid;
};

void nes_target_publish(NES_TargetPublication *publication, NES_Emulator *emulator);

#endif
