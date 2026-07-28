#include "base.h"
#include "ttf_api.h"
#include "text.h"
#include "text_cache.h"

typedef struct
{
	Rune rune;
	u32 byte_count;
}
Text_DecodedRune;

static b32 text__is_utf8_continuation(u8 byte)
{
	return (byte & 0xC0) == 0x80;
}

static Text_DecodedRune text__decode_utf8(const u8 *bytes, u32 remaining)
{
	Text_DecodedRune invalid = { 0xFFFD, 1 };
	if (!remaining) return (Text_DecodedRune) {};
	u8 a = bytes[0];
	if (a < 0x80) {
		return a ? (Text_DecodedRune) { a, 1 } : invalid;
	}
	if (a >= 0xC2 && a <= 0xDF && remaining >= 2 && text__is_utf8_continuation(bytes[1]))
	{
		return (Text_DecodedRune) { ((Rune)(a & 0x1F) << 6) | (Rune)(bytes[1] & 0x3F), 2 };
	}
	if (a >= 0xE0 && a <= 0xEF && remaining >= 3 && text__is_utf8_continuation(bytes[1]) && text__is_utf8_continuation(bytes[2]) && !(a == 0xE0 && bytes[1] < 0xA0) && !(a == 0xED && bytes[1] >= 0xA0))
	{
		return (Text_DecodedRune) { ((Rune)(a & 0x0F) << 12) | ((Rune)(bytes[1] & 0x3F) << 6) | (Rune)(bytes[2] & 0x3F), 3 };
	}
	if (a >= 0xF0 && a <= 0xF4 && remaining >= 4 && text__is_utf8_continuation(bytes[1]) && text__is_utf8_continuation(bytes[2]) && text__is_utf8_continuation(bytes[3]) && !(a == 0xF0 && bytes[1] < 0x90) && !(a == 0xF4 && bytes[1] >= 0x90))
	{
		return (Text_DecodedRune) { ((Rune)(a & 0x07) << 18) | ((Rune)(bytes[1] & 0x3F) << 12) | ((Rune)(bytes[2] & 0x3F) << 6) | (Rune)(bytes[3] & 0x3F), 4 };
	}
	return invalid;
}

Text_Context *text_create(Arena *owner)
{
	return text_cache_create(owner);
}

void text_preload_ascii(Text_Context *text, Font_Handle font, i32 size)
{
	for (Rune rune = 32; rune < 127; ++rune) {
		text_cache_fetch(text, font, size, rune);
	}
}

Text_Layout text_layout(Arena *arena, Text_Context *text, Font_Handle font, i32 size, String string)
{
	Assert(arena);
	Assert(text);
	Assert(font);
	Assert(size > 0);
	prof_add_metric(PROF_METRIC_TEXT_LAYOUT_CALLS, 1);
	prof_add_metric(PROF_METRIC_TEXT_LAYOUT_BYTES, string.size);
	TTF_FontMetrics font_metrics = ttf_font_metrics(font, size);
	Text_Layout result = {
		.glyphs = string.size ? arena_push_zero(arena, sizeof(*result.glyphs) * string.size) : 0,
		.metrics = {
			.ascender = font_metrics.ascender,
			.descender = font_metrics.descender,
			.line_height = font_metrics.line_height,
		},
	};
	if (!string.size) {
		return result;
	}

	f32 line_height = font_metrics.line_height > 0.f ? font_metrics.line_height : (f32)size;
	f32 baseline = font_metrics.ascender;
	f32 pen_x = 0.f;
	f32 maximum_width = 0.f;
	u32 previous_glyph_index = 0;
	u32 line_index = 0;
	for (u32 byte_index = 0; byte_index < string.size;)
	{
		u32 glyph_byte_offset = byte_index;
		Text_DecodedRune decoded = text__decode_utf8((const u8 *)string.data + byte_index, string.size - byte_index);
		Assert(decoded.byte_count > 0);
		byte_index += decoded.byte_count;
		Text_LayoutGlyph *layout_glyph = &result.glyphs[result.glyph_count++];
		*layout_glyph = (Text_LayoutGlyph) {
			.rune = decoded.rune,
			.byte_offset = glyph_byte_offset,
			.byte_count = decoded.byte_count,
			.line_index = line_index,
			.x = pen_x,
			.page = TEXT_PAGE_INVALID,
		};

		if (decoded.rune == '\r' || decoded.rune == '\n')
		{
			if (decoded.rune == '\r' && byte_index < string.size && string.data[byte_index] == '\n')
			{
				++byte_index;
				++layout_glyph->byte_count;
			}
			maximum_width = Max(maximum_width, pen_x);
			pen_x = 0.f;
			previous_glyph_index = 0;
			baseline += line_height;
			++line_index;
			continue;
		}

		if (decoded.rune == '\t')
		{
			Text_CachedGlyph *space = text_cache_fetch(text, font, size, ' ');
			f32 tab_width = Max(space->metrics.advance_x * 4.f, 1.f);
			f32 next_x = (floorf(pen_x / tab_width) + 1.f) * tab_width;
			layout_glyph->advance = next_x - pen_x;
			pen_x = next_x;
			previous_glyph_index = 0;
			continue;
		}

		Text_CachedGlyph *cached = text_cache_fetch(text, font, size, decoded.rune);
		TTF_GlyphMetrics glyph = cached->metrics;
		f32 kerning = ttf_glyph_kerning(font, previous_glyph_index, glyph.glyph_index, size);
		pen_x += kerning;
		layout_glyph->x = pen_x;
		layout_glyph->advance = glyph.advance_x;
		if (cached->page_index < TEXT_CACHE_PAGE_LIMIT)
		{
			layout_glyph->page = cached->page_index;
			layout_glyph->quad.source = cached->uv;
			layout_glyph->quad.destination = (rect_f32) {
				.x = floorf(pen_x + glyph.bearing_x) - cached->padding,
				.y = floorf(baseline - glyph.bearing_y) - cached->padding,
				.w = glyph.bitmap_size.x + cached->padding * 2,
				.h = glyph.bitmap_size.y + cached->padding * 2,
			};
		}
		pen_x += glyph.advance_x;
		previous_glyph_index = glyph.glyph_index;
	}
	maximum_width = Max(maximum_width, pen_x);
	result.metrics.line_height = line_height;
	result.metrics.dim = v2(ceilf(maximum_width), ceilf((line_index + 1) * line_height));
	return result;
}

Text_DrawRun text_make_draw_run(Arena *arena, const Text_Layout *layout)
{
	Assert(arena);
	Assert(layout);
	prof_add_metric(PROF_METRIC_BUILD_TEXT_RUN_CALLS, 1);
	Text_DrawRun result = { .dim = layout->metrics.dim };
	u32 page_counts[TEXT_CACHE_PAGE_LIMIT] = {};
	for (u32 index = 0; index < layout->glyph_count; ++index)
	{
		Text_PageId page = layout->glyphs[index].page;
		if (page < ArrayCount(page_counts)) {
			++page_counts[page];
		}
	}

	Text_DrawPass pass_head = {};
	Text_DrawPass *previous_pass = &pass_head;
	Text_DrawPass *page_passes[TEXT_CACHE_PAGE_LIMIT] = {};
	for (Text_PageId page = 0; page < ArrayCount(page_counts); ++page)
	{
		if (!page_counts[page]) {
			continue;
		}
		Text_DrawPass *pass = arena_push(arena, sizeof(*pass) + sizeof(*pass->quads) * page_counts[page]);
		*pass = (Text_DrawPass) { .page = page };
		page_passes[page] = pass;
		previous_pass = previous_pass->next = pass;
	}
	for (u32 index = 0; index < layout->glyph_count; ++index)
	{
		const Text_LayoutGlyph *glyph = &layout->glyphs[index];
		if (glyph->page < ArrayCount(page_passes))
		{
			Text_DrawPass *pass = page_passes[glyph->page];
			pass->quads[pass->quad_count++] = glyph->quad;
		}
	}
	result.passes = pass_head.next;
	return result;
}

u32 text_raster_page_count(const Text_Context *text)
{
	return text_cache_page_count(text);
}

Text_RasterPage text_raster_page(const Text_Context *text, Text_PageId id)
{
	return text_cache_page(text, id);
}
