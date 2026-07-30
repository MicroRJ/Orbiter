#include "debugger.h"
#include "views.h"

static Color_SRGBA chr_map_color(Color_RGBA8 color)
{
	return (Color_SRGBA) {
		.r = color.r / 255.f,
		.g = color.g / 255.f,
		.b = color.b / 255.f,
		.a = 1.f,
	};
}

static const char *chr_map_device_name(NES_DeviceId device)
{
	switch (device)
	{
		case NES_DEVICE_CHR_ROM: return "CHR ROM";
		case NES_DEVICE_CHR_RAM: return "CHR RAM";
		case NES_DEVICE_PPU: return "PPU";
		case NES_DEVICE_NONE: return "NONE";
		default: return "OTHER";
	}
}

static void chr_map_draw_palettes(ViewFrameData *frame, rect_f32 rect)
{
	UI_Context *ui = frame->ui;
	UI_TextStyle style = ui->theme.code;
	style.color = ui->theme.text_subtle;
	f32 label_width = style.size * 5.f;
	f32 swatch_size = Min(16.f, rect.h);
	f32 gap = 3.f;
	for (u32 kind = 0; kind < 2; ++kind)
	{
		rect_f32 line = { rect.x, rect.y + kind * (swatch_size + 4.f), rect.w, swatch_size };
		ui_draw_text(ui, line, style, kind ? LIT("SPR") : LIT("BG"));
		f32 x = line.x + label_width;
		for (u32 index = 0; index < 4; ++index)
		{
			const FrontendPalette *palette = &frame->publication->palettes[kind * 4 + index];
			for (u32 slot = 0; slot < ArrayCount(palette->colors); ++slot)
			{
				rect_f32 swatch = { x, line.y, swatch_size, swatch_size };
				ui_draw_rect(ui, swatch, chr_map_color(palette->colors[slot].color));
				ui_draw_rect_outline(ui, swatch, 1.f, ui->theme.panel_outline);
				x += swatch_size + 1.f;
			}
			x += gap * 2.f;
		}
	}
}

static void chr_map_draw_tooltip(ViewFrameData *frame, rect_f32 image_rect)
{
	UI_Context *ui = frame->ui;
	if (!rect_f32_contains(image_rect, ui->mouse)) {
		return;
	}

	f32 local_x = (ui->mouse.x - image_rect.x) * CHR_MAP_TEXTURE_WIDTH / image_rect.w;
	f32 local_y = (ui->mouse.y - image_rect.y) * CHR_MAP_TEXTURE_HEIGHT / image_rect.h;
	u32 table = Min((u32)local_x / 128, 1u);
	u32 tile_x = Min(((u32)local_x % 128) / NES_PATTERN_TILE_SIZE, 15u);
	u32 tile_y = Min((u32)local_y / NES_PATTERN_TILE_SIZE, 15u);
	u32 tile_index = table * NES_PATTERN_TABLE_TILE_COUNT + tile_y * 16 + tile_x;
	f32 tile_extent = image_rect.w / 32.f;
	rect_f32 selected = {
		.x = image_rect.x + (table * 16 + tile_x) * tile_extent,
		.y = image_rect.y + tile_y * tile_extent,
		.w = tile_extent,
		.h = tile_extent,
	};
	ui_draw_rect_outline(ui, rect_f32_round_out(selected), 2.f, ui->theme.text_vibrant);

	NES_MapAddr mapped = frame->publication->chr_map.mappings[tile_index];
	String line0 = push_formatted(frame->scratch, "TABLE %u  TILE $%02X  PPU $%04X", table, tile_index & 0xFF, tile_index << 4);
	String line1 = push_formatted(frame->scratch, "%s $%05X", chr_map_device_name(mapped.device), mapped.address);
	UI_TextStyle style = ui->theme.code;
	style.color = ui->theme.text_neutral;
	f32 padding = 7.f;
	rect_f32 tooltip = { ui->mouse.x + 14.f, ui->mouse.y + 18.f, 260.f, style.size * 2.f + padding * 2.f + 4.f };
	tooltip.x = CLAMP(tooltip.x, frame->rect.x, Max(frame->rect.x, frame->rect.x + frame->rect.w - tooltip.w));
	tooltip.y = CLAMP(tooltip.y, frame->rect.y, Max(frame->rect.y, frame->rect.y + frame->rect.h - tooltip.h));
	ui_push_z(ui, UI_Z_OVERLAY);
	ui_push_unclipped(ui);
	ui_draw_backdrop(ui, tooltip);
	rect_f32 text = rect_f32_inset(tooltip, padding);
	ui_draw_text(ui, text, style, line0);
	text.y += style.size + 4.f;
	ui_draw_text(ui, text, style, line1);
	ui_pop_unclipped(ui);
	ui_pop_z(ui);
}

static void chr_map_draw_sprite_tooltip(ViewFrameData *frame, rect_f32 atlas, rect_f32 sprite_rect)
{
	UI_Context *ui = frame->ui;
	if (!rect_f32_contains(sprite_rect, ui->mouse)) {
		return;
	}
	i32 texture_x = (i32)((ui->mouse.x - atlas.x) * CHR_MAP_TEXTURE_WIDTH / atlas.w);
	i32 texture_y = (i32)((ui->mouse.y - atlas.y) * CHR_MAP_TEXTURE_HEIGHT / atlas.h);
	const FrontendSprite *sprite = NULL;
	for (u32 index = 0; index < ArrayCount(frame->publication->sprites); ++index) {
		rect_i32 region = frame->publication->sprites[index].selection_region;
		if (texture_x >= region.x && texture_x < region.x + region.w && texture_y >= region.y && texture_y < region.y + region.h) {
			sprite = &frame->publication->sprites[index];
			break;
		}
	}
	if (!sprite) {
		return;
	}
	rect_i32 region = sprite->selection_region;
	rect_f32 selected = {
		.x = atlas.x + region.x * atlas.w / CHR_MAP_TEXTURE_WIDTH,
		.y = atlas.y + region.y * atlas.h / CHR_MAP_TEXTURE_HEIGHT,
		.w = region.w * atlas.w / CHR_MAP_TEXTURE_WIDTH,
		.h = region.h * atlas.h / CHR_MAP_TEXTURE_HEIGHT,
	};
	ui_draw_rect_outline(ui, rect_f32_round_out(selected), 2.f, ui->theme.text_vibrant);

	String line0 = push_formatted(frame->scratch, "OAM $%02X  X %u  Y %u  TILE $%02X", sprite->oam_index, sprite->x, sprite->y, sprite->tile);
	String line1 = push_formatted(frame->scratch, "PAL %u  %s  FLIP %c%c", sprite->palette, sprite->behind_background ? "BEHIND BG" : "IN FRONT", sprite->flip_horizontal ? 'H' : '-', sprite->flip_vertical ? 'V' : '-');
	String line2 = push_formatted(frame->scratch, "PPU $%04X -> %s $%05X", sprite->ppu_address, chr_map_device_name(sprite->pattern_mapping.device), sprite->pattern_mapping.address);
	UI_TextStyle style = ui->theme.code;
	style.color = ui->theme.text_neutral;
	f32 padding = 7.f;
	rect_f32 tooltip = { ui->mouse.x + 14.f, ui->mouse.y + 18.f, 330.f, style.size * 3.f + padding * 2.f + 8.f };
	tooltip.x = CLAMP(tooltip.x, frame->rect.x, Max(frame->rect.x, frame->rect.x + frame->rect.w - tooltip.w));
	tooltip.y = CLAMP(tooltip.y, frame->rect.y, Max(frame->rect.y, frame->rect.y + frame->rect.h - tooltip.h));
	ui_push_z(ui, UI_Z_OVERLAY);
	ui_push_unclipped(ui);
	ui_draw_backdrop(ui, tooltip);
	rect_f32 text = rect_f32_inset(tooltip, padding);
	ui_draw_text(ui, text, style, line0);
	text.y += style.size + 4.f;
	ui_draw_text(ui, text, style, line1);
	text.y += style.size + 4.f;
	ui_draw_text(ui, text, style, line2);
	ui_pop_unclipped(ui);
	ui_pop_z(ui);
}

static void chr_map_view_content(ViewFrameData *frame)
{
	Assert(frame->chr_texture);
	if (!frame->publication->valid || !debugger_has_cartridge(frame->debugger))
	{
		UI_TextStyle style = frame->ui->theme.code;
		style.color = frame->ui->theme.text_subtle;
		ui_draw_text(frame->ui, frame->rect, style, LIT("No cartridge loaded - Ctrl+O to open an iNES ROM"));
		return;
	}

	rect_f32 content = rect_f32_inset(frame->rect, 10.f);
	rect_f32 palettes = rect_f32_slice(&content, AXIS_Y, -40.f);
	content.h -= 8.f;
	f32 scale = Min(content.w / CHR_MAP_TEXTURE_WIDTH, content.h / CHR_MAP_TEXTURE_HEIGHT);
	rect_f32 atlas = rect_f32_align(content, v2(CHR_MAP_TEXTURE_WIDTH * scale, CHR_MAP_TEXTURE_HEIGHT * scale), v2(0.5f, 0.5f));
	rect_f32 image_rect = { atlas.x, atlas.y, atlas.w, CHR_MAP_PATTERN_HEIGHT * scale };
	rect_f32 sprite_rect = { atlas.x, image_rect.y + image_rect.h, atlas.w, CHR_MAP_SPRITE_HEIGHT * scale };
	ui_draw_image(frame->ui, (Draw_TextureParams) {
		.rect = rect_f32_round_out(image_rect),
		.texture = frame->chr_texture,
		.region = { 0, 0, CHR_MAP_TEXTURE_WIDTH, CHR_MAP_PATTERN_HEIGHT },
		.tint = COLOR_WHITE,
	});
	ui_draw_rect(frame->ui, rect_f32_round_out(sprite_rect), frame->ui->theme.slider_track);
	ui_draw_image(frame->ui, (Draw_TextureParams) {
		.rect = rect_f32_round_out(sprite_rect),
		.texture = frame->chr_texture,
		.region = { 0, CHR_MAP_PATTERN_HEIGHT, CHR_MAP_TEXTURE_WIDTH, CHR_MAP_SPRITE_HEIGHT },
		.tint = COLOR_WHITE,
	});
	ui_draw_rect_outline(frame->ui, rect_f32_round_out(image_rect), 1.f, frame->ui->theme.panel_outline);
	ui_draw_rect_outline(frame->ui, rect_f32_round_out(sprite_rect), 1.f, frame->ui->theme.panel_outline);
	chr_map_draw_palettes(frame, palettes);
	chr_map_draw_tooltip(frame, image_rect);
	chr_map_draw_sprite_tooltip(frame, atlas, sprite_rect);
}

void chr_map_view_frame(ViewFrameData *frame)
{
	ViewFrameData content = view_begin_frame(frame, LIT("CHR MAP — PATTERN TABLES + OAM SPRITES + PALETTE RAM"));
	chr_map_view_content(&content);
	view_end_frame(&content);
}
