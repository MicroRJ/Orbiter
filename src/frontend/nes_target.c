#include "nes_target.h"

#define NES_PALETTE_COLORS(_) \
_(0x80,0x80,0x80) _(0x00,0x00,0xBB) _(0x37,0x00,0xBF) _(0x84,0x00,0xA6) \
_(0xBB,0x00,0x6A) _(0xB7,0x00,0x1E) _(0xB3,0x00,0x00) _(0x91,0x26,0x00) \
_(0x7B,0x2B,0x00) _(0x00,0x3E,0x00) _(0x00,0x48,0x0D) _(0x00,0x3C,0x22) \
_(0x00,0x2F,0x66) _(0x00,0x00,0x00) _(0x05,0x05,0x05) _(0x05,0x05,0x05) \
_(0xC8,0xC8,0xC8) _(0x00,0x59,0xFF) _(0x44,0x3C,0xFF) _(0xB7,0x33,0xCC) \
_(0xFF,0x33,0xAA) _(0xFF,0x37,0x5E) _(0xFF,0x37,0x1A) _(0xD5,0x4B,0x00) \
_(0xC4,0x62,0x00) _(0x3C,0x7B,0x00) _(0x1E,0x84,0x15) _(0x00,0x95,0x66) \
_(0x00,0x84,0xC4) _(0x11,0x11,0x11) _(0x09,0x09,0x09) _(0x09,0x09,0x09) \
_(0xFF,0xFF,0xFF) _(0x00,0x95,0xFF) _(0x6F,0x84,0xFF) _(0xD5,0x6F,0xFF) \
_(0xFF,0x77,0xCC) _(0xFF,0x6F,0x99) _(0xFF,0x7B,0x59) _(0xFF,0x91,0x5F) \
_(0xFF,0xA2,0x33) _(0xA6,0xBF,0x00) _(0x51,0xD9,0x6A) _(0x4D,0xD5,0xAE) \
_(0x00,0xD9,0xFF) _(0x66,0x66,0x66) _(0x0D,0x0D,0x0D) _(0x0D,0x0D,0x0D) \
_(0xFF,0xFF,0xFF) _(0x84,0xBF,0xFF) _(0xBB,0xBB,0xFF) _(0xD0,0xBB,0xFF) \
_(0xFF,0xBF,0xEA) _(0xFF,0xBF,0xCC) _(0xFF,0xC4,0xB7) _(0xFF,0xCC,0xAE) \
_(0xFF,0xD9,0xA2) _(0xCC,0xE1,0x99) _(0xAE,0xEE,0xB7) _(0xAA,0xF7,0xEE) \
_(0xB3,0xEE,0xFF) _(0xDD,0xDD,0xDD) _(0x11,0x11,0x11) _(0x11,0x11,0x11)

#define NES_PALETTE_COLOR(r, g, b) { r, g, b, 255 },
static const Color_RGBA8 nes_target_palette[64] = { NES_PALETTE_COLORS(NES_PALETTE_COLOR) };
#undef NES_PALETTE_COLOR
#undef NES_PALETTE_COLORS

static u32 nes_target_sprite_tile_index(const NES_PPUState *ppu, const NES_PPUSprite *sprite, u32 row)
{
	if (ppu->PPUCTRL & 0x20) {
		return (sprite->index & 1) * NES_PATTERN_TABLE_TILE_COUNT + (sprite->index & 0xFE) + row / NES_PATTERN_TILE_SIZE;
	}
	return !!(ppu->PPUCTRL & 0x08) * NES_PATTERN_TABLE_TILE_COUNT + sprite->index;
}

static void nes_target_publish_sprites(NES_TargetSnapshot *snapshot)
{
	const NES_PPUState *ppu = &snapshot->state.ppu;
	u32 sprite_height = ppu->PPUCTRL & 0x20 ? 16 : 8;
	for (u32 index = 0; index < ArrayCount(snapshot->sprites); ++index)
	{
		const NES_PPUSprite *source = &ppu->OAM[index];
		u32 tile_index = nes_target_sprite_tile_index(ppu, source, 0);
		i32 cell_x = (i32)(index % 16 * 16);
		i32 cell_y = (i32)(NES_TARGET_CHR_PATTERN_HEIGHT + index / 16 * 16);
		snapshot->sprites[index] = (NES_TargetSprite) {
			.texture_region = {
				.x = cell_x + 4,
				.y = cell_y + (16 - (i32)sprite_height) / 2,
				.w = NES_PATTERN_TILE_SIZE,
				.h = (i32)sprite_height,
			},
			.selection_region = { cell_x, cell_y, 16, 16 },
			.pattern_mapping = snapshot->chr_map.mappings[tile_index],
			.ppu_address = (u16)(tile_index * NES_PATTERN_TILE_SIZE * 2),
			.oam_index = (u8)index,
			.x = source->xpos,
			.y = (u8)(source->ypos + 1),
			.tile = source->index,
			.palette = source->attrs & 3,
			.behind_background = !!(source->attrs & 0x20),
			.flip_horizontal = !!(source->attrs & 0x40),
			.flip_vertical = !!(source->attrs & 0x80),
		};
	}
}

static void nes_target_publish_palettes(NES_TargetSnapshot *snapshot)
{
	for (u32 index = 0; index < ArrayCount(snapshot->palettes); ++index)
	{
		NES_TargetPalette *palette = &snapshot->palettes[index];
		palette->index = (u8)(index & 3);
		palette->is_sprite = index >= 4;
		for (u32 slot = 0; slot < ArrayCount(palette->colors); ++slot)
		{
			u8 address = (u8)((index >= 4 ? 0x10 : 0) + (index & 3) * 4 + slot);
			u8 color_index = snapshot->chr_map.palette[address];
			palette->colors[slot] = (NES_TargetPaletteColor) {
				.color = snapshot->palette[color_index & 63],
				.palette_address = address,
				.color_index = color_index,
			};
		}
	}
}

static void nes_target_colorize_video(NES_TargetSnapshot *snapshot)
{
	for (u32 index = 0; index < NES_VIDEO_WIDTH * NES_VIDEO_HEIGHT; ++index) {
		snapshot->video[index] = snapshot->palette[snapshot->video_indices[index] & 63];
	}
}

static void nes_target_render_chr(NES_TargetSnapshot *snapshot)
{
	const NES_CHRMap *map = &snapshot->chr_map;
	const NES_PPUState *ppu = &snapshot->state.ppu;
	memory_zero(snapshot->chr_image, sizeof(snapshot->chr_image));
	for (u32 tile_index = 0; tile_index < NES_PATTERN_TILE_COUNT; ++tile_index)
	{
		u32 table = tile_index / NES_PATTERN_TABLE_TILE_COUNT;
		u32 local_index = tile_index % NES_PATTERN_TABLE_TILE_COUNT;
		u32 tile_x = table * 128 + local_index % 16 * NES_PATTERN_TILE_SIZE;
		u32 tile_y = local_index / 16 * NES_PATTERN_TILE_SIZE;
		for (u32 y = 0; y < NES_PATTERN_TILE_SIZE; ++y) {
			for (u32 x = 0; x < NES_PATTERN_TILE_SIZE; ++x) {
				u8 color = map->palette[map->tiles[tile_index].pixels[y][x]];
				snapshot->chr_image[(tile_y + y) * NES_TARGET_CHR_WIDTH + tile_x + x] = snapshot->palette[color & 63];
			}
		}
	}

	u32 sprite_height = ppu->PPUCTRL & 0x20 ? 16 : 8;
	for (u32 sprite_index = 0; sprite_index < ArrayCount(ppu->OAM); ++sprite_index)
	{
		const NES_PPUSprite *sprite = &ppu->OAM[sprite_index];
		rect_i32 texture_region = snapshot->sprites[sprite_index].texture_region;
		for (u32 y = 0; y < sprite_height; ++y)
		{
			u32 source_y = sprite->attrs & 0x80 ? sprite_height - 1 - y : y;
			u32 tile_index = nes_target_sprite_tile_index(ppu, sprite, source_y);
			for (u32 x = 0; x < NES_PATTERN_TILE_SIZE; ++x)
			{
				u32 source_x = sprite->attrs & 0x40 ? NES_PATTERN_TILE_SIZE - 1 - x : x;
				u8 palette_slot = map->tiles[tile_index].pixels[source_y % NES_PATTERN_TILE_SIZE][source_x];
				if (palette_slot)
				{
					u8 color = map->palette[0x10 + (sprite->attrs & 3) * 4 + palette_slot];
					snapshot->chr_image[(texture_region.y + y) * NES_TARGET_CHR_WIDTH + texture_region.x + x] = snapshot->palette[color & 63];
				}
			}
		}
	}
}

void nes_target_publish(NES_TargetSnapshot *snapshot, const Debugger *debugger)
{
	Assert(snapshot);
	Assert(debugger);
	snapshot->valid = false;
	if (!debugger_has_cartridge(debugger))
	{
		return;
	}

	snapshot->state = debugger_capture_state(debugger);
	PROF_BLOCK("capture video") debugger_capture_video(debugger, snapshot->video_indices, NES_VIDEO_WIDTH);
	PROF_BLOCK("capture CHR map") debugger_capture_chr_map(debugger, &snapshot->chr_map);
	memory_copy(snapshot->palette, nes_target_palette, sizeof(snapshot->palette));
	PROF_BLOCK("publish sprites") nes_target_publish_sprites(snapshot);
	PROF_BLOCK("publish palettes") nes_target_publish_palettes(snapshot);
	PROF_BLOCK("colorize video") nes_target_colorize_video(snapshot);
	PROF_BLOCK("render CHR image") nes_target_render_chr(snapshot);
	snapshot->generation++;
	snapshot->valid = true;
}
