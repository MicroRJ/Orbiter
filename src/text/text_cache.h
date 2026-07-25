#ifndef TEXT_CACHE_H
#define TEXT_CACHE_H

enum
{
	TEXT_CACHE_PAGE_LIMIT = TEXT_RASTER_PAGE_LIMIT,
	TEXT_CACHE_GLYPH_PADDING = 1,
};

typedef struct
{
	Font_Handle font;
	i32         size;
	Rune        rune;
}
Text_CacheKey;

typedef struct
{
	TTF_GlyphMetrics metrics;
	u32              page_index;
	UV_Coords        uv;
	i32              padding;
}
Text_CachedGlyph;

typedef struct
{
	Text_CacheKey   key;
	Text_CachedGlyph glyph;
	b32              metrics_ready;
	b32              bitmap_ready;
}
Text_CacheEntry;

typedef struct
{
	u32               usage;
	u32               capacity;
	Text_CacheEntry *entries;
}
Text_CacheTable;

Text_CacheTable text_cache_table_create(Arena *arena, u32 capacity);
Text_CacheEntry *text_cache_table_find_or_insert(Text_CacheTable *table,
	Text_CacheKey key, b32 *inserted);

Text_Context *text_cache_create(Arena *owner);
Text_CachedGlyph *text_cache_metrics(Text_Context *text, Font_Handle font, i32 size, Rune rune);
Text_CachedGlyph *text_cache_fetch(Text_Context *text, Font_Handle font, i32 size, Rune rune);
u32 text_cache_page_count(const Text_Context *text);
Text_RasterPage text_cache_page(const Text_Context *text, Text_PageId id);

#endif
