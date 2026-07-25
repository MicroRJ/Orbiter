#include "base.h"
#include "ttf_api.h"
#include "text.h"
#include "text_cache.h"

enum
{
	TEXT_CACHE_TABLE_CAPACITY = 4096,
	TEXT_ATLAS_SIZE = 1024,
};

typedef struct
{
	Image_r_u8 image;
	vec2i      cursor;
	i32        row_height;
	u64        revision;
}
Text_AtlasPage;

typedef struct
{
	Text_AtlasPage *page;
	u32              page_index;
	vec2i            bitmap_position;
	Image_r_u8       destination;
}
Text_AtlasAllocation;

struct Text_Context
{
	Arena            *arena;
	Text_CacheTable  table;
	Text_AtlasPage   pages[TEXT_CACHE_PAGE_LIMIT];
	u32               page_count;
};

static u32 text_hash_words(const u32 *words, u32 word_count, u32 seed)
{
	prof_add_metric(PROF_METRIC_HASH_FUNC_CALLS, 1);
	prof_add_metric(PROF_METRIC_HASH_BYTES, word_count * sizeof(*words));
	enum { PRIME = 0xc6a4a793 };
	u32 hash = seed ^ (word_count * PRIME);
	for (u32 index = 0; index < word_count; ++index)
	{
		hash += words[index];
		hash *= PRIME;
		hash ^= hash >> 16;
	}
	hash *= PRIME;
	hash ^= hash >> 10;
	hash *= PRIME;
	hash ^= hash >> 17;
	return hash;
}

static b32 text_cache_keys_equal(Text_CacheKey a, Text_CacheKey b)
{
	return a.font == b.font && a.size == b.size && a.rune == b.rune;
}

static u32 text_cache_key_hash(Text_CacheKey key)
{
	// Hash explicit values rather than the raw key struct. Its padding bytes are
	// not part of key identity and are not guaranteed to be initialized.
	u64 pointer = (u64)(uintptr_t)key.font;
	u32 words[] = {
		(u32)pointer,
		(u32)(pointer >> 32),
		(u32)key.size,
		(u32)key.rune,
	};
	return text_hash_words(words, ArrayCount(words), 0xF07CA6E5u);
}

Text_CacheTable text_cache_table_create(Arena *arena, u32 capacity)
{
	Assert(arena);
	Assert(capacity && !(capacity & (capacity - 1)));
	Text_CacheTable result = {
		.capacity = capacity,
		.entries = arena_push_zero(arena, sizeof(*result.entries) * capacity),
	};
	return result;
}

Text_CacheEntry *text_cache_table_find_or_insert(Text_CacheTable *table,
	Text_CacheKey key, b32 *inserted)
{
	Assert(table && table->entries);
	Assert(key.font && key.size > 0 && key.rune != 0);
	if (inserted) *inserted = false;

	u32 mask = table->capacity - 1;
	u32 hash = text_cache_key_hash(key);
	for (u32 miss = 0; miss < table->capacity; ++miss)
	{
		Text_CacheEntry *entry = &table->entries[(hash + miss) & mask];
		if (text_cache_keys_equal(entry->key, key)) return entry;
		if (entry->key.rune == 0)
		{
			entry->key = key;
			entry->glyph.page_index = ~0u;
			++table->usage;
			if (inserted) *inserted = true;
			return entry;
		}
	}

	// A full cache is a valid lookup result, not permission to use an
	// uninitialized slot as the previous implementation did.
	return 0;
}

static Text_AtlasPage *text_cache_create_page(Text_Context *text, u32 *page_index)
{
	Assert(text->page_count < TEXT_CACHE_PAGE_LIMIT);
	*page_index = text->page_count;
	Text_AtlasPage *page = &text->pages[text->page_count++];
	page->image = push_image_r_u8(text->arena, v2i(TEXT_ATLAS_SIZE, TEXT_ATLAS_SIZE));
	page->revision = 1;
	return page;
}

static b32 text_cache_page_can_fit(Text_AtlasPage *page, vec2i slot_size)
{
	if (page->cursor.x + slot_size.x <= page->image.reso.x && page->cursor.y + slot_size.y <= page->image.reso.y)
	{
		return true;
	}
	return page->cursor.y + page->row_height + slot_size.y <= page->image.reso.y;
}

static Text_AtlasAllocation text_cache_allocate_bitmap(Text_Context *text, vec2i bitmap_size)
{
	// A transparent texel around every bitmap lets linear sampling interpolate
	// at glyph edges without pulling coverage from a neighboring atlas entry.
	vec2i slot_size = v2i(bitmap_size.x + TEXT_CACHE_GLYPH_PADDING * 2, bitmap_size.y + TEXT_CACHE_GLYPH_PADDING * 2);
	Assert(slot_size.x <= TEXT_ATLAS_SIZE);
	Assert(slot_size.y <= TEXT_ATLAS_SIZE);

	Text_AtlasPage *page = 0;
	u32 page_index = 0;
	for (u32 index = 0; index < text->page_count; ++index)
	{
		if (text_cache_page_can_fit(&text->pages[index], slot_size))
		{
			page = &text->pages[index];
			page_index = index;
			break;
		}
	}
	if (!page) page = text_cache_create_page(text, &page_index);

	if (page->cursor.x + slot_size.x > page->image.reso.x)
	{
		page->cursor.x = 0;
		page->cursor.y += page->row_height;
		page->row_height = 0;
	}
	Assert(page->cursor.y + slot_size.y <= page->image.reso.y);

	vec2i slot_position = page->cursor;
	page->cursor.x += slot_size.x;
	page->row_height = Max(page->row_height, slot_size.y);
	vec2i bitmap_position = v2i_add(slot_position,
		v2i(TEXT_CACHE_GLYPH_PADDING, TEXT_CACHE_GLYPH_PADDING));
	Image_r_u8 destination = slice_image_r_u8(page->image,
		rect_i32_from_pos_size(bitmap_position, bitmap_size));
	return (Text_AtlasAllocation) {
		.page = page,
		.page_index = page_index,
		.bitmap_position = bitmap_position,
		.destination = destination,
	};
}

static void text_cache_copy_bitmap(Image_r_u8 destination, TTF_Bitmap source)
{
	Assert(destination.reso.x == source.size.x);
	Assert(destination.reso.y == source.size.y);
	Assert(source.stride != 0);
	for (i32 y = 0; y < source.size.y; ++y)
	{
		const u8 *source_row;
		if (source.stride > 0)
		{
			source_row = source.pixels + y * source.stride;
		}
		else
		{
			source_row = source.pixels +
				(source.size.y - 1 - y) * -source.stride;
		}
		memory_copy(destination.data + y * destination.elem_stride,
			source_row, source.size.x);
	}
}

Text_Context *text_cache_create(Arena *owner)
{
	Assert(owner);
	Text_Context *text = arena_push_zero(owner, sizeof(*text));
	text->arena = owner;
	text->table = text_cache_table_create(owner,
		TEXT_CACHE_TABLE_CAPACITY);
	return text;
}

static Text_CacheEntry *text_cache_entry(Text_Context *text, Font_Handle font, i32 size,
	Rune rune)
{
	Text_CacheKey key = {
		.font = font,
		.size = size,
		.rune = rune,
	};
	Text_CacheEntry *entry = text_cache_table_find_or_insert(
		&text->table, key, 0);
	Assert(entry);
	return entry;
}

Text_CachedGlyph *text_cache_metrics(Text_Context *text, Font_Handle font, i32 size, Rune rune)
{
	Text_CacheEntry *entry = text_cache_entry(text, font, size, rune);
	if (!entry->metrics_ready)
	{
		entry->glyph.metrics = ttf_glyph_metrics(font, rune, size);
		entry->metrics_ready = true;
	}
	return &entry->glyph;
}

Text_CachedGlyph *text_cache_fetch(Text_Context *text, Font_Handle font, i32 size, Rune rune)
{
	Text_CacheEntry *entry = text_cache_entry(text, font, size, rune);
	if (entry->bitmap_ready) return &entry->glyph;

	TTF_RasterizedGlyph rasterized = {};
	b32 success = ttf_rasterize_glyph(font, rune, size, &rasterized);
	Assert(success);
	entry->glyph.metrics = rasterized.metrics;
	entry->metrics_ready = true;
	entry->bitmap_ready = true;

	if (rasterized.bitmap.pixels && rasterized.bitmap.size.x > 0 && rasterized.bitmap.size.y > 0)
	{
		Text_AtlasAllocation allocation = text_cache_allocate_bitmap(text, rasterized.bitmap.size);
		text_cache_copy_bitmap(allocation.destination, rasterized.bitmap);
		++ allocation.page->revision;

		f32 inverse_width = 1.0f / allocation.page->image.reso.x;
		f32 inverse_height = 1.0f / allocation.page->image.reso.y;
		vec2i position = allocation.bitmap_position;
		vec2i bitmap_size = rasterized.bitmap.size;
		i32 padding = TEXT_CACHE_GLYPH_PADDING;
		entry->glyph.page_index = allocation.page_index;
		entry->glyph.padding = padding;
		entry->glyph.uv = (UV_Coords) {
			.u0 = (position.x - padding) * inverse_width,
			.v0 = (position.y - padding) * inverse_height,
			.u1 = (position.x + bitmap_size.x + padding) * inverse_width,
			.v1 = (position.y + bitmap_size.y + padding) * inverse_height,
		};
	}
	return &entry->glyph;
}

u32 text_cache_page_count(const Text_Context *text)
{
	Assert(text);
	return text->page_count;
}

Text_RasterPage text_cache_page(const Text_Context *text, Text_PageId id)
{
	Assert(text);
	Assert(id < text->page_count);
	const Text_AtlasPage *page = &text->pages[id];
	return (Text_RasterPage) {
		.id = id,
		.size = page->image.reso,
		.stride = page->image.elem_stride,
		.pixels = page->image.data,
		.revision = page->revision,
	};
}
