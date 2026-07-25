#ifndef FONTS_TTF_API_H
#define FONTS_TTF_API_H

#include "base.h"

typedef struct TTF_Font TTF_Font;

typedef struct
{
	f32 ascender;
	f32 descender;
	f32 line_height;
}
TTF_FontMetrics;

typedef struct
{
	u32   glyph_index;
	vec2i bitmap_size;
	i32   bearing_x;
	i32   bearing_y;
	f32   advance_x;
}
TTF_GlyphMetrics;

typedef struct
{
	vec2i     size;
	i32       stride;
	const u8 *pixels;
}
TTF_Bitmap;

typedef struct
{
	TTF_GlyphMetrics metrics;
	TTF_Bitmap       bitmap;
}
TTF_RasterizedGlyph;

void ttf_init_api(void);
TTF_Font *ttf_load(String contents);
// Size is the desired ascender-to-descender line height in pixels. Backend
// concepts such as em size and atlas allocation dimensions are not exposed.
TTF_FontMetrics ttf_font_metrics(TTF_Font *font, i32 line_height);
TTF_GlyphMetrics ttf_glyph_metrics(TTF_Font *font, Rune rune, i32 line_height);
b32 ttf_rasterize_glyph(TTF_Font *font, Rune rune, i32 line_height, TTF_RasterizedGlyph *result);
f32 ttf_glyph_kerning(TTF_Font *font, u32 left_glyph_index, u32 right_glyph_index, i32 line_height);

#endif
