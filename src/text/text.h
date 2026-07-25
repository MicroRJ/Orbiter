#ifndef TEXT_H
#define TEXT_H

typedef struct TTF_Font *Font_Handle;
typedef struct Text_Context Text_Context;

typedef u32 Text_PageId;

enum
{
	TEXT_RASTER_PAGE_LIMIT = 16,
	TEXT_PAGE_INVALID = MAX_VALUE_U32,
};

typedef struct
{
	Text_PageId id;
	vec2i size;
	u32 stride;
	const u8 *pixels;
	u64 revision;
}
Text_RasterPage;

typedef UV_Rect Text_DrawQuad;

typedef struct Text_DrawPass Text_DrawPass;
struct Text_DrawPass
{
	Text_DrawPass *next;
	Text_PageId page;
	u32 quad_count;
	Text_DrawQuad quads[];
};

typedef struct
{
	vec2 dim;
	f32  ascender;
	f32  descender;
	f32  line_height;
}
Text_Metrics;

typedef struct
{
	Rune rune;
	u32 byte_offset;
	u32 byte_count;
	u32 line_index;
	f32 x;
	f32 advance;
	Text_PageId page;
	Text_DrawQuad quad;
}
Text_LayoutGlyph;

// Arena-owned, renderer-neutral text geometry. Logical entries include tabs
// and line breaks; only drawable glyphs carry a raster page and quad.
typedef struct
{
	Text_LayoutGlyph *glyphs;
	u32 glyph_count;
	Text_Metrics metrics;
}
Text_Layout;

typedef struct
{
	vec2       dim;
	Text_DrawPass *passes;
}
Text_DrawRun;

Text_Context *text_create(Arena *owner);
// Size is the desired ascender-to-descender line height in pixels.
void text_preload_ascii(Text_Context *text, Font_Handle font, i32 size);
Text_Layout text_layout(Arena *arena, Text_Context *text, Font_Handle font, i32 size, String string);
// Groups an existing layout by raster page without consulting the glyph cache.
Text_DrawRun text_make_draw_run(Arena *arena, const Text_Layout *layout);
u32 text_raster_page_count(const Text_Context *text);
Text_RasterPage text_raster_page(const Text_Context *text, Text_PageId id);

#endif
