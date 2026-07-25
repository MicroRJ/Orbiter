#include "freetype/freetype.h"

struct TTF_Font
{
	FT_Face face;
	i32     line_height;
};

static struct
{
	FT_Library library;
}
g_freetype;

static b32 ttf_freetype_set_size(TTF_Font *font, i32 line_height)
{
	Assert(font);
	Assert(line_height > 0);
	if (font->line_height == line_height) return true;

	// The frontend size is the desired ascender-to-descender line box.
	// FT_Set_Pixel_Sizes instead sizes the font's abstract em square; IBM Plex
	// has a real vertical span about 1.35 times its em, so that made a 20-pixel
	// UI font occupy a 27-pixel line. REAL_DIM maps the face's actual ascender
	// and descender to the requested height while keeping atlas geometry out of
	// the sizing contract.
	FT_Size_RequestRec request = {
		.type = FT_SIZE_REQUEST_TYPE_REAL_DIM,
		.width = 0,
		.height = (FT_Long)line_height * 64,
		.horiResolution = 0,
		.vertResolution = 0,
	};
	FT_Error error = FT_Request_Size(font->face, &request);
	if (error) return false;
	font->line_height = line_height;
	return true;
}

static b32 ttf_freetype_load_rendered_glyph(TTF_Font *font, Rune rune,
	i32 line_height, TTF_RasterizedGlyph *result)
{
	Assert(result);
	memory_zero(result, sizeof(*result));
	if (!ttf_freetype_set_size(font, line_height)) return false;

	FT_UInt glyph_index = FT_Get_Char_Index(font->face, rune);
	FT_Error error = FT_Load_Glyph(font->face, glyph_index, FT_LOAD_DEFAULT);
	if (error) return false;
	error = FT_Render_Glyph(font->face->glyph, FT_RENDER_MODE_NORMAL);
	if (error) return false;

	FT_GlyphSlot slot = font->face->glyph;
	FT_Bitmap *bitmap = &slot->bitmap;
	result->metrics.glyph_index = glyph_index;
	result->metrics.bitmap_size = v2i((i32)bitmap->width, (i32)bitmap->rows);
	result->metrics.bearing_x = slot->bitmap_left;
	result->metrics.bearing_y = slot->bitmap_top;
	result->metrics.advance_x = slot->advance.x / 64.0f;
	result->bitmap.size = result->metrics.bitmap_size;
	result->bitmap.stride = bitmap->pitch;
	result->bitmap.pixels = bitmap->buffer;
	return true;
}

void ttf_init_api(void)
{
	if (g_freetype.library) return;
	FT_Error error = FT_Init_FreeType(&g_freetype.library);
	Assert(error == 0);
}

TTF_Font *ttf_load(String contents)
{
	Assert(contents.size);
	Assert(contents.data);
	Assert(g_freetype.library);

	TTF_Font *font = calloc(1, sizeof(*font));
	Assert(font);
	FT_Error error = FT_New_Memory_Face(g_freetype.library,
		(const FT_Byte *)contents.data, (FT_Long)contents.size, 0, &font->face);
	if (error)
	{
		free(font);
		return 0;
	}
	return font;
}

TTF_FontMetrics ttf_font_metrics(TTF_Font *font, i32 line_height)
{
	TTF_FontMetrics result = {};
	if (!ttf_freetype_set_size(font, line_height)) return result;

	// Hinted ascender and descender are rounded independently: a REAL_DIM
	// request for 20 pixels can consequently report a 21-pixel span. Layout
	// needs the exact requested line box, so place its baseline using the face's
	// unrounded proportions. Glyph bearings and advances remain hinted.
	f32 face_span = (f32)(font->face->ascender - font->face->descender);
	if (face_span > 0.0f)
	{
		result.ascender = line_height * font->face->ascender / face_span;
		result.descender = result.ascender - line_height;
	}
	result.line_height = (f32)line_height;
	return result;
}

TTF_GlyphMetrics ttf_glyph_metrics(TTF_Font *font, Rune rune, i32 line_height)
{
	TTF_RasterizedGlyph glyph = {};
	ttf_freetype_load_rendered_glyph(font, rune, line_height, &glyph);
	return glyph.metrics;
}

b32 ttf_rasterize_glyph(TTF_Font *font, Rune rune, i32 line_height,
	TTF_RasterizedGlyph *result)
{
	return ttf_freetype_load_rendered_glyph(font, rune, line_height, result);
}

f32 ttf_glyph_kerning(TTF_Font *font, u32 left_glyph_index,
	u32 right_glyph_index, i32 line_height)
{
	if (!left_glyph_index || !right_glyph_index ||
		!FT_HAS_KERNING(font->face) ||
		!ttf_freetype_set_size(font, line_height))
	{
		return 0.0f;
	}

	FT_Vector delta = {};
	FT_Error error = FT_Get_Kerning(font->face, left_glyph_index,
		right_glyph_index, FT_KERNING_DEFAULT, &delta);
	return error ? 0.0f : delta.x / 64.0f;
}
