#include "base.h"
#include "graphics.h"
#include "os.h"
#include "text.h"
#include "text_gfx.h"
#include "ttf_api.h"
#include "ui_box.h"
#include "ui_widgets.h"

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
	PLAYGROUND_MODE_SCROLL_HISTORY,
	PLAYGROUND_MODE_COUNT,
}
PlaygroundMode;

typedef struct
{
	b32 valid;
	rect_f32 viewport;
	rect_f32 track;
	rect_f32 track_viewport;
	rect_f32 thumb;
	vec2 content_size;
	vec2 scroll_min;
	vec2 scroll_max;
	u64 layout_generation;
}
PlaygroundScrollGeometry;

typedef struct
{
	f32 offset;
	f32 target;
	f32 drag_offset;
	f32 drag_mouse;
	PlaygroundScrollGeometry previous;
}
PlaygroundScroll;

typedef struct
{
	UI_Box *root;
	UI_Box *viewport;
	UI_Box *track;
	UI_Box *space_before;
	UI_Box *thumb;
	UI_Box *space_after;
}
PlaygroundScrollArea;

typedef struct
{
	UI_Box *root;
	PlaygroundScrollArea scroll_areas[4];
	u32 scroll_area_count;
}
PlaygroundScene;

typedef struct
{
	f32 max_scroll;
	f32 travel;
}
PlaygroundScrollbar;

static const PlaygroundDensity playground_densities[] = {
	{ 8.f,  8.f,  8.f },
	{ 18.f, 14.f, 14.f },
	{ 30.f, 22.f, 22.f },
};

static const u64 PLAYGROUND_SCROLLBAR_TRACK_KEY = 0x5343524F4C4C4241ull;

static f32 playground_smooth_scroll(f32 offset, f32 target, f32 elapsed)
{
	f32 half_life = 0.055f;
	return target + (offset - target) * exp2f(-Max(elapsed, 0.f) / half_life);
}

static PlaygroundScrollbar playground_scrollbar(PlaygroundScrollArea *area)
{
	PlaygroundScrollbar result = {};
	result.max_scroll = area->viewport->scroll_max.y - area->viewport->scroll_min.y;
	result.travel = Max(0.f, area->track->viewport.h - area->thumb->rect.h);
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

static UI_Box *playground_make_box(UI_Context *ui, u64 key, String name, UI_BoxDesc desc, Color_SRGBA color, b32 show_size)
{
	UI_Box *box = ui_box_make_desc(ui, key, name, desc);
	box->user = playground_visual(&ui->frame_arena, color, show_size);
	return box;
}

static UI_Box *playground_begin_box(UI_Context *ui, u64 key, String name, UI_BoxDesc desc, Color_SRGBA color, b32 show_size)
{
	UI_Box *box = ui_box_begin_desc(ui, key, name, desc);
	box->user = playground_visual(&ui->frame_arena, color, show_size);
	return box;
}

static UI_BoxDesc playground_fill_desc(void)
{
	UI_BoxDesc desc = ui_box_desc();
	desc.size[AXIS_X] = ui_box_fill(1.f);
	desc.size[AXIS_Y] = ui_box_fill(1.f);
	return desc;
}

static PlaygroundScrollArea playground_scroll_area_begin(UI_Context *ui, u64 key, UI_BoxDesc desc)
{
	desc.axis = AXIS_X;
	PlaygroundScrollArea area = { .root = ui_box_begin_desc(ui, key, LIT("scroll area"), desc) };
	return area;
}

static void playground_scroll_area_end(UI_Context *ui, PlaygroundScrollArea *area, UI_Box *viewport, Color_SRGBA track_color, Color_SRGBA thumb_color)
{
	Assert(area);
	Assert(viewport);
	area->viewport = viewport;

	UI_BoxDesc track = playground_fill_desc();
	track.axis = AXIS_Y;
	track.size[AXIS_X] = ui_box_pixels(12.f);
	track.horz_padd[0] = track.horz_padd[1] = 3.f;
	area->track = playground_begin_box(ui, PLAYGROUND_SCROLLBAR_TRACK_KEY, LIT(""), track, track_color, false);

	UI_BoxDesc piece = playground_fill_desc();
	piece.size[AXIS_Y] = ui_box_fill(0.f);
	area->space_before = ui_box_make_desc(ui, 1, LIT(""), piece);
	piece.size[AXIS_Y] = ui_box_fill(1.f);
	area->thumb = playground_make_box(ui, 2, LIT(""), piece, thumb_color, false);
	piece.size[AXIS_Y] = ui_box_fill(0.f);
	area->space_after = ui_box_make_desc(ui, 3, LIT(""), piece);
	ui_box_end(ui);
	ui_box_end(ui);
}

static void playground_size_scrollbar(PlaygroundScrollArea *area, f32 track_height, f32 viewport_height, f32 content_height, f32 scroll_min, f32 scroll_max, f32 scroll_offset)
{
	f32 max_scroll = scroll_max - scroll_min;
	f32 thumb_height = track_height;
	f32 thumb_offset = 0.f;
	if (max_scroll > 0.001f && content_height > 0.f)
	{
		thumb_height = Min(Max(24.f, track_height * viewport_height / content_height), track_height);
		f32 travel = Max(0.f, track_height - thumb_height);
		f32 ratio = (scroll_offset - scroll_min) / max_scroll;
		thumb_offset = travel * CLAMP(ratio, 0.f, 1.f);
	}

	area->space_before->desc.size[AXIS_Y] = ui_box_fill(thumb_offset);
	area->thumb->desc.size[AXIS_Y] = ui_box_fill(thumb_height);
	area->space_after->desc.size[AXIS_Y] = ui_box_fill(Max(track_height - thumb_offset - thumb_height, 0.f));
}

static void playground_prepare_scrollbar(PlaygroundScrollArea *area, PlaygroundScroll *scroll)
{
	PlaygroundScrollGeometry *previous = &scroll->previous;
	if (!previous->valid) return;
	playground_size_scrollbar(area, previous->track_viewport.h, previous->viewport.h, Max(previous->content_size.y, previous->viewport.h), previous->scroll_min.y, previous->scroll_max.y, scroll->offset);
}

static void playground_layout_scrollbar(PlaygroundScrollArea *area)
{
	UI_Box *viewport = area->viewport;
	playground_size_scrollbar(area, area->track->viewport.h, viewport->viewport.h, Max(viewport->content_size.y, viewport->viewport.h), viewport->scroll_min.y, viewport->scroll_max.y, viewport->scroll_offset.y);
	ui_box_relayout(area->track);
}

static void playground_capture_scrollbar(PlaygroundScrollArea *area, PlaygroundScroll *scroll, u64 layout_generation)
{
	scroll->previous = (PlaygroundScrollGeometry) {
		.valid = true,
		.viewport = area->viewport->viewport,
		.track = area->track->rect,
		.track_viewport = area->track->viewport,
		.thumb = area->thumb->rect,
		.content_size = area->viewport->content_size,
		.scroll_min = area->viewport->scroll_min,
		.scroll_max = area->viewport->scroll_max,
		.layout_generation = layout_generation,
	};
}

typedef struct
{
	Color_SRGBA violet;
	Color_SRGBA slate;
	UI_TextStyle title_style;
	UI_TextStyle subtitle_style;
}
PlaygroundProfilerRows;

static void playground_build_profiler_row(UI_Context *ui, u32 row, void *user)
{
	PlaygroundProfilerRows *rows = user;
	UI_BoxDesc metric = playground_fill_desc();
	metric.axis = AXIS_X;
	metric.size[AXIS_Y] = ui_box_pixels(76.f);
	metric.horz_padd[0] = metric.horz_padd[1] = 8.f;
	metric.vert_padd[0] = metric.vert_padd[1] = 8.f;
	metric.gap = 10.f;
	playground_begin_box(ui, 1, LIT(""), metric, color_srgba_mix(rows->violet, rows->slate, 0.58f), false);

	UI_BoxDesc swatch = ui_box_desc();
	swatch.size[AXIS_X] = ui_box_pixels(44.f);
	swatch.size[AXIS_Y] = ui_box_pixels(44.f);
	swatch.perp_align = 0.5f;
	f32 swatch_mix = (f32)(row % 7) / 6.f;
	playground_make_box(ui, 1, LIT(""), swatch, color_srgba_mix(rows->violet, color_srgba(0x18B8A4), swatch_mix), false);

	UI_BoxDesc text_stack = playground_fill_desc();
	text_stack.axis = AXIS_Y;
	text_stack.gap = 4.f;
	ui_box_begin_desc(ui, 2, LIT(""), text_stack);

	String title =
		row == 0 ? LIT("Frame time") :
		row == 1 ? LIT("Application") :
		row == 2 ? LIT("Rendering") :
		row == 3 ? LIT("Present wait") :
		row == 4 ? LIT("Other") :
			push_formatted(&ui->frame_arena, "Profiler scope %05u", row + 1);
	UI_BoxDesc title_box = playground_fill_desc();
	title_box.size[AXIS_Y] = ui_box_pixels(28.f);
	ui_text_box_string_desc(ui, 1, title_box, rows->title_style, title);

	String subtitle =
		row == 0 ? LIT("16.67 ms  |  complete frame") :
		row == 1 ? LIT("3.82 ms  |  application work") :
		row == 2 ? LIT("2.14 ms  |  render submission") :
		row == 3 ? LIT("7.35 ms  |  swapchain wait") :
		row == 4 ? LIT("3.36 ms  |  uncategorized") :
			push_formatted(&ui->frame_arena, "%.2f ms  |  %u calls", 0.01f * (f32)(row % 300), 1 + row % 97);
	UI_BoxDesc subtitle_box = playground_fill_desc();
	subtitle_box.size[AXIS_Y] = ui_box_pixels(28.f);
	ui_text_box_string_desc(ui, 2, subtitle_box, rows->subtitle_style, subtitle);

	ui_box_end(ui);
	ui_box_end(ui);
}

#include "demo_basics.c"

#include "demo_scroll_history.c"

#include "demo_profiler.c"

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

static void playground_draw_tree(Arena *arena, Draw_Context *draw, Text_Context *text, Text_GFX *text_gfx, Font_Handle font, UI_Box *box, UI_Box *hot, UI_Id selected_id)
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

	ui_box_paint(box);

	for (u32 child_index = 0; child_index < box->child_count; ++child_index) {
		playground_draw_tree(arena, draw, text, text_gfx, font, box->children[child_index], hot, selected_id);
	}
}

static void playground_draw_ui_frame(Draw_Context *draw, Text_GFX *text_gfx, UI_Context *ui)
{
	const UI_Frame *frame = ui_frame(ui);
	for (u32 layer_index = 0; layer_index < UI_LAYER_COUNT; layer_index ++)
	{
		for (UI_DrawCommand *command = frame->layers[layer_index].first; command; command = command->next)
		{
			if (command->has_clip) {
				draw_push_clip(draw, command->clip);
			}
			switch (command->kind)
			{
				case UI_DRAW_COMMAND_RECT:
				draw_rect(draw, (Draw_RectParams) {
					.rect = command->rect.rect,
					.color = command->rect.color,
					.corner_radii = { command->rect.roundness, command->rect.roundness, command->rect.roundness, command->rect.roundness },
					.edge_softness = command->rect.edge_softness,
				});
				break;
				case UI_DRAW_COMMAND_IMAGE:
				draw_image(draw, (Draw_TextureParams) {
					.rect = command->image.params.rect,
					.texture = command->image.params.texture,
					.region = command->image.params.region,
					.tint = COLOR_WHITE,
					.sampler = command->image.params.sampler,
					.blender = command->image.params.blender,
					.shader = command->image.params.shader,
				});
				break;
				case UI_DRAW_COMMAND_TEXT:
					text_gfx_draw_run(text_gfx, draw, command->text.run, command->text.position, command->text.color);
					break;
				case UI_DRAW_COMMAND_INSET_SHADOW:
					draw_inset_shadow(draw, command->inset_shadow.rect, command->inset_shadow.strength);
					break;
				case UI_DRAW_COMMAND_BACKDROP:
					Assert(!"the playground does not provide a backdrop texture");
					break;
				default:
					Assert(!"invalid UI draw command");
			}
			if (command->has_clip) {
				draw_pop_clip(draw);
			}
		}
	}
}

static b32 playground_near(f32 a, f32 b)
{
	return fabsf(a - b) < 0.01f;
}

static vec2 playground_test_measure_counted(UI_Box *box, UI_BoxConstraints constraints)
{
	(void)constraints;
	u32 *measure_count = box->user;
	(*measure_count)++;
	return v2(70.f, 10.f);
}

static const UI_BoxOps playground_test_counted_ops = {
	.measure = playground_test_measure_counted,
};

static UI_Box *playground_test_row(Arena *arena, f32 width, UI_BoxSize left_size, f32 left_basis, f32 left_min, f32 left_max, UI_BoxSize right_size, f32 right_basis, f32 right_min, f32 right_max)
{
	UI_BoxDesc root_desc = playground_fill_desc();
	root_desc.axis = AXIS_X;
	UI_BoxBuilder builder;
	UI_Box *root = ui_box_builder_begin(&builder, arena, 0, 1, LIT("root"), root_desc);

	UI_BoxDesc left = playground_fill_desc();
	left.size[AXIS_X] = left_size;
	left.min_size.x = left_min;
	left.max_size.x = left_max;
	UI_Box *left_box = ui_builder_box_make_desc(&builder, 1, LIT("left"), left);
	left_box->intrinsic_size.x = left_basis;

	UI_BoxDesc right = playground_fill_desc();
	right.size[AXIS_X] = right_size;
	right.min_size.x = right_min;
	right.max_size.x = right_max;
	UI_Box *right_box = ui_builder_box_make_desc(&builder, 2, LIT("right"), right);
	right_box->intrinsic_size.x = right_basis;

	ui_box_builder_end(&builder);
	ui_box_measure(root, (UI_BoxConstraints) { .max = v2(width, 100.f) });
	ui_box_layout(root, (rect_f32) { 0.f, 0.f, width, 100.f });
	return root;
}

static void playground_build_test_virtual_item(UI_Context *ui, u32 item_index, void *user)
{
	(void)item_index;
	(void)user;
	UI_BoxDesc row = ui_box_desc();
	row.axis = AXIS_X;
	row.size[AXIS_X] = ui_box_fill(1.f);
	row.size[AXIS_Y] = ui_box_pixels(42.f);
	ui_box_begin_desc(ui, 1, LIT("row"), row);
	UI_BoxDesc child = ui_box_desc();
	child.size[AXIS_X] = ui_box_fill(1.f);
	child.size[AXIS_Y] = ui_box_fill(1.f);
	ui_box_make_desc(ui, 1, LIT("nested child"), child);
	ui_box_end(ui);
}

static UI_Scroll *playground_build_test_scroll(Arena *arena, UI_Context *ui, UI_Box **root_out)
{
	(void)arena;
	UI_BoxDesc root_desc = playground_fill_desc();
	UI_Box *root = ui_build_begin(ui, UI_KEY("test scroll"), LIT("root"), root_desc);

	ui_push(ui);
	ui_size(ui, AXIS_X, ui_box_fill(1.f));
	ui_size(ui, AXIS_Y, ui_box_fill(1.f));
	UI_Scroll *scroll = ui_scroll_begin(ui, 1, AXIS_Y);

	ui_size(ui, AXIS_X, ui_box_fill(1.f));
	ui_size(ui, AXIS_Y, ui_box_fill(1.f));
	ui_axis(ui, AXIS_Y);
	ui_box_begin(ui, 1, LIT("viewport"));
	ui_push(ui);
	ui_size(ui, AXIS_X, ui_box_fill(1.f));
	ui_size(ui, AXIS_Y, ui_box_pixels(400.f));
	ui_box_make(ui, 1, LIT("content"));
	ui_pop(ui);
	ui_box_end(ui);

	ui_scroll_end(scroll);
	ui_pop(ui);
	ui_build_end(ui);
	*root_out = root;
	return scroll;
}

static UI_Response playground_build_test_signal(Arena *arena, UI_Context *ui, UI_Box **root_out, UI_Box **box_out)
{
	(void)arena;
	UI_BoxDesc root_desc = playground_fill_desc();
	root_desc.axis = AXIS_Y;
	UI_Box *root = ui_build_begin(ui, UI_KEY("test signal"), LIT("root"), root_desc);
	UI_BoxDesc box_desc = ui_box_desc();
	box_desc.size[AXIS_X] = ui_box_pixels(50.f);
	box_desc.size[AXIS_Y] = ui_box_pixels(30.f);
	UI_Box *box = ui_box_make_desc(ui, UI_KEY("button"), LIT("button"), box_desc);
	UI_Response response = ui_signal_from_box(box);
	ui_build_end(ui);
	*root_out = root;
	*box_out = box;
	return response;
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
		UI_Key profiler = UI_KEY("profiler");
		CHECK(profiler == ui_key_string(LIT("profiler")), "literal and runtime strings produce the same UI key");
		CHECK(profiler != UI_KEY("program"), "different strings produce different UI keys");
		CHECK(ui_key_child(profiler, 1) != ui_key_child(profiler, 2), "integer keys extend a string-keyed structural namespace");
		CHECK(ui_id_equal(ui_id_child(UI_ID_NONE, profiler), ui_id_child(UI_ID_NONE, UI_KEY("profiler"))), "string keys produce deterministic UI IDs");
	}

	ARENA_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(112, 100) };
		UI_Context *ui = ui_create(&arena, &window, (Text_Context *)1, (UI_Theme) {});
		ui_begin_frame(ui);
		UI_BoxDesc root_desc = playground_fill_desc();
		root_desc.axis = AXIS_X;
		UI_Box *root = ui_build_begin(ui, 1, LIT("root"), root_desc);
		PlaygroundScrollArea area = playground_scroll_area_begin(ui, 1, playground_fill_desc());
		UI_BoxDesc viewport_desc = playground_fill_desc();
		viewport_desc.axis = AXIS_Y;
		viewport_desc.overflow[AXIS_Y] = UI_BOX_OVERFLOW_SCROLL;
		UI_Box *viewport = ui_box_begin_desc(ui, 1, LIT("viewport"), viewport_desc);
		UI_BoxDesc content_desc = playground_fill_desc();
		content_desc.size[AXIS_Y] = ui_box_pixels(400.f);
		ui_box_make_desc(ui, 1, LIT("content"), content_desc);
		ui_box_end(ui);
		playground_scroll_area_end(ui, &area, viewport, color_srgba(0x25343A), color_srgba(0xC99CFF));
		ui_build_end(ui);

		viewport->scroll_offset.y = 150.f;
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		playground_layout_scrollbar(&area);
		PlaygroundScrollbar scrollbar = playground_scrollbar(&area);
		CHECK(area.root->child_count == 2 && area.root->children[0] == viewport && area.root->children[1] == area.track, "scroll area composes the viewport and scrollbar as sibling boxes");
		CHECK(area.track->child_count == 3 && area.track->children[1] == area.thumb, "scrollbar track and thumb are ordinary boxes");
		CHECK(playground_near(area.thumb->rect.h, 25.f) && playground_near(area.thumb->rect.y, 37.5f), "box scrollbar thumb maps the logical scroll range onto track travel");
		CHECK(playground_near(scrollbar.max_scroll, 300.f) && playground_near(scrollbar.travel, 75.f), "box scrollbar reads its range from the scrollable viewport");

		PlaygroundScroll scroll = { .offset = 225.f };
		playground_capture_scrollbar(&area, &scroll, 1);
		playground_prepare_scrollbar(&area, &scroll);
		viewport->scroll_offset.y = scroll.offset;
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		CHECK(playground_near(area.thumb->rect.h, 25.f) && playground_near(area.thumb->rect.y, 56.25f), "previous-frame scroll geometry configures the current scrollbar in one tree layout");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	ARENA_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(112, 100) };
		UI_Context *ui = ui_create(&arena, &window, (Text_Context *)1, (UI_Theme) {});

		ui_begin_frame(ui);
		UI_Box *root = 0;
		UI_Scroll *scroll = playground_build_test_scroll(&arena, ui, &root);
		UI_BoxState *root_state = root->state;
		CHECK(root->key == UI_KEY("test scroll") && root_state && !root->has_previous, "a string-keyed box acquires new persistent state");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		CHECK(playground_near(root_state->rect.w, 112.f) && playground_near(root_state->rect.h, 100.f), "layout commits finished box geometry into persistent state");
		CHECK(!scroll->has_previous && playground_near(scroll->viewport->scroll_max.y, 300.f), "a new scroll scope computes its geometry into box state");
		CHECK(playground_near(scroll->thumb->rect.h, 25.f), "the first layout resolves the scrollbar thumb");
		CHECK(scroll->track->paint.flags == UI_BOX_DRAW_BACKGROUND && scroll->thumb->paint.flags == UI_BOX_DRAW_BACKGROUND, "scrollbar track and thumb carry generic box appearance");
		ui_end_frame(ui);

		window.mouse_wheel.y = -1;
		ui_begin_frame(ui);
		ui->frame_elapsed = 0.055f;
		scroll = playground_build_test_scroll(&arena, ui, &root);
		CHECK(root->state == root_state && root->has_previous, "the next frame recovers the same box state and previous geometry");
		CHECK(scroll->has_previous && playground_near(scroll->viewport->state->scroll_max.y, 300.f), "the next scroll scope consumes geometry persisted by its boxes");
		CHECK(playground_near(scroll->target, 48.f) && playground_near(scroll->offset, 24.f), "wheel input updates context-owned target and time-invariant offset before layout");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		CHECK(playground_near(scroll->viewport->scroll_offset.y, 24.f), "current layout consumes the persistent scroll offset without a content relayout");
		ui_end_frame(ui);

		window.mouse_wheel.y = 0;
		window.mouse_position = v2i(106, 18);
		window.keys[OS_Key_MouseLeft] = OS_KEY_PRESSED | OS_KEY_DOWN;
		ui_begin_frame(ui);
		ui->frame_elapsed = 0.f;
		scroll = playground_build_test_scroll(&arena, ui, &root);
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		CHECK(ui_is_active(ui, scroll->thumb->id), "the scrollbar thumb exclusively captures the mouse");
		ui_end_frame(ui);

		window.mouse_position.y += 50;
		window.keys[OS_Key_MouseLeft] = OS_KEY_DOWN;
		ui_begin_frame(ui);
		scroll = playground_build_test_scroll(&arena, ui, &root);
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		CHECK(playground_near(scroll->offset, 224.f), "thumb dragging maps mouse travel into the persistent logical range");
		window.keys[OS_Key_MouseLeft] = OS_KEY_RELEASED;
		ui_end_frame(ui);

		window.keys[OS_Key_MouseLeft] = 0;
		ui_begin_frame(ui);
		scroll = playground_build_test_scroll(&arena, ui, &root);
		ui_scroll_reset(scroll);
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		CHECK(playground_near(scroll->viewport->scroll_offset.y, 0.f) && playground_near(scroll->thumb->rect.y, scroll->track->viewport.y), "reset updates the current viewport and box scrollbar geometry");
		ui_end_frame(ui);

		ui_begin_frame(ui);
		scroll = playground_build_test_scroll(&arena, ui, &root);
		CHECK(playground_near(scroll->offset, 0.f) && playground_near(scroll->target, 0.f), "reset persists through the viewport box state");
		scroll->viewport->children[0]->desc.size[AXIS_Y] = ui_box_pixels(800.f);
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		CHECK(playground_near(scroll->thumb->rect.h, 24.f), "track preparation sizes the thumb from current-frame content instead of cached geometry");
		ui_end_frame(ui);

		window.mouse_position = v2i(106, 80);
		window.keys[OS_Key_MouseLeft] = OS_KEY_PRESSED | OS_KEY_DOWN;
		ui_begin_frame(ui);
		ui->frame_elapsed = 0.f;
		scroll = playground_build_test_scroll(&arena, ui, &root);
		CHECK(ui_is_active(ui, scroll->track->id) && playground_near(scroll->target, 85.f), "the scrollbar track is an ordinary signaled box with page-step behavior");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		window.keys[OS_Key_MouseLeft] = OS_KEY_RELEASED;
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	ARENA_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(100, 100), .mouse_position = v2i(10, 10) };
		UI_Context *ui = ui_create(&arena, &window, (Text_Context *)1, (UI_Theme) {});
		UI_Box *root = 0;
		UI_Box *box = 0;

		ui_begin_frame(ui);
		UI_Response response = playground_build_test_signal(&arena, ui, &root, &box);
		CHECK(!response.hovered && !response.pressed && !response.held, "a new box does not interact through zero or uninitialized geometry");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(100.f, 100.f), .max = v2(100.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		CHECK(playground_near(box->state->hit_rect.w, 50.f) && playground_near(box->state->hit_rect.h, 30.f), "layout persists the box's clipped interaction rectangle");
		ui_end_frame(ui);

		window.keys[OS_Key_MouseLeft] = OS_KEY_PRESSED | OS_KEY_DOWN;
		ui_begin_frame(ui);
		response = playground_build_test_signal(&arena, ui, &root, &box);
		CHECK(response.hovered && response.pressed && response.held && ui_is_active(ui, box->id), "a box signal presses and exclusively captures through previous geometry");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(100.f, 100.f), .max = v2(100.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		ui_end_frame(ui);

		window.mouse_position = v2i(20, 15);
		window.keys[OS_Key_MouseLeft] = OS_KEY_DOWN;
		ui_begin_frame(ui);
		response = playground_build_test_signal(&arena, ui, &root, &box);
		CHECK(response.hovered && response.held && !response.pressed && playground_near(response.drag_delta.x, 10.f) && playground_near(response.drag_delta.y, 5.f), "a captured box reports held state and drag displacement");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(100.f, 100.f), .max = v2(100.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		ui_end_frame(ui);

		window.keys[OS_Key_MouseLeft] = OS_KEY_RELEASED;
		ui_begin_frame(ui);
		response = playground_build_test_signal(&arena, ui, &root, &box);
		CHECK(response.released && !response.held, "a captured box reports release before capture is cleared");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(100.f, 100.f), .max = v2(100.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		ui_end_frame(ui);
		CHECK(!ui->active.value, "mouse release clears the exclusive capture at frame end");

		ui_invalidate_layout(ui);
		window.keys[OS_Key_MouseLeft] = OS_KEY_PRESSED | OS_KEY_DOWN;
		ui_begin_frame(ui);
		response = playground_build_test_signal(&arena, ui, &root, &box);
		CHECK(!box->has_previous && !response.hovered && !response.pressed, "layout invalidation rejects stale box geometry for interaction");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(100.f, 100.f), .max = v2(100.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	ARENA_SCOPE(&arena)
	{
		UI_BoxDesc desc = ui_box_desc();
		UI_BoxBuilder builder;
		UI_Box *root = ui_box_builder_begin(&builder, &arena, 0, 1, LIT("root"), desc);
		UI_Box *a = ui_builder_box_begin_desc(&builder, 1, LIT("a"), desc);
		UI_Box *b = ui_builder_box_make_desc(&builder, 1, LIT("b"), desc);
		UI_Box *c = ui_builder_box_make_desc(&builder, 2, LIT("c"), desc);
		ui_builder_box_end(&builder);
		UI_Box *d = ui_builder_box_make_desc(&builder, 2, LIT("d"), desc);
		ui_box_builder_end(&builder);
		CHECK(root->child_count == 2 && root->children[0] == a && root->children[1] == d, "builder stores root children contiguously and in order");
		CHECK(a->child_count == 2 && a->children[0] == b && a->children[1] == c, "builder stores nested children contiguously and in order");
		CHECK(root->id.value && ui_id_equal(a->id, ui_id_child(root->id, 1)) && ui_id_equal(b->id, ui_id_child(a->id, 1)), "box IDs derive from their structural parent and construction key");
		CHECK(!ui_id_equal(b->id, d->id), "the same local key in a different structural scope produces a different ID");
	}

	ARENA_SCOPE(&arena)
	{
		UI_BoxBuilder builder;
		UI_Box *root = ui_box_builder_begin(&builder, &arena, 0, 1, LIT("root"), ui_box_desc());
		ui_builder_background(&builder, color_srgba(0x123456));
		UI_Box *background = ui_builder_box_make(&builder, 1, LIT("background"));
		ui_builder_push(&builder);
		ui_builder_border(&builder, color_srgba(0xABCDEF), 2.f);
		ui_builder_roundness(&builder, 4.f);
		UI_Box *background_and_border = ui_builder_box_make(&builder, 2, LIT("background and border"));
		ui_builder_pop(&builder);
		UI_Box *restored = ui_builder_box_make(&builder, 3, LIT("restored"));
		ui_box_builder_end(&builder);
		CHECK(!root->paint.flags, "the box root snapshots the initial paint description");
		CHECK(background->paint.flags == UI_BOX_DRAW_BACKGROUND, "a box snapshots the active background");
		CHECK(background_and_border->paint.flags == (UI_BOX_DRAW_BACKGROUND | UI_BOX_DRAW_BORDER) && playground_near(background_and_border->paint.border_width, 2.f) && playground_near(background_and_border->paint.roundness, 4.f), "nested paint changes compose on a box");
		CHECK(restored->paint.flags == UI_BOX_DRAW_BACKGROUND && playground_near(restored->paint.roundness, 0.f), "ui_pop restores layout and paint descriptions together");
	}

	ARENA_SCOPE(&arena)
	{
		UI_BoxDesc desc = ui_box_desc();
		UI_BoxBuilder builder;
		UI_Box *root = ui_box_builder_begin(&builder, &arena, 0, 1, LIT("root"), desc);
		ui_builder_push_id(&builder, 100);
		UI_Box *first = ui_builder_box_make_desc(&builder, 1, LIT("first"), desc);
		ui_builder_pop_id(&builder);
		ui_builder_push_id(&builder, 200);
		UI_Box *second = ui_builder_box_make_desc(&builder, 1, LIT("second"), desc);
		ui_builder_pop_id(&builder);
		ui_box_builder_end(&builder);
		CHECK(root->child_count == 2 && !ui_id_equal(first->id, second->id), "explicit ID scopes disambiguate repeated component-local keys");
	}

	ARENA_SCOPE(&arena)
	{
		UI_BoxBuilder builder;
		UI_Box *root = ui_box_builder_begin(&builder, &arena, 0, 1, LIT("root"), ui_box_desc());
		ui_builder_push(&builder);
		ui_builder_size(&builder, AXIS_Y, ui_box_fill(1.f));
		ui_builder_size(&builder, AXIS_X, ui_box_pixels(60.f));
		UI_Box *first = ui_builder_box_make(&builder, 1, LIT("first"));
		UI_Box *second = ui_builder_box_make(&builder, 2, LIT("second"));
		ui_builder_push(&builder);
		ui_builder_size(&builder, AXIS_X, ui_box_pixels(10.f));
		UI_Box *nested = ui_builder_box_make(&builder, 3, LIT("nested"));
		ui_builder_pop(&builder);
		UI_Box *restored = ui_builder_box_make(&builder, 4, LIT("restored"));
		ui_builder_pop(&builder);
		UI_Box *defaults = ui_builder_box_make(&builder, 5, LIT("defaults"));
		ui_box_builder_end(&builder);
		CHECK(root->child_count == 5 && first->desc.size[AXIS_X].value == 60.f && second->desc.size[AXIS_X].value == 60.f, "active descriptor values apply to every box in the construction scope");
		CHECK(nested->desc.size[AXIS_X].value == 10.f && restored->desc.size[AXIS_X].value == 60.f, "pop restores the complete previous descriptor");
		CHECK(restored->desc.size[AXIS_Y].kind == UI_BOX_SIZE_FILL && defaults->desc.size[AXIS_X].kind == UI_BOX_SIZE_CONTENT, "nested descriptor scopes restore all fields");
	}

	ARENA_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(300, 100) };
		UI_Context *ui = ui_create(&arena, &window, (Text_Context *)1, (UI_Theme) {});
		ui_begin_frame(ui);
		ui_build_begin(ui, 1, LIT("root"), ui_box_desc());
		UI_BoxTableColumn columns[] = {
			ui_box_table_content(),
			ui_box_table_fixed(50.f),
			ui_box_table_flex(1.f),
		};
		UI_BoxTable table = ui_box_table_begin(ui, 1, LIT("table"), (UI_BoxTableDesc) {
			.columns = columns,
			.column_count = ArrayCount(columns),
			.row_height = 20.f,
			.column_gap = 5.f,
			.row_gap = 2.f,
		});
		ui_box_table_row_begin(&table, 1);
		UI_Box *r0c0 = ui_box_table_cell_begin(&table);
		r0c0->intrinsic_size.x = 30.f;
		ui_box_table_cell_end(&table);
		UI_Box *r0c1 = ui_box_table_cell_begin(&table);
		r0c1->intrinsic_size.x = 10.f;
		ui_box_table_cell_end(&table);
		UI_Box *r0c2 = ui_box_table_cell_begin(&table);
		r0c2->intrinsic_size.x = 20.f;
		ui_box_table_cell_end(&table);
		ui_box_table_row_end(&table);
		ui_box_table_row_begin(&table, 2);
		UI_Box *r1c0 = ui_box_table_cell_begin(&table);
		u32 nested_measure_count = 0;
		UI_Box *nested = ui_box_make_desc(ui, 1, LIT("nested"), ui_box_desc());
		nested->ops = &playground_test_counted_ops;
		nested->user = &nested_measure_count;
		ui_box_table_cell_end(&table);
		ui_box_table_cell_begin(&table);
		ui_box_table_cell_end(&table);
		ui_box_table_cell_begin(&table);
		ui_box_table_cell_end(&table);
		ui_box_table_row_end(&table);
		UI_Box *table_box = ui_box_table_end(&table);
		ui_build_end(ui);
		ui_box_measure(table_box, (UI_BoxConstraints) { .max = v2(300.f, 100.f) });
		ui_box_layout(table_box, (rect_f32) { 0.f, 0.f, 300.f, 42.f });
		UI_Box *r1c1 = table_box->children[1]->children[1];
		UI_Box *r1c2 = table_box->children[1]->children[2];
		CHECK(playground_near(r0c0->rect.w, 70.f) && playground_near(r1c0->rect.w, 70.f), "content table tracks use the widest cell subtree across rows");
		CHECK(playground_near(r0c1->rect.w, 50.f) && playground_near(r1c1->rect.w, 50.f), "fixed table tracks remain aligned across rows");
		CHECK(playground_near(r0c2->rect.w, 170.f) && playground_near(r1c2->rect.w, 170.f), "flex table tracks receive the remaining width");
		CHECK(playground_near(r0c1->rect.x, r1c1->rect.x) && playground_near(r0c2->rect.x, r1c2->rect.x), "cell boundaries align across every table row");
		CHECK(nested_measure_count == 1, "table cells are measured once before shared tracks are resolved");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	ARENA_SCOPE(&arena)
	{
		UI_Box *root = playground_test_row(&arena, 400.f, ui_box_flex(0.f, 3.f), 300.f, 0.f, UI_BOX_INFINITY, ui_box_flex(0.f, 1.f), 300.f, 0.f, UI_BOX_INFINITY);
		CHECK(playground_near(root->children[0]->rect.w, 150.f) && playground_near(root->children[1]->rect.w, 250.f), "negative space follows shrink weights");
	}

	ARENA_SCOPE(&arena)
	{
		UI_BoxDesc desc = ui_box_desc();
		desc.axis = AXIS_X;
		UI_BoxBuilder builder;
		UI_Box *root = ui_box_builder_begin(&builder, &arena, 0, 1, LIT("root"), desc);
		root->intrinsic_size.x = 270.f;
		UI_Box *child = ui_builder_box_make_desc(&builder, 1, LIT("child"), desc);
		child->intrinsic_size.x = 100.f;
		ui_box_builder_end(&builder);
		ui_box_measure(root, (UI_BoxConstraints) { .max = v2(UI_BOX_INFINITY, UI_BOX_INFINITY) });
		CHECK(playground_near(root->measured_size.x, 270.f), "intrinsic basis remains independent from child content");
	}

	ARENA_SCOPE(&arena)
	{
		UI_Box *root = playground_test_row(&arena, 300.f, ui_box_flex(0.f, 3.f), 300.f, 200.f, UI_BOX_INFINITY, ui_box_flex(0.f, 1.f), 300.f, 0.f, UI_BOX_INFINITY);
		CHECK(playground_near(root->children[0]->rect.w, 200.f) && playground_near(root->children[1]->rect.w, 100.f), "shrink deficit redistributes after a child reaches its minimum");
	}

	ARENA_SCOPE(&arena)
	{
		UI_Box *root = playground_test_row(&arena, 400.f, ui_box_fill(1.f), 0.f, 0.f, 100.f, ui_box_fill(1.f), 0.f, 0.f, UI_BOX_INFINITY);
		CHECK(playground_near(root->children[0]->rect.w, 100.f) && playground_near(root->children[1]->rect.w, 300.f), "grow surplus redistributes after a child reaches its maximum");
	}

	ARENA_SCOPE(&arena)
	{
		UI_BoxDesc root_desc = playground_fill_desc();
		root_desc.axis = AXIS_X;
		root_desc.gap = 20.f;
		UI_BoxBuilder builder;
		UI_Box *root = ui_box_builder_begin(&builder, &arena, 0, 1, LIT("root"), root_desc);

		UI_BoxDesc fixed = playground_fill_desc();
		fixed.size[AXIS_X] = ui_box_pixels(100.f);
		fixed.horz_margin[0] = 10.f;
		fixed.horz_margin[1] = 10.f;
		ui_builder_box_make_desc(&builder, 1, LIT("fixed"), fixed);

		UI_BoxDesc fill = playground_fill_desc();
		fill.size[AXIS_X] = ui_box_fill(1.f);
		ui_builder_box_make_desc(&builder, 2, LIT("fill"), fill);

		ui_box_builder_end(&builder);
		ui_box_measure(root, (UI_BoxConstraints) { .max = v2(400.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 400.f, 100.f });
		CHECK(playground_near(root->children[1]->rect.w, 260.f), "margins and gaps are deducted before distributing free space");
	}

	ARENA_SCOPE(&arena)
	{
		UI_Box *root = playground_test_row(&arena, 100.f, ui_box_flex(0.f, 1.f), 200.f, 80.f, UI_BOX_INFINITY, ui_box_flex(0.f, 1.f), 200.f, 80.f, UI_BOX_INFINITY);
		CHECK(playground_near(root->children[0]->rect.w, 80.f) && playground_near(root->children[1]->rect.w, 80.f), "unsatisfied deficit stops at child minimums without negative sizes");
	}

	ARENA_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(100, 100) };
		UI_Context *ui = ui_create(&arena, &window, (Text_Context *)1, (UI_Theme) {});
		ui_begin_frame(ui);
		UI_BoxDesc root_desc = ui_box_desc();
		ui_build_begin(ui, 1, LIT("root"), root_desc);
		UI_BoxDesc list_desc = ui_box_desc();
		list_desc.gap = 8.f;
		UI_Box *list = ui_box_make_virtual_list_desc(ui, 1, LIT("list"), list_desc, (UI_BoxVirtualListDesc) {
			.item_count = 1,
			.build_item = playground_build_test_virtual_item,
		});
		ui_build_end(ui);
		ui_box_measure(list, (UI_BoxConstraints) { .max = v2(100.f, 100.f) });
		ui_box_layout(list, rect_f32_from_size(list->measured_size));
		CHECK(playground_near(list->measured_size.y, 42.f) && playground_near(list->content_size.y, 42.f), "a short virtual list wraps its logical items");
		CHECK(list->child_count == 1 && list->children[0]->child_count == 1, "a virtual item materializes as an arbitrary box subtree");
		CHECK(ui_id_equal(list->children[0]->id, ui_id_child(ui_id_child(list->id, 0), 1)), "a virtual item ID includes its logical item scope");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	ARENA_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(100, 100) };
		UI_Context *ui = ui_create(&arena, &window, (Text_Context *)1, (UI_Theme) {});
		ui_begin_frame(ui);
		UI_BoxDesc root_desc = ui_box_desc();
		ui_build_begin(ui, 1, LIT("root"), root_desc);
		UI_BoxDesc list_desc = ui_box_desc();
		list_desc.gap = 8.f;
		UI_Box *list = ui_box_make_virtual_list_desc(ui, 1, LIT("list"), list_desc, (UI_BoxVirtualListDesc) {
			.item_count = 1000,
			.build_item = playground_build_test_virtual_item,
		});
		ui_build_end(ui);
		ui_box_measure(list, (UI_BoxConstraints) { .max = v2(100.f, 100.f) });
		ui_box_layout(list, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		CHECK(playground_near(list->measured_size.y, 100.f) && playground_near(list->content_size.y, 49992.f), "a long virtual list clamps while preserving its full logical extent");
		CHECK(list->child_count == 4 && list->virtual_list.first_item == 0 && list->virtual_list.one_past_item == 4, "a virtual list materializes only visible and overscan items");
		list->scroll_offset.y = 500.f;
		ui_box_relayout(list);
		CHECK(list->child_count == 6 && list->children[0]->virtual_index == 8 && list->children[5]->virtual_index == 13, "scrolling rematerializes the correct logical item range");
		CHECK(ui_id_equal(list->children[0]->id, ui_id_child(ui_id_child(list->id, 8), 1)), "rematerialized virtual items retain deterministic IDs");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	ARENA_SCOPE(&arena)
	{
		UI_BoxDesc root_desc = playground_fill_desc();
		root_desc.axis = AXIS_Y;
		root_desc.overflow[AXIS_Y] = UI_BOX_OVERFLOW_SCROLL;
		UI_BoxBuilder builder;
		UI_Box *root = ui_box_builder_begin(&builder, &arena, 0, 1, LIT("scroll"), root_desc);

		UI_BoxDesc content = playground_fill_desc();
		content.size[AXIS_Y] = ui_box_pixels(200.f);
		UI_Box *child = ui_builder_box_make_desc(&builder, 1, LIT("content"), content);
		ui_box_builder_end(&builder);
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(100.f, 100.f), .max = v2(100.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		root->scroll_offset.y = 500.f;
		ui_box_relayout(root);
		CHECK(playground_near(root->content_size.y, 200.f) && playground_near(root->scroll_max.y, 100.f), "layout computes content extent and scroll range");
		CHECK(playground_near(root->scroll_offset.y, 100.f) && playground_near(child->rect.y, -100.f), "layout clamps scroll and places descendant geometry at its scrolled position");
		CHECK(ui_box_find_deepest(root, v2(50.f, 50.f)) == child && !ui_box_find_deepest(root, v2(50.f, 150.f)), "hit testing uses translated geometry and the effective clip");
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
	UI_Context *ui = ui_create(&arena, window, text, ui_default_theme(font));
	text_preload_ascii(text, font, 14);
	text_preload_ascii(text, font, 16);

	vec2i previous_size = {};
	u32 density_index = 1;
	UI_Id selected_id = UI_ID_NONE;
	UI_Id active_scrollbar = UI_ID_NONE;
	PlaygroundMode mode = PLAYGROUND_MODE_BASICS;
	PlaygroundScroll scrolls[PLAYGROUND_MODE_COUNT][4] = {};
	u64 layout_generation = 1;
	Seconds previous_frame_time = seconds_now();
	while (os_window_is_open(window))
	{
		os_graphical_poll();
		b32 reset_scroll_history = false;
		Seconds frame_time = seconds_now();
		f32 elapsed = (f32)Max(frame_time.seconds - previous_frame_time.seconds, 0.0);
		previous_frame_time = frame_time;
		if (!os_window_is_open(window)) {
			break;
		}
		if (window->keys[OS_Key_Space] & OS_KEY_PRESSED) {
			density_index = (density_index + 1) % ArrayCount(playground_densities);
			layout_generation++;
			ui_invalidate_layout(ui);
			active_scrollbar = UI_ID_NONE;
		}
		if (window->keys[OS_Key_Tab] & OS_KEY_PRESSED) {
			mode = (mode + 1) % PLAYGROUND_MODE_COUNT;
			ui_invalidate_layout(ui);
			active_scrollbar = UI_ID_NONE;
		}
		if (window->keys[OS_Key_R] & OS_KEY_PRESSED)
		{
			if (mode == PLAYGROUND_MODE_SCROLL_HISTORY) reset_scroll_history = true;
			else memory_zero(scrolls[mode], sizeof(scrolls[mode]));
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
			layout_generation++;
			active_scrollbar = UI_ID_NONE;
		}

		ui_begin_frame(ui);
		ARENA_SCOPE(&frame_arena)
		{
			PlaygroundScene scene = {};
			switch (mode)
			{
				case PLAYGROUND_MODE_BASICS: scene = playground_build_basics(&frame_arena, ui, font, playground_densities[density_index]); break;
				case PLAYGROUND_MODE_PROFILER: scene = playground_build_dummy_profiler(&frame_arena, ui, font, playground_densities[density_index]); break;
				case PLAYGROUND_MODE_SCROLL_HISTORY: scene = playground_build_scroll_history(&frame_arena, ui, font, playground_densities[density_index], reset_scroll_history); break;
				default: Assert(false); break;
			}
			vec2 mouse = v2_from_v2i(window->mouse_position);
			OS_KeyState mouse_left = window->keys[OS_Key_MouseLeft];
			b32 scrollbar_hovered = false;
			b32 mouse_press_consumed = false;

			if (window->mouse_wheel.y)
			{
				for (u32 scroll_index = 0; scroll_index < scene.scroll_area_count; scroll_index ++)
				{
					PlaygroundScroll *scroll = &scrolls[mode][scroll_index];
					PlaygroundScrollGeometry *previous = &scroll->previous;
					if (previous->valid && previous->layout_generation == layout_generation && rect_f32_contains(previous->viewport, mouse))
					{
						scroll->target -= window->mouse_wheel.y * 48.f;
						break;
					}
				}
			}

			if ((mouse_left & OS_KEY_PRESSED) && !active_scrollbar.value)
			{
				for (u32 scroll_index = scene.scroll_area_count; scroll_index > 0; scroll_index --)
				{
					PlaygroundScrollArea *area = &scene.scroll_areas[scroll_index - 1];
					PlaygroundScroll *scroll = &scrolls[mode][scroll_index - 1];
					PlaygroundScrollGeometry *previous = &scroll->previous;
					f32 max_scroll = previous->scroll_max.y - previous->scroll_min.y;
					if (!previous->valid || previous->layout_generation != layout_generation || max_scroll <= 0.f || !rect_f32_contains(previous->track, mouse)) continue;

					mouse_press_consumed = true;
					if (rect_f32_contains(previous->thumb, mouse))
					{
						active_scrollbar = area->thumb->id;
						scroll->drag_offset = scroll->offset;
						scroll->drag_mouse = mouse.y;
					}
					else
					{
						active_scrollbar = area->track->id;
						f32 direction = mouse.y < previous->thumb.y ? -1.f : 1.f;
						scroll->target += direction * previous->viewport.h * 0.85f;
					}
					break;
				}
			}

			b32 active_scrollbar_found = !active_scrollbar.value;
			for (u32 scroll_index = 0; scroll_index < scene.scroll_area_count; scroll_index ++)
			{
				PlaygroundScrollArea *area = &scene.scroll_areas[scroll_index];
				PlaygroundScroll *scroll = &scrolls[mode][scroll_index];
				PlaygroundScrollGeometry *previous = &scroll->previous;
				UI_Id track_id = area->track->id;
				UI_Id thumb_id = area->thumb->id;
				b32 previous_is_current = previous->valid && previous->layout_generation == layout_generation;
				f32 max_scroll = previous->scroll_max.y - previous->scroll_min.y;
				f32 travel = Max(0.f, previous->track_viewport.h - previous->thumb.h);
				scrollbar_hovered |= previous_is_current && max_scroll > 0.f && rect_f32_contains(previous->track, mouse);
				if (ui_id_equal(active_scrollbar, track_id)) {
					active_scrollbar_found = true;
				}
				if (ui_id_equal(active_scrollbar, thumb_id))
				{
					active_scrollbar_found = true;
					if (previous_is_current && (mouse_left & (OS_KEY_DOWN | OS_KEY_RELEASED)) && travel > 0.f)
					{
						f32 mouse_delta = mouse.y - scroll->drag_mouse;
						scroll->offset = CLAMP(scroll->drag_offset + mouse_delta * max_scroll / travel, previous->scroll_min.y, previous->scroll_max.y);
						scroll->target = scroll->offset;
					}
				}
			}
			if (!active_scrollbar_found) {
				active_scrollbar = UI_ID_NONE;
			}
			for (u32 scroll_index = 0; scroll_index < scene.scroll_area_count; scroll_index ++)
			{
				PlaygroundScrollArea *area = &scene.scroll_areas[scroll_index];
				PlaygroundScroll *scroll = &scrolls[mode][scroll_index];
				PlaygroundScrollGeometry *previous = &scroll->previous;
				if (previous->valid) {
					scroll->target = CLAMP(scroll->target, previous->scroll_min.y, previous->scroll_max.y);
				}
				b32 dragging = ui_id_equal(active_scrollbar, area->thumb->id);
				if (!dragging) {
					scroll->offset = playground_smooth_scroll(scroll->offset, scroll->target, elapsed);
				}
				area->viewport->scroll_offset.y = scroll->offset;
				playground_prepare_scrollbar(area, scroll);
			}

			vec2 size = v2_from_v2i(window->size);
			ui_box_measure(scene.root, (UI_BoxConstraints) { .min = size, .max = size });
			ui_box_layout(scene.root, rect_f32_from_size(size));
			for (u32 scroll_index = 0; scroll_index < scene.scroll_area_count; scroll_index ++)
			{
				PlaygroundScrollArea *area = &scene.scroll_areas[scroll_index];
				PlaygroundScroll *scroll = &scrolls[mode][scroll_index];
				if (!scroll->previous.valid) {
					playground_layout_scrollbar(area);
				}
				scroll->offset = area->viewport->scroll_offset.y;
				scroll->target = CLAMP(scroll->target, area->viewport->scroll_min.y, area->viewport->scroll_max.y);
				playground_capture_scrollbar(area, scroll, layout_generation);
				PlaygroundScrollbar scrollbar = playground_scrollbar(area);
				scrollbar_hovered |= scrollbar.max_scroll > 0.f && rect_f32_contains(area->track->rect, mouse);
			}
			UI_Box *hot = ui_box_find_deepest(scene.root, mouse);
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
			gfx_end_pass(draw);
			gfx_begin_pass(draw, (GFX_PassDesc) { .output = gfx_window_texture(gfx_window) });
			playground_draw_ui_frame(draw, text_gfx, ui);
			gfx_end_pass(draw);
			text_gfx_sync(text_gfx);
			gfx_end_frame(draw);
			gfx_window_present(gfx_window);
		}
		ui_end_frame(ui);
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
