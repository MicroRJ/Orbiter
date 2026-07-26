#include "base.h"
#include "graphics.h"
#include "layout.h"
#include "os.h"
#include "text.h"
#include "text_gfx.h"
#include "ttf_api.h"
#include "widgets.h"

typedef struct
{
	Color_SRGBA color;
	b32 show_size;
}
PlaygroundVisual;

typedef struct
{
	f32 outer_padding;
	f32 gap;
	f32 card_padding;
}
PlaygroundDensity;

typedef enum
{
	PLAYGROUND_MODE_BASICS,
	PLAYGROUND_MODE_PROFILER,
	PLAYGROUND_MODE_COUNT,
}
PlaygroundMode;

typedef struct
{
	UIP_Box *root;
	UIP_Box *scroll_boxes[4];
	u32 scroll_box_count;
}
PlaygroundScene;

typedef struct
{
	f32 offset;
	f32 target;
	f32 drag_offset;
	f32 drag_mouse;
}
PlaygroundScroll;

typedef struct
{
	rect_f32 track;
	rect_f32 thumb;
	rect_f32 hit_rect;
	rect_f32 thumb_hit_rect;
	f32 max_scroll;
	f32 travel;
}
PlaygroundScrollbar;

static const u64 PLAYGROUND_ID_SCROLLBAR_TRACK = 0x5343524F4C4C0001ull;
static const u64 PLAYGROUND_ID_SCROLLBAR_THUMB = 0x5343524F4C4C0002ull;

static const PlaygroundDensity playground_densities[] = {
	{ 8.f,  8.f,  8.f },
	{ 18.f, 14.f, 14.f },
	{ 30.f, 22.f, 22.f },
};

static f32 playground_smooth_scroll(f32 offset, f32 target, f32 elapsed)
{
	f32 half_life = 0.055f;
	return target + (offset - target) * exp2f(-Max(elapsed, 0.f) / half_life);
}

static UI_Id playground_scrollbar_track_id(UIP_Box *box)
{
	return ui_id_child(box->id, PLAYGROUND_ID_SCROLLBAR_TRACK);
}

static UI_Id playground_scrollbar_thumb_id(UIP_Box *box)
{
	return ui_id_child(box->id, PLAYGROUND_ID_SCROLLBAR_THUMB);
}

static PlaygroundScrollbar playground_scrollbar(UIP_Box *box)
{
	PlaygroundScrollbar result = {};
	result.max_scroll = box->scroll_max.y - box->scroll_min.y;
	if (result.max_scroll <= 0.001f || box->viewport.h <= 0.f) return result;

	f32 content_height = Max(box->content_size.y, box->viewport.h);
	f32 thumb_height = Max(24.f, box->viewport.h * box->viewport.h / content_height);
	result.track = (rect_f32) {
		.x = box->viewport.x + box->viewport.w - 5.f,
		.y = box->viewport.y,
		.w = 4.f,
		.h = box->viewport.h,
	};
	result.travel = Max(0.f, result.track.h - thumb_height);
	f32 ratio = (box->scroll_offset.y - box->scroll_min.y) / result.max_scroll;
	result.thumb = (rect_f32) {
		.x = result.track.x,
		.y = result.track.y + result.travel * ratio,
		.w = result.track.w,
		.h = thumb_height,
	};
	result.hit_rect = (rect_f32) {
		.x = result.track.x - 8.f,
		.y = result.track.y,
		.w = result.track.w + 8.f,
		.h = result.track.h,
	};
	result.thumb_hit_rect = result.thumb;
	result.thumb_hit_rect.x = result.hit_rect.x;
	result.thumb_hit_rect.w = result.hit_rect.w;
	return result;
}

static String playground_read_file(Arena *arena, const char *path)
{
	String result = {};
	Platform_File file = platform_access_file(path, PLATFORM_FILE_OPEN_EXISTING, PLATFORM_FILE_READ | PLATFORM_FILE_SHARE_READ);
	if (!platform_file_is_valid(file)) {
		return result;
	}
	u64 size = 0;
	if (platform_get_file_size(file, &size) && size <= MAX_VALUE_U32)
	{
		u8 *data = arena_push(arena, size + 1);
		u64 bytes_read = 0;
		if (platform_read_file(file, data, size, &bytes_read) && bytes_read == size)
		{
			data[size] = 0;
			result = string_from_data(data, (u32)size);
		}
	}
	platform_close_file(file);
	return result;
}

static PlaygroundVisual *playground_visual(Arena *arena, Color_SRGBA color, b32 show_size)
{
	PlaygroundVisual *visual = arena_push_zero(arena, sizeof(*visual));
	visual->color = color;
	visual->show_size = show_size;
	return visual;
}

static UIP_Box *playground_make_box(UIP_Builder *builder, u64 key, String name, UIP_BoxDesc desc, Color_SRGBA color, b32 show_size)
{
	UIP_Box *box = uip_make_box(builder, key, name, desc);
	box->user = playground_visual(builder->arena, color, show_size);
	return box;
}

static UIP_Box *playground_begin_box(UIP_Builder *builder, u64 key, String name, UIP_BoxDesc desc, Color_SRGBA color, b32 show_size)
{
	UIP_Box *box = uip_begin_box(builder, key, name, desc);
	box->user = playground_visual(builder->arena, color, show_size);
	return box;
}

static UIP_BoxDesc playground_fill_desc(void)
{
	UIP_BoxDesc desc = uip_box_desc();
	desc.size[AXIS_X] = uip_fill(1.f);
	desc.size[AXIS_Y] = uip_fill(1.f);
	return desc;
}

typedef struct
{
	Color_SRGBA violet;
	Color_SRGBA slate;
	UIP_TextStyle title_style;
	UIP_TextStyle subtitle_style;
}
PlaygroundProfilerRows;

static void playground_build_profiler_row(UIP_Builder *builder, u32 row, void *user)
{
	PlaygroundProfilerRows *rows = user;
	UIP_BoxDesc metric = playground_fill_desc();
	metric.axis = AXIS_X;
	metric.size[AXIS_Y] = uip_pixels(76.f);
	metric.horz_padd[0] = metric.horz_padd[1] = 8.f;
	metric.vert_padd[0] = metric.vert_padd[1] = 8.f;
	metric.gap = 10.f;
	playground_begin_box(builder, 1, LIT(""), metric, color_srgba_mix(rows->violet, rows->slate, 0.58f), false);

	UIP_BoxDesc swatch = uip_box_desc();
	swatch.size[AXIS_X] = uip_pixels(44.f);
	swatch.size[AXIS_Y] = uip_pixels(44.f);
	swatch.perp_align = 0.5f;
	f32 swatch_mix = (f32)(row % 7) / 6.f;
	playground_make_box(builder, 1, LIT(""), swatch, color_srgba_mix(rows->violet, color_srgba(0x18B8A4), swatch_mix), false);

	UIP_BoxDesc text_stack = playground_fill_desc();
	text_stack.axis = AXIS_Y;
	text_stack.gap = 4.f;
	uip_begin_box(builder, 2, LIT(""), text_stack);

	String title =
		row == 0 ? LIT("Frame time") :
		row == 1 ? LIT("Application") :
		row == 2 ? LIT("Rendering") :
		row == 3 ? LIT("Present wait") :
		row == 4 ? LIT("Other") :
			push_formatted(builder->arena, "Profiler scope %05u", row + 1);
	UIP_BoxDesc title_box = playground_fill_desc();
	title_box.size[AXIS_Y] = uip_pixels(28.f);
	uip_text(builder, 1, title, title_box, rows->title_style);

	String subtitle =
		row == 0 ? LIT("16.67 ms  |  complete frame") :
		row == 1 ? LIT("3.82 ms  |  application work") :
		row == 2 ? LIT("2.14 ms  |  render submission") :
		row == 3 ? LIT("7.35 ms  |  swapchain wait") :
		row == 4 ? LIT("3.36 ms  |  uncategorized") :
			push_formatted(builder->arena, "%.2f ms  |  %u calls", 0.01f * (f32)(row % 300), 1 + row % 97);
	UIP_BoxDesc subtitle_box = playground_fill_desc();
	subtitle_box.size[AXIS_Y] = uip_pixels(28.f);
	uip_text(builder, 2, subtitle, subtitle_box, rows->subtitle_style);

	uip_end_box(builder);
	uip_end_box(builder);
}

static PlaygroundScene playground_build_basics(Arena *arena, UIP_Context *ui, Font_Handle font, PlaygroundDensity density)
{
	PlaygroundScene scene = {};
	Color_SRGBA teal = color_srgba(0x18B8A4);
	Color_SRGBA blue = color_srgba(0x3478F6);
	Color_SRGBA violet = color_srgba(0x9558E8);
	Color_SRGBA amber = color_srgba(0xE5A83C);
	Color_SRGBA slate = color_srgba(0x25343A);

	UIP_BoxDesc root_desc = playground_fill_desc();
	root_desc.axis = AXIS_Y;
	root_desc.horz_padd[0] = root_desc.horz_padd[1] = density.outer_padding;
	root_desc.vert_padd[0] = root_desc.vert_padd[1] = density.outer_padding;
	root_desc.gap = density.gap;

	UIP_Builder builder;
	UIP_Box *root = uip_builder_begin(&builder, arena, ui, 1, LIT("root"), root_desc);

	UIP_BoxDesc header = playground_fill_desc();
	header.axis = AXIS_X;
	header.size[AXIS_Y] = uip_pixels(48.f);
	header.horz_padd[0] = header.horz_padd[1] = 18.f;
	header.vert_padd[0] = header.vert_padd[1] = 8.f;
	header.gap = 8.f;
	playground_begin_box(&builder, 1, LIT(""), header, slate, false);
	UIP_BoxDesc status_text = uip_box_desc();
	status_text.perp_align = 0.5f;
	UIP_TextStyle vibrant = { .font = font, .size = 16, .color = color_srgba(0x18B8A4) };
	UIP_TextStyle subtle = { .font = font, .size = 16, .color = color_srgba(0x8EAAA5) };
	UIP_TextStyle running = { .font = font, .size = 16, .color = amber };
	uip_text(&builder, 1, LIT("ORBITER"), status_text, vibrant);
	uip_text(&builder, 2, LIT("|  UI BOX PLAYGROUND  |"), status_text, subtle);
	uip_text(&builder, 3, LIT("RUNNING"), status_text, running);
	UIP_BoxDesc status_spacer = playground_fill_desc();
	uip_make_box(&builder, 4, LIT(""), status_spacer);
	subtle.align.x = 1.f;
	uip_text_sized(&builder, 5, LIT("60.0 FPS  |  FRAME 123456"), LIT("999.9 FPS  |  FRAME 9999999999"), status_text, subtle);
	uip_end_box(&builder);

	UIP_BoxDesc laboratory = playground_fill_desc();
	laboratory.axis = AXIS_X;
	laboratory.gap = density.gap;
	laboratory.vert_padd[0] = 32.f;
	playground_begin_box(&builder, 2, LIT("layout laboratory"), laboratory, slate, false);

	UIP_BoxDesc navigation = playground_fill_desc();
	navigation.size[AXIS_X] = uip_flex(0.f, 3.f);
	navigation.min_size.x = 120.f;
	navigation.max_size.x = 310.f;
	navigation.horz_padd[0] = navigation.horz_padd[1] = density.card_padding;
	navigation.vert_padd[0] = 48.f;
	navigation.vert_padd[1] = density.card_padding;
	navigation.gap = 8.f;

	UIP_Box *navigation_box = playground_begin_box(&builder, 10, LIT("CONTENT BASIS 270  |  SHRINK 3"), navigation, teal, true);
	navigation_box->intrinsic_size.x = 270.f;

	for (u32 row = 0; row < 4; ++row)
	{
		UIP_BoxDesc item = playground_fill_desc();
		item.size[AXIS_Y] = uip_pixels(38.f);
		playground_make_box(&builder, 11 + row, row == 0 ? LIT("Overview") : row == 1 ? LIT("Call tree") : row == 2 ? LIT("Flame graph") : LIT("Memory"), item, color_srgba_mix(teal, slate, 0.45f), false);
	}
	uip_end_box(&builder);

	UIP_BoxDesc timeline = playground_fill_desc();
	timeline.size[AXIS_X] = uip_fill(2.f);
	timeline.min_size.x = 160.f;
	timeline.horz_padd[0] = timeline.horz_padd[1] = density.card_padding;
	timeline.vert_padd[0] = 48.f;
	timeline.vert_padd[1] = density.card_padding;
	timeline.gap = 10.f;
	playground_begin_box(&builder, 20, LIT("ZERO BASIS  |  GROW 2"), timeline, blue, true);

	UIP_BoxDesc chart = playground_fill_desc();
	chart.axis = AXIS_X;
	chart.gap = 7.f;
	playground_begin_box(&builder, 21, LIT("weighted children"), chart, color_srgba_mix(blue, slate, 0.55f), false);
	for (u32 bar = 0; bar < 5; ++bar)
	{
		UIP_BoxDesc bar_desc = playground_fill_desc();
		bar_desc.size[AXIS_X] = uip_fill((f32)(bar + 1));
		bar_desc.size[AXIS_Y] = uip_pixels(54.f + 22.f * bar);
		bar_desc.perp_align = 1.f;
		playground_make_box(&builder, 22 + bar, LIT(""), bar_desc, color_srgba_mix(blue, violet, (f32)bar / 5.f), false);
	}
	uip_end_box(&builder);

	UIP_BoxDesc timeline_status = playground_fill_desc();
	timeline_status.size[AXIS_Y] = uip_pixels(36.f);
	playground_make_box(&builder, 28, LIT("1x  2x  3x  4x  5x grow weights"), timeline_status, color_srgba_mix(blue, slate, 0.35f), false);
	uip_end_box(&builder);

	UIP_BoxDesc inspector = playground_fill_desc();
	inspector.size[AXIS_X] = uip_flex(1.f, 1.f);
	inspector.min_size.x = 180.f;
	inspector.max_size.x = 420.f;
	inspector.horz_padd[0] = inspector.horz_padd[1] = density.card_padding;
	inspector.vert_padd[0] = 48.f;
	inspector.vert_padd[1] = density.card_padding;
	inspector.gap = 8.f;
	inspector.overflow[AXIS_X] = UIP_OVERFLOW_CLIP;
	inspector.overflow[AXIS_Y] = UIP_OVERFLOW_SCROLL;
	PlaygroundProfilerRows *profiler_rows = arena_push_zero(arena, sizeof(*profiler_rows));
	profiler_rows->violet = violet;
	profiler_rows->slate = slate;
	profiler_rows->title_style = (UIP_TextStyle) { .font = font, .size = 16, .color = color_srgba(0xD6E7E4) };
	profiler_rows->subtitle_style = (UIP_TextStyle) { .font = font, .size = 14, .color = color_srgba(0x8EAAA5) };
	UIP_Box *inspector_box = uip_make_virtual_list(&builder, 30, LIT("VIRTUAL 10,000 ROWS  |  CONTENT BASIS 300"), inspector, (UIP_VirtualListDesc) {
		.item_count = 10000,
		.user = profiler_rows,
		.build_item = playground_build_profiler_row,
	});
	inspector_box->user = playground_visual(arena, violet, true);
	inspector_box->intrinsic_size.x = 300.f;
	scene.scroll_boxes[scene.scroll_box_count++] = inspector_box;

	uip_end_box(&builder);

	UIP_BoxDesc footer = playground_fill_desc();
	footer.size[AXIS_Y] = uip_pixels(42.f);
	footer.horz_padd[0] = footer.horz_padd[1] = 14.f;
	footer.vert_padd[0] = footer.vert_padd[1] = 8.f;
	playground_make_box(&builder, 40, LIT("Mouse-wheel the purple card. Hit testing and painting share current-frame clips."), footer, color_srgba_mix(amber, slate, 0.55f), false);

	scene.root = uip_builder_end(&builder);
	return scene;
}

typedef struct
{
	Font_Handle font;
	Color_SRGBA slate;
	Color_SRGBA teal;
	Color_SRGBA violet;
	Color_SRGBA error;
	UIP_TextStyle header_style;
	UIP_TextStyle value_style;
	UIP_TextStyle numeric_style;
}
PlaygroundDummyProfiler;

static UIP_BoxDesc playground_dummy_text_cell(b32 fill)
{
	UIP_BoxDesc desc = uip_box_desc();
	desc.size[AXIS_X] = fill ? uip_fill(1.f) : uip_content();
	desc.size[AXIS_Y] = uip_fill(1.f);
	return desc;
}

static void playground_build_dummy_timing_row(UIP_Builder *builder, PlaygroundDummyProfiler *profiler, u32 row, b32 header)
{
	static const String scope_names[] = {
		LIT("main frame incl wait"),
		LIT("debug stepping"),
		LIT("emulation"),
		LIT("audio buffering"),
		LIT("audio"),
		LIT("snapshot"),
		LIT("program refinement"),
		LIT("application"),
		LIT("application draw"),
		LIT("present wait"),
		LIT("frame pacing"),
	};
	UIP_BoxDesc row_desc = playground_fill_desc();
	row_desc.axis = AXIS_X;
	row_desc.size[AXIS_Y] = uip_pixels(32.f);
	row_desc.horz_padd[0] = row_desc.horz_padd[1] = 8.f;
	row_desc.gap = 10.f;
	Color_SRGBA row_color = header ? profiler->teal : color_srgba_mix(profiler->slate, profiler->violet, row & 1 ? 0.12f : 0.20f);
	playground_begin_box(builder, header ? 2000 : 3000, LIT(""), row_desc, row_color, false);

	UIP_TextStyle label_style = header ? profiler->header_style : profiler->value_style;
	UIP_TextStyle numeric_style = header ? profiler->header_style : profiler->numeric_style;
	numeric_style.align.x = 1.f;
	String scope = header ? LIT("SCOPE") : scope_names[row % ArrayCount(scope_names)];
	String milliseconds = header ? LIT("MS") : push_formatted(builder->arena, "%.3f", 0.025f + (f32)(row % 400) * 0.013f);
	String frame_pct = header ? LIT("FRAME %") : push_formatted(builder->arena, "%.1f", 0.2f + (f32)(row % 97));
	String calls = header ? LIT("CALLS") : push_formatted(builder->arena, "%u", 1 + row % 99999);
	String per_call = header ? LIT("US/CALL") : push_formatted(builder->arena, "%.2f", 0.1f + (f32)(row % 1200) * 0.07f);
	uip_text(builder, 1, scope, playground_dummy_text_cell(true), label_style);
	uip_text_sized(builder, 2, milliseconds, LIT("999.999"), playground_dummy_text_cell(false), numeric_style);
	uip_text_sized(builder, 3, frame_pct, LIT("100.0 %"), playground_dummy_text_cell(false), numeric_style);
	uip_text_sized(builder, 4, calls, LIT("999999"), playground_dummy_text_cell(false), numeric_style);
	uip_text_sized(builder, 5, per_call, LIT("99999.99"), playground_dummy_text_cell(false), numeric_style);
	uip_end_box(builder);
}

static void playground_build_dummy_timing_item(UIP_Builder *builder, u32 item_index, void *user)
{
	playground_build_dummy_timing_row(builder, user, item_index, false);
}

static void playground_build_dummy_metric_row(UIP_Builder *builder, PlaygroundDummyProfiler *profiler, u32 row, b32 header)
{
	static const String metric_names[] = {
		LIT("TEXT LAYOUT CALLS"),
		LIT("TEXT DRAW RUNS"),
		LIT("BOXES BUILT"),
		LIT("VISIBLE VIRTUAL ROWS"),
		LIT("DRAW COMMANDS"),
		LIT("GLYPH CACHE HITS"),
		LIT("GLYPH CACHE MISSES"),
		LIT("TRANSIENT TEXTURES"),
	};
	UIP_BoxDesc row_desc = playground_fill_desc();
	row_desc.axis = AXIS_X;
	row_desc.size[AXIS_Y] = uip_pixels(32.f);
	row_desc.horz_padd[0] = row_desc.horz_padd[1] = 8.f;
	row_desc.gap = 10.f;
	Color_SRGBA row_color = header ? profiler->violet : color_srgba_mix(profiler->slate, profiler->teal, row & 1 ? 0.10f : 0.18f);
	playground_begin_box(builder, header ? 4000 : 5000, LIT(""), row_desc, row_color, false);

	UIP_TextStyle label_style = header ? profiler->header_style : profiler->value_style;
	UIP_TextStyle numeric_style = header ? profiler->header_style : profiler->numeric_style;
	numeric_style.align.x = 1.f;
	String metric = header ? LIT("METRIC") : metric_names[row % ArrayCount(metric_names)];
	String value = header ? LIT("VALUE") : push_formatted(builder->arena, "%llu", 1000ull + (u64)row * 7919ull);
	uip_text(builder, 1, metric, playground_dummy_text_cell(true), label_style);
	uip_text_sized(builder, 2, value, LIT("9999999999"), playground_dummy_text_cell(false), numeric_style);
	uip_end_box(builder);
}

static void playground_build_dummy_metric_item(UIP_Builder *builder, u32 item_index, void *user)
{
	playground_build_dummy_metric_row(builder, user, item_index, false);
}

static PlaygroundScene playground_build_dummy_profiler(Arena *arena, UIP_Context *ui, Font_Handle font, PlaygroundDensity density)
{
	PlaygroundScene scene = {};
	Color_SRGBA teal = color_srgba(0x18B8A4);
	Color_SRGBA violet = color_srgba(0x9558E8);
	Color_SRGBA amber = color_srgba(0xE5A83C);
	Color_SRGBA slate = color_srgba(0x25343A);
	PlaygroundDummyProfiler *profiler = arena_push_zero(arena, sizeof(*profiler));
	profiler->font = font;
	profiler->slate = slate;
	profiler->teal = teal;
	profiler->violet = violet;
	profiler->error = color_srgba(0xE65B65);
	profiler->header_style = (UIP_TextStyle) { .font = font, .size = 14, .color = color_srgba(0xD6E7E4), .align = v2(0.f, 0.5f) };
	profiler->value_style = (UIP_TextStyle) { .font = font, .size = 14, .color = color_srgba(0xAFC5C1), .align = v2(0.f, 0.5f) };
	profiler->numeric_style = profiler->value_style;

	UIP_BoxDesc root_desc = playground_fill_desc();
	root_desc.axis = AXIS_Y;
	root_desc.horz_padd[0] = root_desc.horz_padd[1] = density.outer_padding;
	root_desc.vert_padd[0] = root_desc.vert_padd[1] = density.outer_padding;
	root_desc.gap = density.gap;
	UIP_Builder builder;
	UIP_Box *root = uip_builder_begin(&builder, arena, ui, 2, LIT("dummy profiler"), root_desc);

	UIP_BoxDesc header = playground_fill_desc();
	header.axis = AXIS_X;
	header.size[AXIS_Y] = uip_pixels(48.f);
	header.horz_padd[0] = header.horz_padd[1] = 16.f;
	header.vert_padd[0] = header.vert_padd[1] = 8.f;
	header.gap = 8.f;
	playground_begin_box(&builder, 6000, LIT(""), header, slate, false);
	UIP_BoxDesc header_text = uip_box_desc();
	header_text.perp_align = 0.5f;
	UIP_TextStyle title = { .font = font, .size = 16, .color = teal };
	UIP_TextStyle subtle = { .font = font, .size = 14, .color = color_srgba(0x8EAAA5) };
	uip_text(&builder, 1, LIT("ORBITER PROFILER"), header_text, title);
	uip_text(&builder, 2, LIT("|  BOX TABLE PROTOTYPE"), header_text, subtle);
	UIP_BoxDesc header_spacer = playground_fill_desc();
	uip_make_box(&builder, 3, LIT(""), header_spacer);
	subtle.align.x = 1.f;
	uip_text(&builder, 4, LIT("TAB: BASICS  |  SPACE: DENSITY"), header_text, subtle);
	uip_end_box(&builder);

	UIP_BoxDesc graph = playground_fill_desc();
	graph.size[AXIS_Y] = uip_flex(1.f, 1.f);
	graph.min_size.y = 128.f;
	graph.max_size.y = 240.f;
	UIP_Box *graph_box = playground_make_box(&builder, 6001, LIT("DUMMY FRAME PHASE GRAPH  |  custom rendering stays inside this rectangle"), graph, color_srgba_mix(teal, slate, 0.58f), false);
	graph_box->intrinsic_size.y = 184.f;

	UIP_BoxDesc selection = playground_fill_desc();
	selection.axis = AXIS_X;
	selection.size[AXIS_Y] = uip_pixels(34.f);
	selection.horz_padd[0] = selection.horz_padd[1] = 10.f;
	playground_begin_box(&builder, 6002, LIT(""), selection, color_srgba_mix(amber, slate, 0.72f), false);
	UIP_BoxDesc selection_text = uip_box_desc();
	selection_text.perp_align = 0.5f;
	uip_text(&builder, 1, LIT("SELECTED FRAME 123456  /  16.667 MS"), selection_text, profiler->value_style);
	UIP_BoxDesc selection_spacer = playground_fill_desc();
	uip_make_box(&builder, 2, LIT(""), selection_spacer);
	UIP_TextStyle live = profiler->header_style;
	live.color = amber;
	live.align.x = 1.f;
	uip_text(&builder, 3, LIT("LIVE"), selection_text, live);
	uip_end_box(&builder);

	UIP_BoxDesc tables = playground_fill_desc();
	tables.axis = AXIS_X;
	tables.gap = density.gap;
	uip_begin_box(&builder, 6003, LIT(""), tables);

	UIP_BoxDesc timing_panel = playground_fill_desc();
	timing_panel.axis = AXIS_Y;
	timing_panel.size[AXIS_X] = uip_fill(1.6f);
	timing_panel.horz_padd[0] = timing_panel.horz_padd[1] = density.card_padding;
	timing_panel.vert_padd[0] = timing_panel.vert_padd[1] = density.card_padding;
	timing_panel.gap = 4.f;
	playground_begin_box(&builder, 6100, LIT(""), timing_panel, slate, false);
	playground_build_dummy_timing_row(&builder, profiler, 0, true);
	UIP_BoxDesc timing_list = playground_fill_desc();
	timing_list.gap = 1.f;
	timing_list.overflow[AXIS_X] = UIP_OVERFLOW_CLIP;
	UIP_Box *timing_scroll = uip_make_virtual_list(&builder, 2, LIT(""), timing_list, (UIP_VirtualListDesc) {
		.item_count = 4096,
		.user = profiler,
		.build_item = playground_build_dummy_timing_item,
	});
	scene.scroll_boxes[scene.scroll_box_count++] = timing_scroll;
	uip_end_box(&builder);

	UIP_BoxDesc metric_panel = playground_fill_desc();
	metric_panel.axis = AXIS_Y;
	metric_panel.size[AXIS_X] = uip_fill(1.f);
	metric_panel.horz_padd[0] = metric_panel.horz_padd[1] = density.card_padding;
	metric_panel.vert_padd[0] = metric_panel.vert_padd[1] = density.card_padding;
	metric_panel.gap = 4.f;
	playground_begin_box(&builder, 6200, LIT(""), metric_panel, slate, false);
	playground_build_dummy_metric_row(&builder, profiler, 0, true);
	UIP_BoxDesc metric_list = playground_fill_desc();
	metric_list.gap = 1.f;
	metric_list.overflow[AXIS_X] = UIP_OVERFLOW_CLIP;
	UIP_Box *metric_scroll = uip_make_virtual_list(&builder, 2, LIT(""), metric_list, (UIP_VirtualListDesc) {
		.item_count = 2048,
		.user = profiler,
		.build_item = playground_build_dummy_metric_item,
	});
	scene.scroll_boxes[scene.scroll_box_count++] = metric_scroll;
	uip_end_box(&builder);

	uip_end_box(&builder);
	scene.root = uip_builder_end(&builder);
	return scene;
}

static void playground_draw_outline(Draw_Context *draw, rect_f32 rect, f32 thickness, Color_SRGBA color)
{
	draw_rect(draw, (Draw_RectParams) {
		.rect = { rect.x, rect.y, rect.w, thickness },
		.color = color,
	});
	draw_rect(draw, (Draw_RectParams) {
		.rect = { rect.x, rect.y + rect.h - thickness, rect.w, thickness },
		.color = color,
	});
	draw_rect(draw, (Draw_RectParams) {
		.rect = { rect.x, rect.y, thickness, rect.h },
		.color = color,
	});
	draw_rect(draw, (Draw_RectParams) {
		.rect = { rect.x + rect.w - thickness, rect.y, thickness, rect.h },
		.color = color,
	});
}

static void playground_draw_tree(Arena *arena, Draw_Context *draw, Text_Context *text, Text_GFX *text_gfx, Font_Handle font, UIP_Box *box, UIP_Box *hot, UI_Id selected_id)
{
	PlaygroundVisual *visual = box->user;
	if (visual)
	{
		draw_push_clip(draw, box->clip_rect);
		Color_SRGBA fill = color_with_alpha(visual->color, 0.24f);
		draw_rect(draw, (Draw_RectParams) {
			.rect = box->rect,
			.color = fill,
			.corner_radii = { 5.f, 5.f, 5.f, 5.f },
			.edge_softness = 1.f,
		});

		b32 hovered = box == hot;
		b32 selected = ui_id_equal(box->id, selected_id);
		Color_SRGBA outline = color_with_alpha(visual->color, selected ? 1.f : hovered ? 0.82f : 0.38f);
		playground_draw_outline(draw, box->rect, selected ? 3.f : hovered ? 2.f : 1.f, outline);

		if (box->name.size && box->rect.w > 24.f && box->rect.h > 20.f)
		{
			String label = box->name;
			if (visual->show_size) {
				label = push_formatted(arena, "%.*s  |  %.0f px", box->name.size, box->name.text, box->rect.w);
			}
			Text_Layout layout = text_layout(arena, text, font, 16, label);
			Text_DrawRun run = text_make_draw_run(arena, &layout);
			draw_push_clip(draw, box->rect);
			text_gfx_draw_run(text_gfx, draw, run, v2(box->rect.x + 10.f, box->rect.y + 9.f), color_srgba(0xD6E7E4));
			draw_pop_clip(draw);
		}
		draw_pop_clip(draw);
	}

	if (box->ops && box->ops->paint)
	{
		box->ops->paint(box);
	}

	for (u32 child_index = 0; child_index < box->child_count; ++child_index) {
		playground_draw_tree(arena, draw, text, text_gfx, font, box->children[child_index], hot, selected_id);
	}
}

static void playground_draw_scrollbar(Draw_Context *draw, UIP_Box *box)
{
	PlaygroundScrollbar scrollbar = playground_scrollbar(box);
	if (scrollbar.max_scroll <= 0.f) return;
	draw_push_clip(draw, box->clip_rect);
	draw_rect(draw, (Draw_RectParams) { .rect = scrollbar.track, .color = color_with_alpha(color_srgba(0x9558E8), 0.18f) });
	draw_rect(draw, (Draw_RectParams) { .rect = scrollbar.thumb, .color = color_with_alpha(color_srgba(0xC99CFF), 0.85f) });
	draw_pop_clip(draw);
}

static b32 playground_near(f32 a, f32 b)
{
	return fabsf(a - b) < 0.01f;
}

static UIP_Box *playground_test_row(Arena *arena, f32 width, UIP_Size left_size, f32 left_basis, f32 left_min, f32 left_max, UIP_Size right_size, f32 right_basis, f32 right_min, f32 right_max)
{
	UIP_BoxDesc root_desc = playground_fill_desc();
	root_desc.axis = AXIS_X;
	UIP_Builder builder;
	UIP_Box *root = uip_builder_begin(&builder, arena, 0, 1, LIT("root"), root_desc);

	UIP_BoxDesc left = playground_fill_desc();
	left.size[AXIS_X] = left_size;
	left.min_size.x = left_min;
	left.max_size.x = left_max;
	UIP_Box *left_box = uip_make_box(&builder, 1, LIT("left"), left);
	left_box->intrinsic_size.x = left_basis;

	UIP_BoxDesc right = playground_fill_desc();
	right.size[AXIS_X] = right_size;
	right.min_size.x = right_min;
	right.max_size.x = right_max;
	UIP_Box *right_box = uip_make_box(&builder, 2, LIT("right"), right);
	right_box->intrinsic_size.x = right_basis;

	uip_builder_end(&builder);
	uip_measure(root, (UIP_Constraints) { .max = v2(width, 100.f) });
	uip_layout(root, (rect_f32) { 0.f, 0.f, width, 100.f });
	return root;
}

static void playground_build_test_virtual_item(UIP_Builder *builder, u32 item_index, void *user)
{
	(void)item_index;
	(void)user;
	UIP_BoxDesc row = uip_box_desc();
	row.axis = AXIS_X;
	row.size[AXIS_X] = uip_fill(1.f);
	row.size[AXIS_Y] = uip_pixels(42.f);
	uip_begin_box(builder, 1, LIT("row"), row);
	UIP_BoxDesc child = uip_box_desc();
	child.size[AXIS_X] = uip_fill(1.f);
	child.size[AXIS_Y] = uip_fill(1.f);
	uip_make_box(builder, 1, LIT("nested child"), child);
	uip_end_box(builder);
}

static int playground_run_tests(void)
{
	Arena arena = arena_create(0, "UI playground tests");
	u32 failures = 0;

#define CHECK(condition, label) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL: %s\n", (label)); \
		failures++; \
	} \
} while (0)

	{
		f32 coarse = playground_smooth_scroll(0.f, 100.f, 1.f / 30.f);
		f32 fine = 0.f;
		for (u32 step = 0; step < 4; step ++) {
			fine = playground_smooth_scroll(fine, 100.f, 1.f / 120.f);
		}
		CHECK(playground_near(coarse, fine), "smooth scrolling is invariant to frame subdivision");
	}

	{
		UIP_Box list = {
			.viewport = { 0.f, 0.f, 100.f, 100.f },
			.content_size = v2(100.f, 400.f),
			.scroll_offset = v2(0.f, 150.f),
			.scroll_max = v2(0.f, 300.f),
		};
		PlaygroundScrollbar scrollbar = playground_scrollbar(&list);
		CHECK(playground_near(scrollbar.thumb.h, 25.f) && playground_near(scrollbar.thumb.y, 37.5f), "scrollbar thumb geometry maps the logical scroll range onto track travel");
		CHECK(scrollbar.hit_rect.w > scrollbar.track.w && scrollbar.thumb_hit_rect.w == scrollbar.hit_rect.w, "scrollbar interaction uses a wider target than its painted track");
	}

	ARENA_SCOPE(&arena)
	{
		UIP_BoxDesc desc = uip_box_desc();
		UIP_Builder builder;
		UIP_Box *root = uip_builder_begin(&builder, &arena, 0, 1, LIT("root"), desc);
		UIP_Box *a = uip_begin_box(&builder, 1, LIT("a"), desc);
		UIP_Box *b = uip_make_box(&builder, 1, LIT("b"), desc);
		UIP_Box *c = uip_make_box(&builder, 2, LIT("c"), desc);
		uip_end_box(&builder);
		UIP_Box *d = uip_make_box(&builder, 2, LIT("d"), desc);
		uip_builder_end(&builder);
		CHECK(root->child_count == 2 && root->children[0] == a && root->children[1] == d, "builder stores root children contiguously and in order");
		CHECK(a->child_count == 2 && a->children[0] == b && a->children[1] == c, "builder stores nested children contiguously and in order");
		CHECK(root->id.value && ui_id_equal(a->id, ui_id_child(root->id, 1)) && ui_id_equal(b->id, ui_id_child(a->id, 1)), "box IDs derive from their structural parent and construction key");
		CHECK(!ui_id_equal(b->id, d->id), "the same local key in a different structural scope produces a different ID");
	}

	ARENA_SCOPE(&arena)
	{
		UIP_BoxDesc desc = uip_box_desc();
		UIP_Builder builder;
		UIP_Box *root = uip_builder_begin(&builder, &arena, 0, 1, LIT("root"), desc);
		uip_push_id(&builder, 100);
		UIP_Box *first = uip_make_box(&builder, 1, LIT("first"), desc);
		uip_pop_id(&builder);
		uip_push_id(&builder, 200);
		UIP_Box *second = uip_make_box(&builder, 1, LIT("second"), desc);
		uip_pop_id(&builder);
		uip_builder_end(&builder);
		CHECK(root->child_count == 2 && !ui_id_equal(first->id, second->id), "explicit ID scopes disambiguate repeated component-local keys");
	}

	ARENA_SCOPE(&arena)
	{
		UIP_Box *root = playground_test_row(&arena, 400.f, uip_flex(0.f, 3.f), 300.f, 0.f, UIP_INFINITY, uip_flex(0.f, 1.f), 300.f, 0.f, UIP_INFINITY);
		CHECK(playground_near(root->children[0]->rect.w, 150.f) && playground_near(root->children[1]->rect.w, 250.f), "negative space follows shrink weights");
	}

	ARENA_SCOPE(&arena)
	{
		UIP_BoxDesc desc = uip_box_desc();
		desc.axis = AXIS_X;
		UIP_Builder builder;
		UIP_Box *root = uip_builder_begin(&builder, &arena, 0, 1, LIT("root"), desc);
		root->intrinsic_size.x = 270.f;
		UIP_Box *child = uip_make_box(&builder, 1, LIT("child"), desc);
		child->intrinsic_size.x = 100.f;
		uip_builder_end(&builder);
		uip_measure(root, (UIP_Constraints) { .max = v2(UIP_INFINITY, UIP_INFINITY) });
		CHECK(playground_near(root->measured_size.x, 270.f), "intrinsic basis remains independent from child content");
	}

	ARENA_SCOPE(&arena)
	{
		UIP_Box *root = playground_test_row(&arena, 300.f, uip_flex(0.f, 3.f), 300.f, 200.f, UIP_INFINITY, uip_flex(0.f, 1.f), 300.f, 0.f, UIP_INFINITY);
		CHECK(playground_near(root->children[0]->rect.w, 200.f) && playground_near(root->children[1]->rect.w, 100.f), "shrink deficit redistributes after a child reaches its minimum");
	}

	ARENA_SCOPE(&arena)
	{
		UIP_Box *root = playground_test_row(&arena, 400.f, uip_fill(1.f), 0.f, 0.f, 100.f, uip_fill(1.f), 0.f, 0.f, UIP_INFINITY);
		CHECK(playground_near(root->children[0]->rect.w, 100.f) && playground_near(root->children[1]->rect.w, 300.f), "grow surplus redistributes after a child reaches its maximum");
	}

	ARENA_SCOPE(&arena)
	{
		UIP_BoxDesc root_desc = playground_fill_desc();
		root_desc.axis = AXIS_X;
		root_desc.gap = 20.f;
		UIP_Builder builder;
		UIP_Box *root = uip_builder_begin(&builder, &arena, 0, 1, LIT("root"), root_desc);

		UIP_BoxDesc fixed = playground_fill_desc();
		fixed.size[AXIS_X] = uip_pixels(100.f);
		fixed.horz_margin[0] = 10.f;
		fixed.horz_margin[1] = 10.f;
		uip_make_box(&builder, 1, LIT("fixed"), fixed);

		UIP_BoxDesc fill = playground_fill_desc();
		fill.size[AXIS_X] = uip_fill(1.f);
		uip_make_box(&builder, 2, LIT("fill"), fill);

		uip_builder_end(&builder);
		uip_measure(root, (UIP_Constraints) { .max = v2(400.f, 100.f) });
		uip_layout(root, (rect_f32) { 0.f, 0.f, 400.f, 100.f });
		CHECK(playground_near(root->children[1]->rect.w, 260.f), "margins and gaps are deducted before distributing free space");
	}

	ARENA_SCOPE(&arena)
	{
		UIP_Box *root = playground_test_row(&arena, 100.f, uip_flex(0.f, 1.f), 200.f, 80.f, UIP_INFINITY, uip_flex(0.f, 1.f), 200.f, 80.f, UIP_INFINITY);
		CHECK(playground_near(root->children[0]->rect.w, 80.f) && playground_near(root->children[1]->rect.w, 80.f), "unsatisfied deficit stops at child minimums without negative sizes");
	}

	ARENA_SCOPE(&arena)
	{
		UIP_BoxDesc root_desc = uip_box_desc();
		UIP_Builder builder;
		uip_builder_begin(&builder, &arena, 0, 1, LIT("root"), root_desc);
		UIP_BoxDesc list_desc = uip_box_desc();
		list_desc.gap = 8.f;
		UIP_Box *list = uip_make_virtual_list(&builder, 1, LIT("list"), list_desc, (UIP_VirtualListDesc) {
			.item_count = 1,
			.build_item = playground_build_test_virtual_item,
		});
		uip_builder_end(&builder);
		uip_measure(list, (UIP_Constraints) { .max = v2(100.f, 100.f) });
		uip_layout(list, rect_f32_from_size(list->measured_size));
		CHECK(playground_near(list->measured_size.y, 42.f) && playground_near(list->content_size.y, 42.f), "a short virtual list wraps its logical items");
		CHECK(list->child_count == 1 && list->children[0]->child_count == 1, "a virtual item materializes as an arbitrary box subtree");
		CHECK(ui_id_equal(list->children[0]->id, ui_id_child(ui_id_child(list->id, 0), 1)), "a virtual item ID includes its logical item scope");
	}

	ARENA_SCOPE(&arena)
	{
		UIP_BoxDesc root_desc = uip_box_desc();
		UIP_Builder builder;
		uip_builder_begin(&builder, &arena, 0, 1, LIT("root"), root_desc);
		UIP_BoxDesc list_desc = uip_box_desc();
		list_desc.gap = 8.f;
		UIP_Box *list = uip_make_virtual_list(&builder, 1, LIT("list"), list_desc, (UIP_VirtualListDesc) {
			.item_count = 1000,
			.build_item = playground_build_test_virtual_item,
		});
		uip_builder_end(&builder);
		uip_measure(list, (UIP_Constraints) { .max = v2(100.f, 100.f) });
		uip_layout(list, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		CHECK(playground_near(list->measured_size.y, 100.f) && playground_near(list->content_size.y, 49992.f), "a long virtual list clamps while preserving its full logical extent");
		CHECK(list->child_count == 4 && list->virtual_list.first_item == 0 && list->virtual_list.one_past_item == 4, "a virtual list materializes only visible and overscan items");
		list->scroll_offset.y = 500.f;
		uip_relayout(list);
		CHECK(list->child_count == 6 && list->children[0]->virtual_index == 8 && list->children[5]->virtual_index == 13, "scrolling rematerializes the correct logical item range");
		CHECK(ui_id_equal(list->children[0]->id, ui_id_child(ui_id_child(list->id, 8), 1)), "rematerialized virtual items retain deterministic IDs");
	}

	ARENA_SCOPE(&arena)
	{
		UIP_BoxDesc root_desc = playground_fill_desc();
		root_desc.axis = AXIS_Y;
		root_desc.overflow[AXIS_Y] = UIP_OVERFLOW_SCROLL;
		UIP_Builder builder;
		UIP_Box *root = uip_builder_begin(&builder, &arena, 0, 1, LIT("scroll"), root_desc);

		UIP_BoxDesc content = playground_fill_desc();
		content.size[AXIS_Y] = uip_pixels(200.f);
		UIP_Box *child = uip_make_box(&builder, 1, LIT("content"), content);
		uip_builder_end(&builder);
		uip_measure(root, (UIP_Constraints) { .min = v2(100.f, 100.f), .max = v2(100.f, 100.f) });
		uip_layout(root, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		root->scroll_offset.y = 500.f;
		uip_relayout(root);
		CHECK(playground_near(root->content_size.y, 200.f) && playground_near(root->scroll_max.y, 100.f), "layout computes content extent and scroll range");
		CHECK(playground_near(root->scroll_offset.y, 100.f) && playground_near(child->rect.y, -100.f), "layout clamps scroll and places descendant geometry at its scrolled position");
		CHECK(uip_find_deepest(root, v2(50.f, 50.f)) == child && !uip_find_deepest(root, v2(50.f, 150.f)), "hit testing uses translated geometry and the effective clip");
	}

#undef CHECK

	arena_destroy(&arena);
	if (failures) {
		fprintf(stderr, "%u UI playground test(s) failed\n", failures);
		return 1;
	}
	printf("UI playground: all layout tests passed\n");
	return 0;
}

static int playground_run_window(void)
{
	if (!os_init() || !os_graphical_init())
	{
		fprintf(stderr, "failed to initialize the graphical OS layer\n");
		return 1;
	}

	Arena arena = arena_create(0, "UI playground");
	Arena frame_arena = arena_create(0, "UI playground frame");
	ttf_init_api();
	Font_Handle font = ttf_load(playground_read_file(&arena, "data/fonts/Saira/static/Saira-Medium.ttf"));
	if (!font)
	{
		fprintf(stderr, "failed to load the playground font\n");
		return 1;
	}

	OS_Window *window = os_window_create((OS_WindowDesc) {
		.title = "Orbiter UI Playground - resize me",
		.size = v2i(1180, 720),
		.title_bar = {
			.enabled = true,
			.dark = true,
			.background_rgb = 0x050A0C,
			.text_rgb = 0x8EAAA5,
			.border_rgb = 0x45645E,
		},
	});
	if (!window)
	{
		fprintf(stderr, "failed to create the playground window\n");
		return 1;
	}

	GFX_Renderer *renderer = gfx_renderer_create(&arena);
	GFX_Window *gfx_window = gfx_window_create(&arena, renderer, window);
	Draw_Context *draw = draw_create(&arena, renderer);
	Text_Context *text = text_create(&arena);
	Text_GFX *text_gfx = text_gfx_create(&arena, renderer, text);
	UIP_Context ui = { .draw = draw, .text = text, .text_gfx = text_gfx };
	text_preload_ascii(text, font, 14);
	text_preload_ascii(text, font, 16);

	vec2i previous_size = {};
	u32 density_index = 1;
	UI_Id selected_id = UI_ID_NONE;
	UI_Id active_scrollbar = UI_ID_NONE;
	PlaygroundMode mode = PLAYGROUND_MODE_BASICS;
	PlaygroundScroll scrolls[PLAYGROUND_MODE_COUNT][4] = {};
	Seconds previous_frame_time = seconds_now();
	while (os_window_is_open(window))
	{
		os_graphical_poll();
		Seconds frame_time = seconds_now();
		f32 elapsed = (f32)Max(frame_time.seconds - previous_frame_time.seconds, 0.0);
		previous_frame_time = frame_time;
		if (!os_window_is_open(window)) {
			break;
		}
		if (window->keys[OS_Key_Space] & OS_KEY_PRESSED) {
			density_index = (density_index + 1) % ArrayCount(playground_densities);
		}
		if (window->keys[OS_Key_Tab] & OS_KEY_PRESSED) {
			mode = (mode + 1) % PLAYGROUND_MODE_COUNT;
			active_scrollbar = UI_ID_NONE;
		}
		if (window->keys[OS_Key_Backspace] & OS_KEY_PRESSED) {
			selected_id = UI_ID_NONE;
		}
		if (window->size.x <= 0 || window->size.y <= 0) {
			continue;
		}

		if (window->size.x != previous_size.x || window->size.y != previous_size.y)
		{
			gfx_window_resize(gfx_window, window->size);
			previous_size = window->size;
		}

		ARENA_SCOPE(&frame_arena)
		{
			PlaygroundScene scene = mode == PLAYGROUND_MODE_BASICS ? playground_build_basics(&frame_arena, &ui, font, playground_densities[density_index]) : playground_build_dummy_profiler(&frame_arena, &ui, font, playground_densities[density_index]);
			for (u32 scroll_index = 0; scroll_index < scene.scroll_box_count; scroll_index ++) {
				scene.scroll_boxes[scroll_index]->scroll_offset.y = scrolls[mode][scroll_index].offset;
			}
			vec2 size = v2_from_v2i(window->size);
			uip_measure(scene.root, (UIP_Constraints) { .min = size, .max = size });
			uip_layout(scene.root, rect_f32_from_size(size));

			vec2 mouse = v2_from_v2i(window->mouse_position);
			for (u32 scroll_index = 0; scroll_index < scene.scroll_box_count; scroll_index ++)
			{
				UIP_Box *scroll_box = scene.scroll_boxes[scroll_index];
				PlaygroundScroll *scroll = &scrolls[mode][scroll_index];
				scroll->offset = scroll_box->scroll_offset.y;
				scroll->target = CLAMP(scroll->target, scroll_box->scroll_min.y, scroll_box->scroll_max.y);
			}
			if (window->mouse_wheel.y)
			{
				for (u32 scroll_index = 0; scroll_index < scene.scroll_box_count; scroll_index ++)
				{
					UIP_Box *scroll_box = scene.scroll_boxes[scroll_index];
					if (rect_f32_contains(scroll_box->viewport, mouse))
					{
						PlaygroundScroll *scroll = &scrolls[mode][scroll_index];
						scroll->target -= window->mouse_wheel.y * 48.f;
						break;
					}
				}
			}
			OS_KeyState mouse_left = window->keys[OS_Key_MouseLeft];
			b32 scrollbar_hovered = false;
			b32 mouse_press_consumed = false;
			if ((mouse_left & OS_KEY_PRESSED) && !active_scrollbar.value)
			{
				for (u32 scroll_index = scene.scroll_box_count; scroll_index > 0; scroll_index --)
				{
					UIP_Box *scroll_box = scene.scroll_boxes[scroll_index - 1];
					PlaygroundScroll *scroll = &scrolls[mode][scroll_index - 1];
					PlaygroundScrollbar scrollbar = playground_scrollbar(scroll_box);
					if (scrollbar.max_scroll <= 0.f || !rect_f32_contains(scrollbar.hit_rect, mouse)) continue;

					mouse_press_consumed = true;
					if (rect_f32_contains(scrollbar.thumb_hit_rect, mouse))
					{
						active_scrollbar = playground_scrollbar_thumb_id(scroll_box);
						scroll->drag_offset = scroll->offset;
						scroll->drag_mouse = mouse.y;
					}
					else
					{
						active_scrollbar = playground_scrollbar_track_id(scroll_box);
						f32 direction = mouse.y < scrollbar.thumb.y ? -1.f : 1.f;
						scroll->target += direction * scroll_box->viewport.h * 0.85f;
					}
					break;
				}
			}

			b32 active_scrollbar_found = !active_scrollbar.value;
			for (u32 scroll_index = 0; scroll_index < scene.scroll_box_count; scroll_index ++)
			{
				UIP_Box *scroll_box = scene.scroll_boxes[scroll_index];
				PlaygroundScroll *scroll = &scrolls[mode][scroll_index];
				PlaygroundScrollbar scrollbar = playground_scrollbar(scroll_box);
				UI_Id track_id = playground_scrollbar_track_id(scroll_box);
				UI_Id thumb_id = playground_scrollbar_thumb_id(scroll_box);
				scrollbar_hovered |= scrollbar.max_scroll > 0.f && rect_f32_contains(scrollbar.hit_rect, mouse);
				if (ui_id_equal(active_scrollbar, track_id)) {
					active_scrollbar_found = true;
				}
				if (ui_id_equal(active_scrollbar, thumb_id))
				{
					active_scrollbar_found = true;
					if ((mouse_left & (OS_KEY_DOWN | OS_KEY_RELEASED)) && scrollbar.travel > 0.f)
					{
						f32 mouse_delta = mouse.y - scroll->drag_mouse;
						scroll->offset = CLAMP(scroll->drag_offset + mouse_delta * scrollbar.max_scroll / scrollbar.travel, scroll_box->scroll_min.y, scroll_box->scroll_max.y);
						scroll->target = scroll->offset;
						scroll_box->scroll_offset.y = scroll->offset;
						uip_relayout(scroll_box);
					}
				}
			}
			if (!active_scrollbar_found) {
				active_scrollbar = UI_ID_NONE;
			}
			for (u32 scroll_index = 0; scroll_index < scene.scroll_box_count; scroll_index ++)
			{
				UIP_Box *scroll_box = scene.scroll_boxes[scroll_index];
				PlaygroundScroll *scroll = &scrolls[mode][scroll_index];
				scroll->target = CLAMP(scroll->target, scroll_box->scroll_min.y, scroll_box->scroll_max.y);
				b32 dragging = ui_id_equal(active_scrollbar, playground_scrollbar_thumb_id(scroll_box));
				f32 offset = dragging ? scroll->offset : playground_smooth_scroll(scroll->offset, scroll->target, elapsed);
				if (fabsf(offset - scroll->offset) > 0.001f)
				{
					scroll_box->scroll_offset.y = offset;
					uip_relayout(scroll_box);
					scroll->offset = scroll_box->scroll_offset.y;
				}
			}
			UIP_Box *hot = uip_find_deepest(scene.root, mouse);
			if (scrollbar_hovered || active_scrollbar.value || (hot && !hot->user)) {
				hot = 0;
			}
			if (hot && (mouse_left & OS_KEY_PRESSED) && !mouse_press_consumed) {
				selected_id = hot->id;
			}
			if (mouse_left & OS_KEY_RELEASED) {
				active_scrollbar = UI_ID_NONE;
			}

			gfx_begin_frame(draw);
			gfx_begin_pass(draw, (GFX_PassDesc) {
				.output = gfx_window_texture(gfx_window),
				.clear = true,
				.clear_color = color_srgba(0x071013),
			});
			playground_draw_tree(&frame_arena, draw, text, text_gfx, font, scene.root, hot, selected_id);
			for (u32 scroll_index = 0; scroll_index < scene.scroll_box_count; scroll_index ++) {
				playground_draw_scrollbar(draw, scene.scroll_boxes[scroll_index]);
			}
			gfx_end_pass(draw);
			text_gfx_sync(text_gfx);
			gfx_end_frame(draw);
			gfx_window_present(gfx_window);
		}
	}

	os_window_destroy(window);
	arena_destroy(&frame_arena);
	arena_destroy(&arena);
	os_graphical_shutdown();
	os_shutdown();
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--test") == 0) {
		return playground_run_tests();
	}
	if (argc != 1)
	{
		fprintf(stderr, "usage: %s [--test]\n", argv[0]);
		return 2;
	}
	return playground_run_window();
}
