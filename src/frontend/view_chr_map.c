#include "debugger.h"
#include "ui_widgets.h"
#include "views.h"

typedef struct
{
	rect_f32 palettes;
	rect_f32 atlas;
	rect_f32 patterns;
	rect_f32 sprites;
}
CHRMapLayout;

typedef enum
{
	CHR_MAP_SELECTION_NONE,
	CHR_MAP_SELECTION_TILE,
	CHR_MAP_SELECTION_SPRITE,
}
CHRMapSelectionKind;

typedef struct
{
	CHRMapSelectionKind kind;
	u32 index;
}
CHRMapSelection;

typedef struct
{
	const NES_TargetSnapshot *publication;
	GFX_Texture *texture;
	CHRMapSelection selection;
	b32 available;
}
CHRMapBoxData;

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

static CHRMapLayout chr_map_layout(rect_f32 rect)
{
	rect_f32 content = rect_f32_inset(rect, 10.f);
	rect_f32 palettes = rect_f32_slice(&content, AXIS_Y, -40.f);
	content.h = Max(0.f, content.h - 8.f);
	f32 scale = Max(0.f, Min(content.w / NES_TARGET_CHR_WIDTH, content.h / NES_TARGET_CHR_HEIGHT));
	rect_f32 atlas = rect_f32_align(content, v2(NES_TARGET_CHR_WIDTH * scale, NES_TARGET_CHR_HEIGHT * scale), v2(0.5f, 0.5f));
	return (CHRMapLayout) {
		.palettes = palettes,
		.atlas = atlas,
		.patterns = { atlas.x, atlas.y, atlas.w, NES_TARGET_CHR_PATTERN_HEIGHT * scale },
		.sprites = { atlas.x, atlas.y + NES_TARGET_CHR_PATTERN_HEIGHT * scale, atlas.w, NES_TARGET_CHR_SPRITE_HEIGHT * scale },
	};
}

static rect_f32 chr_map_tile_rect(CHRMapLayout layout, u32 tile_index)
{
	u32 table = tile_index / NES_PATTERN_TABLE_TILE_COUNT;
	u32 table_tile = tile_index % NES_PATTERN_TABLE_TILE_COUNT;
	u32 tile_x = table_tile % 16;
	u32 tile_y = table_tile / 16;
	f32 tile_extent = layout.patterns.w / 32.f;
	return (rect_f32) {
		.x = layout.patterns.x + (table * 16 + tile_x) * tile_extent,
		.y = layout.patterns.y + tile_y * tile_extent,
		.w = tile_extent,
		.h = tile_extent,
	};
}

static rect_f32 chr_map_sprite_rect(CHRMapLayout layout, const NES_TargetSprite *sprite)
{
	rect_i32 region = sprite->selection_region;
	return (rect_f32) {
		.x = layout.atlas.x + region.x * layout.atlas.w / NES_TARGET_CHR_WIDTH,
		.y = layout.atlas.y + region.y * layout.atlas.h / NES_TARGET_CHR_HEIGHT,
		.w = region.w * layout.atlas.w / NES_TARGET_CHR_WIDTH,
		.h = region.h * layout.atlas.h / NES_TARGET_CHR_HEIGHT,
	};
}

static CHRMapSelection chr_map_selection_from_mouse(UI_Context *ui, const NES_TargetSnapshot *publication, CHRMapLayout layout)
{
	if (rect_f32_contains(layout.patterns, ui->mouse))
	{
		f32 local_x = (ui->mouse.x - layout.patterns.x) * NES_TARGET_CHR_WIDTH / layout.patterns.w;
		f32 local_y = (ui->mouse.y - layout.patterns.y) * NES_TARGET_CHR_PATTERN_HEIGHT / layout.patterns.h;
		u32 table = Min((u32)local_x / 128, 1u);
		u32 tile_x = Min(((u32)local_x % 128) / NES_PATTERN_TILE_SIZE, 15u);
		u32 tile_y = Min((u32)local_y / NES_PATTERN_TILE_SIZE, 15u);
		return (CHRMapSelection) { CHR_MAP_SELECTION_TILE, table * NES_PATTERN_TABLE_TILE_COUNT + tile_y * 16 + tile_x };
	}

	if (rect_f32_contains(layout.sprites, ui->mouse))
	{
		i32 texture_x = (i32)((ui->mouse.x - layout.atlas.x) * NES_TARGET_CHR_WIDTH / layout.atlas.w);
		i32 texture_y = (i32)((ui->mouse.y - layout.atlas.y) * NES_TARGET_CHR_HEIGHT / layout.atlas.h);
		for (u32 index = 0; index < ArrayCount(publication->sprites); ++index)
		{
			rect_i32 region = publication->sprites[index].selection_region;
			if (texture_x >= region.x && texture_x < region.x + region.w && texture_y >= region.y && texture_y < region.y + region.h) {
				return (CHRMapSelection) { CHR_MAP_SELECTION_SPRITE, index };
			}
		}
	}

	return (CHRMapSelection) {};
}

static void chr_map_build_tooltip(ViewFrameData *frame, CHRMapSelection selection)
{
	if (selection.kind == CHR_MAP_SELECTION_NONE) return;
	UI_Context *ui = frame->ui;
	UI_Key key = ui_key_child(UI_KEY("chr map tooltip"), selection.kind);
	UI_Box *tooltip = ui_tooltip_begin(ui, key, ui->mouse);
	if (!tooltip) return;

	UI_TextStyle style = ui->theme.code;
	style.color = ui->theme.text_neutral;
	if (selection.kind == CHR_MAP_SELECTION_TILE)
	{
		Assert(selection.index < ArrayCount(frame->publication->chr_map.mappings));
		u32 table = selection.index / NES_PATTERN_TABLE_TILE_COUNT;
		NES_MapAddr mapped = frame->publication->chr_map.mappings[selection.index];
		ui_text_box_sized(ui, 1, style, LIT("TABLE 1  TILE $FF  PPU $1FF0"), "TABLE %u  TILE $%02X  PPU $%04X", table, selection.index & 0xFF, selection.index << 4);
		ui_text_box_sized(ui, 2, style, LIT("CHR ROM $FFFFFFFF"), "%s $%05X", chr_map_device_name(mapped.device), mapped.address);
	}
	else
	{
		Assert(selection.index < ArrayCount(frame->publication->sprites));
		const NES_TargetSprite *sprite = &frame->publication->sprites[selection.index];
		ui_text_box_sized(ui, 1, style, LIT("OAM $FF  X 255  Y 255  TILE $FF"), "OAM $%02X  X %u  Y %u  TILE $%02X", (u32)sprite->oam_index, (u32)sprite->x, (u32)sprite->y, (u32)sprite->tile);
		ui_text_box_sized(ui, 2, style, LIT("PAL 3  BEHIND BG  FLIP HV"), "PAL %u  %s  FLIP %c%c", (u32)sprite->palette, sprite->behind_background ? "BEHIND BG" : "IN FRONT", sprite->flip_horizontal ? 'H' : '-', sprite->flip_vertical ? 'V' : '-');
		ui_text_box_sized(ui, 3, style, LIT("PPU $FFFF -> CHR ROM $FFFFFFFF"), "PPU $%04X -> %s $%05X", (u32)sprite->ppu_address, chr_map_device_name(sprite->pattern_mapping.device), sprite->pattern_mapping.address);
	}

	ui_tooltip_end(ui);
}

static void chr_map_draw_palettes(UI_Context *ui, const NES_TargetSnapshot *publication, rect_f32 rect)
{
	UI_TextStyle style = ui->theme.code;
	style.color = ui->theme.text_subtle;
	f32 label_width = style.size * 5.f;
	f32 swatch_size = Max(0.f, Min(16.f, rect.h));
	f32 gap = 3.f;
	for (u32 kind = 0; kind < 2; ++kind)
	{
		rect_f32 line = { rect.x, rect.y + kind * (swatch_size + 4.f), rect.w, swatch_size };
		ui_draw_text(ui, line, style, kind ? LIT("SPR") : LIT("BG"));
		f32 x = line.x + label_width;
		for (u32 index = 0; index < 4; ++index)
		{
			const NES_TargetPalette *palette = &publication->palettes[kind * 4 + index];
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

static void chr_map_box_paint(UI_Box *box)
{
	CHRMapBoxData *data = box->content;
	Assert(data);
	Assert(data->texture);
	UI_Context *ui = box->ui;
	ui_push_clip(ui, box->viewport);
	if (!data->available)
	{
		UI_TextStyle style = ui->theme.code;
		style.color = ui->theme.text_subtle;
		ui_draw_text(ui, box->viewport, style, LIT("No cartridge loaded - Ctrl+O to open an iNES ROM"));
		ui_pop_clip(ui);
		return;
	}

	CHRMapLayout layout = chr_map_layout(box->viewport);
	if (layout.atlas.w <= 0.f || layout.atlas.h <= 0.f)
	{
		ui_pop_clip(ui);
		return;
	}
	ui_draw_image(ui, (Draw_TextureParams) {
		.rect = rect_f32_round_out(layout.patterns),
		.texture = data->texture,
		.region = { 0, 0, NES_TARGET_CHR_WIDTH, NES_TARGET_CHR_PATTERN_HEIGHT },
		.tint = COLOR_WHITE,
	});
	ui_draw_rect(ui, rect_f32_round_out(layout.sprites), ui->theme.slider_track);
	ui_draw_image(ui, (Draw_TextureParams) {
		.rect = rect_f32_round_out(layout.sprites),
		.texture = data->texture,
		.region = { 0, NES_TARGET_CHR_PATTERN_HEIGHT, NES_TARGET_CHR_WIDTH, NES_TARGET_CHR_SPRITE_HEIGHT },
		.tint = COLOR_WHITE,
	});
	ui_draw_rect_outline(ui, rect_f32_round_out(layout.patterns), 1.f, ui->theme.panel_outline);
	ui_draw_rect_outline(ui, rect_f32_round_out(layout.sprites), 1.f, ui->theme.panel_outline);
	chr_map_draw_palettes(ui, data->publication, layout.palettes);

	rect_f32 selected = {};
	if (data->selection.kind == CHR_MAP_SELECTION_TILE)
	{
		Assert(data->selection.index < ArrayCount(data->publication->chr_map.mappings));
		selected = chr_map_tile_rect(layout, data->selection.index);
	}
	if (data->selection.kind == CHR_MAP_SELECTION_SPRITE)
	{
		Assert(data->selection.index < ArrayCount(data->publication->sprites));
		selected = chr_map_sprite_rect(layout, &data->publication->sprites[data->selection.index]);
	}
	if (data->selection.kind != CHR_MAP_SELECTION_NONE) {
		ui_draw_rect_outline(ui, rect_f32_round_out(selected), 2.f, ui->theme.text_vibrant);
	}
	ui_pop_clip(ui);
}

static const UI_BoxHooks chr_map_box_hooks = {
	.paint = chr_map_box_paint,
};

void chr_map_view_build_ui(ViewFrameData *frame)
{
	Assert(frame->chr_texture);
	ViewFrameData content = view_begin_frame(frame, LIT("CHR MAP — PATTERN TABLES + OAM SPRITES + PALETTE RAM"));
	CHRMapBoxData *data = arena_push_zero(&frame->ui->frame_arena, sizeof(*data));
	data->publication = frame->publication;
	data->texture = frame->chr_texture;
	data->available = frame->publication->valid && debugger_has_cartridge(frame->debugger);
	if (data->available && content.content_box->has_previous) {
		data->selection = chr_map_selection_from_mouse(frame->ui, frame->publication, chr_map_layout(content.content_box->state->viewport));
	}
	content.content_box->ops = &chr_map_box_hooks;
	content.content_box->content = data;
	chr_map_build_tooltip(&content, data->selection);
	view_end_frame(&content);
}
