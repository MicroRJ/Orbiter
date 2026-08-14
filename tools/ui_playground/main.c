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
	UI_Box *root;
}
PlaygroundScene;

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

static Str playground_read_file(Arena *arena, const char *path)
{
	Str result = {};
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
			result = str_from_data(data, (u32)size);
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

static UI_Box *playground_make_box(UI_Context *ui, u64 key, Str name, Color_SRGBA color, b32 show_size)
{
	UI_Box *box = ui_box_make(ui, key, name);
	box->user = playground_visual(&ui->frame_arena, color, show_size);
	return box;
}

static UI_Box *playground_begin_box(UI_Context *ui, u64 key, Str name, Color_SRGBA color, b32 show_size)
{
	UI_Box *box = ui_box_begin(ui, key, name);
	box->user = playground_visual(&ui->frame_arena, color, show_size);
	return box;
}

static UI_BoxDesc playground_fill_desc(void)
{
	UI_BoxDesc desc = ui_defaults();
	desc.size[AXIS_X] = ui_grow(1.f);
	desc.size[AXIS_Y] = ui_grow(1.f);
	return desc;
}

static UI_Box *playground_frame_slot_begin(UI_Context *ui, UI_Key key, AXIS fill_axis)
{
	ui_clean(ui);
	ui_layout(ui, &UI_FlatLayoutHooks);
	ui_size(ui, fill_axis, ui_grow(1.f));
	return ui_box_begin(ui, key, LIT("frame slot"));
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
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(76.f));
	ui_axis(ui, AXIS_X);
	ui_padd(ui, AXIS_X, 8.f, 8.f);
	ui_padd(ui, AXIS_Y, 8.f, 8.f);
	ui_gap(ui, 10.f);
	playground_begin_box(ui, 1, LIT(""), color_srgba_mix(rows->violet, rows->slate, 0.58f), false);

	ui_clean(ui);
	ui_layout(ui, &UI_FlatLayoutHooks);
	ui_size(ui, AXIS_X, ui_fixed(44.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_box_begin(ui, 1, LIT("swatch slot"));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_fixed(44.f));
	ui_size(ui, AXIS_Y, ui_fixed(44.f));
	ui_align(ui, AXIS_Y, 0.5f);
	f32 swatch_mix = (f32)(row % 7) / 6.f;
	playground_make_box(ui, 1, LIT(""), color_srgba_mix(rows->violet, color_srgba(0x18B8A4), swatch_mix), false);
	ui_box_end(ui);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_axis(ui, AXIS_Y);
	ui_gap(ui, 4.f);
	ui_box_begin(ui, 2, LIT(""));

	Str title =
		row == 0 ? LIT("Frame time") :
		row == 1 ? LIT("Application") :
		row == 2 ? LIT("Rendering") :
		row == 3 ? LIT("Present wait") :
		row == 4 ? LIT("Other") :
			str_push_copy_f(&ui->frame_arena, "Profiler scope %05u", row + 1);
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(28.f));
	ui_text(ui, 1, rows->title_style, title);

	Str subtitle =
		row == 0 ? LIT("16.67 ms  |  complete frame") :
		row == 1 ? LIT("3.82 ms  |  application work") :
		row == 2 ? LIT("2.14 ms  |  render submission") :
		row == 3 ? LIT("7.35 ms  |  swapchain wait") :
		row == 4 ? LIT("3.36 ms  |  uncategorized") :
			str_push_copy_f(&ui->frame_arena, "%.2f ms  |  %u calls", 0.01f * (f32)(row % 300), 1 + row % 97);
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(28.f));
	ui_text(ui, 2, rows->subtitle_style, subtitle);

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
			.corner_radii = draw_corner_radii_all(5.f),
			.edge_softness = 1.f,
		});

		b32 hovered = box == hot;
		b32 selected = ui_id_equal(box->id, selected_id);
		Color_SRGBA outline = color_with_alpha(visual->color, selected ? 1.f : hovered ? 0.82f : 0.38f);
		playground_draw_outline(draw, box->rect, selected ? 3.f : hovered ? 2.f : 1.f, outline);

		if (box->name.size && box->rect.w > 24.f && box->rect.h > 20.f)
		{
			Str label = box->name;
			if (visual->show_size) {
				label = str_push_copy_f(arena, "%.*s  |  %.0f px", box->name.size, box->name.text, box->rect.w);
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

	for (UI_Box *child = box->first; child; child = child->next) {
		playground_draw_tree(arena, draw, text, text_gfx, font, child, hot, selected_id);
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

static const UI_BoxHooks playground_test_counted_ops = {
	.measure = playground_test_measure_counted,
};

static UI_Box *playground_test_row(Arena *arena, f32 width, UI_BoxSize left_size, f32 left_basis, f32 left_min, f32 left_max, UI_BoxSize right_size, f32 right_basis, f32 right_min, f32 right_max)
{
	UI_BoxDesc root_desc = playground_fill_desc();
	root_desc.axis = AXIS_X;
	UI_Builder builder;
	UI_Box *root = ui_box_builder_begin(&builder, arena, 0, 1, LIT("root"), root_desc);

	UI_BoxDesc left = playground_fill_desc();
	left.size[AXIS_X] = left_size;
	left.min_size.x = left_min;
	left.max_size.x = left_max;
	ui_builder_clean(&builder);
	UI_Box *left_box = ui_builder_box_make_desc(&builder, 1, LIT("left"), left);
	left_box->intrinsic_size.x = left_basis;

	UI_BoxDesc right = playground_fill_desc();
	right.size[AXIS_X] = right_size;
	right.min_size.x = right_min;
	right.max_size.x = right_max;
	ui_builder_clean(&builder);
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
	ui_clean(ui);
	ui_axis(ui, AXIS_X);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(42.f));
	ui_box_begin(ui, 1, LIT("row"));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_box_make(ui, 1, LIT("nested child"));
	ui_box_end(ui);
}

static void playground_build_counted_test_virtual_item(UI_Context *ui, u32 item_index, void *user)
{
	u32 *build_count = user;
	(*build_count)++;
	playground_build_test_virtual_item(ui, item_index, 0);
}

static void playground_build_test_horizontal_virtual_item(UI_Context *ui, u32 item_index, void *user)
{
	(void)item_index;
	(void)user;
	ui_clean(ui);
	ui_axis(ui, AXIS_Y);
	ui_size(ui, AXIS_X, ui_fixed(30.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_box_begin(ui, 1, LIT("item"));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_box_make(ui, 1, LIT("nested child"));
	ui_box_end(ui);
}

static void playground_build_test_margined_virtual_item(UI_Context *ui, u32 item_index, void *user)
{
	(void)item_index;
	(void)user;
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(32.f));
	ui_margin(ui, AXIS_Y, 5.f, 7.f);
	ui_box_make(ui, 1, LIT("item"));
}

static UI_ScrollBox *playground_build_test_scroll(Arena *arena, UI_Context *ui, UI_Box **root_out)
{
	(void)arena;
	UI_BoxDesc root_desc = playground_fill_desc();
	UI_Box *root = ui_build_begin(ui, UI_KEY("test scroll"), LIT("root"), root_desc);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	UI_ScrollBox *scroll = ui_scroll_box_begin(ui, 1, AXIS_Y);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_axis(ui, AXIS_Y);
	ui_box_begin(ui, 1, LIT("viewport"));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(400.f));
	ui_box_make(ui, 1, LIT("content"));
	ui_clean(ui);
	ui_box_end(ui);

	ui_scroll_box_end(scroll);
	ui_clean(ui);
	ui_build_end(ui);
	*root_out = root;
	return scroll;
}

typedef struct
{
	UI_Box *root;
	UI_ScrollBox *outer;
	UI_ScrollBox *inner;
}
PlaygroundNestedScrollTest;

static PlaygroundNestedScrollTest playground_build_nested_test_scroll(UI_Context *ui)
{
	PlaygroundNestedScrollTest test = {};
	test.root = ui_build_begin(ui, UI_KEY("nested scroll test"), LIT("root"), playground_fill_desc());

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	test.outer = ui_scroll_box_begin(ui, 1, AXIS_Y);

	ui_clean(ui);
	ui_axis(ui, AXIS_Y);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_box_begin(ui, 1, LIT("outer content"));

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(100.f));
	ui_box_make(ui, 1, LIT("before"));

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(100.f));
	test.inner = ui_scroll_box_begin(ui, 2, AXIS_Y);
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_axis(ui, AXIS_Y);
	ui_box_begin(ui, 1, LIT("inner content"));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(400.f));
	ui_box_make(ui, 1, LIT("inner body"));
	ui_clean(ui);
	ui_box_end(ui);
	ui_scroll_box_end(test.inner);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(300.f));
	ui_box_make(ui, 3, LIT("after"));
	ui_clean(ui);
	ui_box_end(ui);
	ui_scroll_box_end(test.outer);
	ui_clean(ui);
	ui_build_end(ui);
	return test;
}

typedef struct
{
	UI_Box *root;
	UI_ScrollBox *content;
	UI_ScrollBox *overlay;
}
PlaygroundOverlappingScrollTest;

static UI_ScrollBox *playground_build_overlapping_test_scroll(UI_Context *ui, UI_Key key)
{
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	UI_ScrollBox *scroll = ui_scroll_box_begin(ui, key, AXIS_Y);
	ui_clean(ui);
	ui_axis(ui, AXIS_Y);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	ui_box_begin(ui, 1, LIT("content"));
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_fixed(400.f));
	ui_box_make(ui, 1, LIT("body"));
	ui_box_end(ui);
	ui_scroll_box_end(scroll);
	return scroll;
}

static PlaygroundOverlappingScrollTest playground_build_overlapping_test_scrolls(UI_Context *ui)
{
	PlaygroundOverlappingScrollTest test = {};
	UI_BoxDesc root_desc = playground_fill_desc();
	root_desc.layout = &UI_FlatLayoutHooks;
	test.root = ui_build_begin(ui, UI_KEY("overlapping scroll test"), LIT("root"), root_desc);
	test.content = playground_build_overlapping_test_scroll(ui, 1);
	ui_push_box_z(ui, UI_Z_OVERLAY);
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_grow(1.f));
	UI_Box *overlay_frame = ui_box_begin(ui, 2, LIT("overlay frame"));
	overlay_frame->hit_intercept = true;
	test.overlay = playground_build_overlapping_test_scroll(ui, 1);
	ui_box_end(ui);
	ui_pop_box_z(ui);
	ui_clean(ui);
	ui_build_end(ui);
	return test;
}

static UI_Response playground_build_test_signal(Arena *arena, UI_Context *ui, UI_Box **root_out, UI_Box **box_out)
{
	(void)arena;
	UI_BoxDesc root_desc = playground_fill_desc();
	root_desc.axis = AXIS_Y;
	UI_Box *root = ui_build_begin(ui, UI_KEY("test signal"), LIT("root"), root_desc);
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_fixed(50.f));
	ui_size(ui, AXIS_Y, ui_fixed(30.f));
	UI_Box *box = ui_box_make(ui, UI_KEY("button"), LIT("button"));
	UI_Response response = ui_signal_from_box(box);
	ui_build_end(ui);
	*root_out = root;
	*box_out = box;
	return response;
}

static UI_Context *playground_test_ui_create(Arena *arena, OS_Window *window)
{
	Input_State *input = arena_push_zero(arena, sizeof(*input));
	return ui_create(arena, window, input, (Text_Context *)1, 0, (UI_Theme) {});
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

	{
		OS_Event events[] = {
			{ .type = OS_EVENT_KEY_PRESS, .key = OS_Key_A },
			{ .type = OS_EVENT_KEY_PRESS, .key = OS_Key_A, .repeat = true },
		};
		OS_Window window = { .events = events, .event_count = ArrayCount(events), .event_capacity = ArrayCount(events) };
		Input_State input = {};
		input_state_update(&input, &window);
		CHECK((input.keys[OS_Key_A] & (INPUT_KEY_DOWN | INPUT_KEY_PRESSED | INPUT_KEY_REPEAT)) == (INPUT_KEY_DOWN | INPUT_KEY_PRESSED | INPUT_KEY_REPEAT), "input state observes press and repeat events");
		window.event_count = 0;
		input_state_update(&input, &window);
		CHECK(input.keys[OS_Key_A] == INPUT_KEY_DOWN, "input state retains only held keys between frames");
		events[0] = (OS_Event) { .type = OS_EVENT_WINDOW_FOCUS_LOST };
		window.event_count = 1;
		input_state_update(&input, &window);
		CHECK(input.keys[OS_Key_A] == INPUT_KEY_RELEASED, "focus loss releases every held key");
	}

	SCRATCH_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(100, 100) };
		UI_Context *ui = playground_test_ui_create(&arena, &window);
		ui_begin_frame(ui);
		UI_BoxDesc root_desc = ui_defaults();
		root_desc.layout = &UI_FlatLayoutHooks;
		UI_Box *root = ui_build_begin(ui, UI_KEY("z ordered hits"), LIT("z ordered hits"), root_desc);
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fixed(20.f));
		ui_size(ui, AXIS_Y, ui_fixed(20.f));
		ui_paint_z(ui, 10);
		UI_Box *front = ui_box_make(ui, 1, LIT("front"));
		ui_signal_from_box(front);
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fixed(100.f));
		ui_size(ui, AXIS_Y, ui_fixed(100.f));
		UI_Box *back = ui_box_make(ui, 2, LIT("back"));
		ui_signal_from_box(back);
		ui_build_end(ui);
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(100.f, 100.f), .max = v2(100.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		CHECK(ui_box_find_deepest(root, v2(10.f, 10.f)) == front, "hit discovery follows paint z instead of append order");
		CHECK(ui_box_find_deepest(root, v2(50.f, 50.f)) == back, "an intercept only captures inside its clipped rectangle");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	SCRATCH_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(112, 100), .mouse_position = v2i(50, 50) };
		UI_Context *ui = playground_test_ui_create(&arena, &window);
		ui_begin_frame(ui);
		PlaygroundOverlappingScrollTest overlap = playground_build_overlapping_test_scrolls(ui);
		ui_box_measure(overlap.root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(overlap.root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		ui_end_frame(ui);

		window.mouse_wheel.y = -1;
		ui_begin_frame(ui);
		ui->frame_elapsed = 0.f;
		overlap = playground_build_overlapping_test_scrolls(ui);
		CHECK(!ui_box_contains_hot(overlap.content->root) && ui_box_contains_hot(overlap.overlay->root), "wheel ownership follows the resolved hot box ancestry");
		CHECK(playground_near(overlap.content->target, 0.f), "a content scroll box cannot consume wheel input through an overlay");
		CHECK(playground_near(overlap.overlay->target, 48.f) && ui->mouse_wheel_consumed, "the overlay scroll box receives and consumes wheel input");
		ui_box_measure(overlap.root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(overlap.root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	SCRATCH_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(100, 100), .mouse_position = v2i(10, 10) };
		UI_Context *ui = playground_test_ui_create(&arena, &window);
		ui_begin_frame(ui);
		UI_BoxDesc root_desc = ui_defaults();
		root_desc.layout = &UI_FlatLayoutHooks;
		UI_Box *root = ui_build_begin(ui, UI_KEY("overlay interaction priority"), LIT("overlay interaction priority"), root_desc);
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fixed(100.f));
		ui_size(ui, AXIS_Y, ui_fixed(100.f));
		UI_Box *underlying = ui_box_make(ui, 1, LIT("underlying"));
		ui_signal_from_box(underlying);
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fixed(100.f));
		ui_size(ui, AXIS_Y, ui_fixed(100.f));
		ui_paint_z(ui, UI_Z_OVERLAY);
		UI_Box *overlay = ui_box_make(ui, 2, LIT("overlay"));
		ui_signal_from_box(overlay);
		ui_build_end(ui);
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(100.f, 100.f), .max = v2(100.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		UI_Id underlying_id = underlying->id;
		UI_Id overlay_id = overlay->id;
		ui_end_frame(ui);

		ui_begin_frame(ui);
		CHECK(ui_is_hot(ui, overlay_id), "the previous-frame hit resolver selects the overlay");
		UI_Response underlying_response = ui_interact(ui, underlying_id, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		CHECK(!underlying_response.hovered, "a content interaction cannot steal hover from an overlay");
		CHECK(ui_is_hot(ui, overlay_id), "a rejected content interaction preserves the overlay hot box");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	SCRATCH_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(200, 100) };
		UI_Context *ui = playground_test_ui_create(&arena, &window);
		ui_begin_frame(ui);
		ui_build_begin(ui, 1, LIT("root"), ui_defaults());
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_wrap());
		ui_size(ui, AXIS_Y, ui_wrap());
		ui_background(ui, color_srgba(0x123456));
		UI_ScrollBox *scroll = ui_scroll_box_begin(ui, 1, AXIS_Y);
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fixed(60.f));
		ui_size(ui, AXIS_Y, ui_fixed(40.f));
		ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_CLIP);
		UI_Box *content = ui_box_make(ui, 1, LIT("content"));
		ui_scroll_box_end(scroll);
		ui_clean(ui);
		ui_build_end(ui);

		vec2 measured = ui_box_measure(scroll->root, (UI_BoxConstraints) { .max = v2(200.f, 100.f) });
		ui_box_layout(scroll->root, rect_f32_from_size(measured));
		CHECK(content->desc.size[AXIS_Y].kind == UI_BOX_SIZE_PIXELS && content->desc.overflow[AXIS_Y] == UI_BOX_OVERFLOW_CLIP, "a scroll box preserves the content box's sizing and overflow policy");
		CHECK(scroll->root->paint.flags == UI_BOX_DRAW_BACKGROUND && !content->paint.flags, "scroll-box paint styles stay on the root instead of moving with its content");
		CHECK(playground_near(measured.x, 72.f) && playground_near(measured.y, 40.f), "a short scroll box wraps its content and perpendicular scrollbar");
		CHECK(playground_near(scroll->viewport->rect.w, 60.f) && playground_near(scroll->viewport->rect.h, 40.f), "a wrapping viewport retains the content's natural dimensions");

		content->desc.size[AXIS_Y] = ui_fixed(400.f);
		measured = ui_box_measure(scroll->root, (UI_BoxConstraints) { .max = v2(200.f, 100.f) });
		ui_box_layout(scroll->root, rect_f32_from_size(measured));
		CHECK(playground_near(measured.y, 100.f) && playground_near(scroll->scroll_max, 300.f), "a long scroll box clamps to its at-most constraint and exposes the remaining range");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	SCRATCH_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(30, 20) };
		UI_Context *ui = playground_test_ui_create(&arena, &window);
		ui_begin_frame(ui);
		UI_Box *root = ui_build_begin(ui, UI_KEY("frame clipping"), LIT("frame clipping"), ui_defaults());
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fixed(30.f));
		ui_size(ui, AXIS_Y, ui_fixed(20.f));
		ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
		ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_CLIP);
		ui_layout(ui, &UI_FlatLayoutHooks);
		UI_Box *frame = ui_box_begin(ui, 1, LIT("clipped frame"));
		ui_clean(ui);
		ui_rect(ui, (rect_f32) { -10.f, -5.f, 40.f, 30.f });
		UI_Box *child = ui_box_make(ui, 1, LIT("positioned child"));
		ui_clean(ui);
		ui_box_end(ui);
		ui_build_end(ui);
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(30.f, 20.f), .max = v2(30.f, 20.f) });
		ui_box_layout(root, (rect_f32) { 100.f, 50.f, 30.f, 20.f });
		CHECK(playground_near(child->rect.x, 90.f) && playground_near(child->rect.y, 45.f), "frame positions are relative to a nonzero parent viewport origin");
		CHECK(playground_near(frame->content_bounds.x, 90.f) && playground_near(frame->content_bounds.y, 45.f) && playground_near(frame->content_bounds.w, 40.f) && playground_near(frame->content_bounds.h, 30.f), "a frame records positioned content extending before and after its viewport");
		CHECK(playground_near(child->clip_rect.x, 100.f) && playground_near(child->clip_rect.y, 50.f) && playground_near(child->clip_rect.w, 30.f) && playground_near(child->clip_rect.h, 20.f), "frame overflow clips a positioned child on every edge");
		CHECK(playground_near(child->state->hit_rect.x, 100.f) && playground_near(child->state->hit_rect.y, 50.f) && playground_near(child->state->hit_rect.w, 30.f) && playground_near(child->state->hit_rect.h, 20.f), "positioned frame clipping commits the matching interaction rectangle");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	SCRATCH_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(112, 100) };
		UI_Context *ui = playground_test_ui_create(&arena, &window);

		ui_begin_frame(ui);
		UI_Box *root = 0;
		UI_ScrollBox *scroll = playground_build_test_scroll(&arena, ui, &root);
		UI_BoxState *root_state = root->state;
		CHECK(root->key == UI_KEY("test scroll") && root_state && !root->has_previous, "a string-keyed box acquires new persistent state");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		CHECK(playground_near(root_state->rect.w, 112.f) && playground_near(root_state->rect.h, 100.f), "layout commits finished box geometry into persistent state");
		CHECK(scroll->root->child_count == 2 && scroll->root->first == scroll->viewport && scroll->root->last == scroll->track && scroll->content->parent == scroll->viewport, "a scroll box composes an internal viewport, one content box, and a sibling scrollbar");
		CHECK(!scroll->has_previous && playground_near(scroll->scroll_max, 300.f), "a new scroll box computes its current logical range");
		CHECK(playground_near(scroll->thumb->rect.h, 24.f), "the first layout resolves the scrollbar thumb");
		CHECK(scroll->track->paint.flags == UI_BOX_DRAW_BACKGROUND && scroll->thumb->paint.flags == UI_BOX_DRAW_BACKGROUND, "scrollbar track and thumb carry generic box appearance");
		ui_end_frame(ui);

		window.mouse_wheel.y = -1;
		ui_begin_frame(ui);
		ui->frame_elapsed = 0.055f;
		scroll = playground_build_test_scroll(&arena, ui, &root);
		CHECK(root->state == root_state && root->has_previous, "the next frame recovers the same box state and previous geometry");
		CHECK(scroll->has_previous && playground_near(scroll->viewport->state->content_size.y - scroll->viewport->state->viewport.h, 300.f), "the next scroll box derives its range from persisted viewport geometry");
		CHECK(playground_near(scroll->target, 48.f) && playground_near(scroll->offset, 24.f), "wheel input updates context-owned target and time-invariant offset before layout");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		CHECK(playground_near(scroll->content->rect.y, -24.f) && playground_near(scroll->content->first->rect.y, -24.f), "the viewport translates the complete content subtree by the persistent offset");
		ui_end_frame(ui);

		window.mouse_wheel.y = 0;
		window.mouse_position = v2i(106, 18);
		ui_begin_frame(ui);
		((Input_State *)ui->input)->keys[OS_Key_MouseLeft] = INPUT_KEY_PRESSED | INPUT_KEY_DOWN;
		ui->frame_elapsed = 0.f;
		scroll = playground_build_test_scroll(&arena, ui, &root);
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		CHECK(ui_is_active(ui, scroll->thumb->id), "the scrollbar thumb exclusively captures the mouse");
		ui_end_frame(ui);

		window.mouse_position.y += 50;
		ui_begin_frame(ui);
		scroll = playground_build_test_scroll(&arena, ui, &root);
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		CHECK(playground_near(scroll->offset, 24.f + 50.f * 300.f / 70.f), "thumb dragging maps mouse travel into the persistent logical range");
		((Input_State *)ui->input)->keys[OS_Key_MouseLeft] = INPUT_KEY_RELEASED;
		ui_end_frame(ui);

		ui_begin_frame(ui);
		scroll = playground_build_test_scroll(&arena, ui, &root);
		UI_Id unrelated_active = ui_id_child(UI_ID_NONE, UI_KEY("unrelated active"));
		ui->active = unrelated_active;
		ui_scroll_box_reset(scroll);
		CHECK(ui_id_equal(ui->active, unrelated_active), "resetting a scroll box does not cancel an unrelated active interaction");
		ui->active = UI_ID_NONE;
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		CHECK(playground_near(scroll->offset, 0.f) && playground_near(scroll->content->rect.y, 0.f) && playground_near(scroll->thumb->rect.y, scroll->track->viewport.y), "reset updates the translated content and box scrollbar geometry");
		ui_end_frame(ui);

		ui_begin_frame(ui);
		scroll = playground_build_test_scroll(&arena, ui, &root);
		CHECK(playground_near(scroll->offset, 0.f) && playground_near(scroll->target, 0.f), "reset persists through the viewport box state");
		scroll->content->first->desc.size[AXIS_Y] = ui_fixed(800.f);
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		CHECK(playground_near(scroll->thumb->rect.h, 24.f), "track preparation sizes the thumb from current-frame content instead of cached geometry");
		ui_end_frame(ui);

		window.mouse_position = v2i(106, 80);
		ui_begin_frame(ui);
		((Input_State *)ui->input)->keys[OS_Key_MouseLeft] = INPUT_KEY_PRESSED | INPUT_KEY_DOWN;
		ui->frame_elapsed = 0.f;
		scroll = playground_build_test_scroll(&arena, ui, &root);
		CHECK(ui_is_active(ui, scroll->track->id) && playground_near(scroll->target, 85.f), "the scrollbar track is an ordinary signaled box with page-step behavior");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		((Input_State *)ui->input)->keys[OS_Key_MouseLeft] = INPUT_KEY_RELEASED;
		ui_end_frame(ui);

		ui_begin_frame(ui);
		ui->frame_elapsed = 0.f;
		scroll = playground_build_test_scroll(&arena, ui, &root);
		scroll->content->first->desc.size[AXIS_Y] = ui_fixed(20.f);
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(112.f, 100.f), .max = v2(112.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 112.f, 100.f });
		CHECK(playground_near(scroll->scroll_max, 0.f) && playground_near(scroll->offset, 0.f) && playground_near(scroll->target, 0.f), "shrinking content clamps stale scroll state during the current layout");
		CHECK(playground_near(scroll->content->rect.y, 0.f), "shortened content is immediately visible instead of retaining a stale translation");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	SCRATCH_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(200, 200), .mouse_position = v2i(50, 150) };
		UI_Context *ui = playground_test_ui_create(&arena, &window);

		ui_begin_frame(ui);
		PlaygroundNestedScrollTest nested = playground_build_nested_test_scroll(ui);
		ui_box_measure(nested.root, (UI_BoxConstraints) { .min = v2(200.f, 200.f), .max = v2(200.f, 200.f) });
		ui_box_layout(nested.root, (rect_f32) { 0.f, 0.f, 200.f, 200.f });
		CHECK(playground_near(nested.outer->scroll_max, 300.f) && playground_near(nested.inner->scroll_max, 300.f), "nested scroll boxes compute independent ranges");
		nested.inner->root->state->view_offset.y = nested.inner->scroll_max;
		nested.inner->root->state->view_target.y = nested.inner->scroll_max;
		ui_end_frame(ui);

		window.mouse_wheel.y = -1;
		ui_begin_frame(ui);
		ui->frame_elapsed = 0.f;
		nested = playground_build_nested_test_scroll(ui);
		CHECK(playground_near(nested.inner->target, 300.f) && playground_near(nested.outer->target, 48.f) && ui->mouse_wheel_consumed, "wheel input bubbles to the outer scroll box when the hovered inner box is at its boundary");
		ui_box_measure(nested.root, (UI_BoxConstraints) { .min = v2(200.f, 200.f), .max = v2(200.f, 200.f) });
		ui_box_layout(nested.root, (rect_f32) { 0.f, 0.f, 200.f, 200.f });
		nested.inner->root->state->view_offset.y = 200.f;
		nested.inner->root->state->view_target.y = 200.f;
		nested.outer->root->state->view_offset.y = 0.f;
		nested.outer->root->state->view_target.y = 0.f;
		ui_end_frame(ui);

		ui_begin_frame(ui);
		ui->frame_elapsed = 0.f;
		nested = playground_build_nested_test_scroll(ui);
		CHECK(playground_near(nested.inner->target, 248.f) && playground_near(nested.outer->target, 0.f) && ui->mouse_wheel_consumed, "the hovered inner scroll box consumes wheel input while it still has room");
		ui_box_measure(nested.root, (UI_BoxConstraints) { .min = v2(200.f, 200.f), .max = v2(200.f, 200.f) });
		ui_box_layout(nested.root, (rect_f32) { 0.f, 0.f, 200.f, 200.f });
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	SCRATCH_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(100, 100), .mouse_position = v2i(10, 10) };
		UI_Context *ui = playground_test_ui_create(&arena, &window);
		UI_Box *root = 0;
		UI_Box *box = 0;

		ui_begin_frame(ui);
		UI_Response response = playground_build_test_signal(&arena, ui, &root, &box);
		CHECK(!response.hovered && !response.pressed && !response.held, "a new box does not interact through zero or uninitialized geometry");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(100.f, 100.f), .max = v2(100.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		CHECK(playground_near(box->state->hit_rect.w, 50.f) && playground_near(box->state->hit_rect.h, 30.f), "layout persists the box's clipped interaction rectangle");
		ui_end_frame(ui);

		ui_begin_frame(ui);
		((Input_State *)ui->input)->keys[OS_Key_MouseLeft] = INPUT_KEY_PRESSED | INPUT_KEY_DOWN;
		response = playground_build_test_signal(&arena, ui, &root, &box);
		CHECK(response.hovered && response.pressed && response.held && ui_is_active(ui, box->id), "a box signal presses and exclusively captures through previous geometry");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(100.f, 100.f), .max = v2(100.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		ui_end_frame(ui);

		window.mouse_position = v2i(20, 15);
		ui_begin_frame(ui);
		response = playground_build_test_signal(&arena, ui, &root, &box);
		CHECK(response.hovered && response.held && !response.pressed && playground_near(response.drag_delta.x, 10.f) && playground_near(response.drag_delta.y, 5.f), "a captured box reports held state and drag displacement");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(100.f, 100.f), .max = v2(100.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		ui_end_frame(ui);

		ui_begin_frame(ui);
		((Input_State *)ui->input)->keys[OS_Key_MouseLeft] = INPUT_KEY_RELEASED;
		response = playground_build_test_signal(&arena, ui, &root, &box);
		CHECK(response.released && !response.held, "a captured box reports release before capture is cleared");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(100.f, 100.f), .max = v2(100.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		ui_end_frame(ui);
		CHECK(!ui->active.value, "mouse release clears the exclusive capture at frame end");

		ui_invalidate_layout(ui);
		ui_begin_frame(ui);
		((Input_State *)ui->input)->keys[OS_Key_MouseLeft] = INPUT_KEY_PRESSED | INPUT_KEY_DOWN;
		response = playground_build_test_signal(&arena, ui, &root, &box);
		CHECK(!box->has_previous && !response.hovered && !response.pressed, "layout invalidation rejects stale box geometry for interaction");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(100.f, 100.f), .max = v2(100.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	SCRATCH_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(120, 80) };
		UI_Context *ui = playground_test_ui_create(&arena, &window);

		ui_begin_frame(ui);
		UI_BoxDesc root_desc = ui_defaults();
		root_desc.axis = AXIS_Y;
		UI_Box *root = ui_build_begin(ui, UI_KEY("tooltip test"), LIT("root"), root_desc);

		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fixed(60.f));
		ui_size(ui, AXIS_Y, ui_fixed(40.f));
		ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
		ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_CLIP);
		UI_Box *host = ui_box_begin(ui, 1, LIT("clipped host"));

		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fixed(13.f));
		ui_background(ui, color_srgba(0x123456));
		UI_Box *before = ui_box_make(ui, 1, LIT("before tooltip"));

		UI_Box *tooltip = ui_tooltip_begin(ui, 1, v2(118.f, 78.f));
		CHECK(tooltip != 0, "the first tooltip request claims the context overlay");
		CHECK(!ui_tooltip_begin(ui, UI_KEY("nested tooltip"), v2(0.f, 0.f)), "a nested tooltip request loses arbitration without disturbing the active tooltip");
		ui_push_box_z(ui, UI_Z_OVERLAY);
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fixed(40.f));
		ui_size(ui, AXIS_Y, ui_fixed(20.f));
		UI_Box *tooltip_child = ui_box_make(ui, 1, LIT("arbitrary tooltip child"));
		ui_clean(ui);
		ui_pop_box_z(ui);
		ui_tooltip_end(ui);

		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fixed(13.f));
		ui_background(ui, color_srgba(0x123456));
		UI_Box *after = ui_box_make(ui, 2, LIT("after tooltip"));
		CHECK(!ui_tooltip_begin(ui, UI_KEY("second tooltip"), v2(0.f, 0.f)), "a second tooltip request is ignored for the current frame");
		ui_clean(ui);
		ui_box_end(ui);

		ui_clean(ui);
		UI_Box *ordinary_tail = ui_box_make(ui, 2, LIT("ordinary root tail"));
		ui_build_end(ui);
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(120.f, 80.f), .max = v2(120.f, 80.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 120.f, 80.f });

		CHECK(before->parent == host && after->parent == host && host->child_count == 2, "tooltip construction restores the caller's structural parent");
		CHECK(!ui_id_equal(before->id, tooltip->id) && before->state != tooltip->state, "overlay namespacing prevents tooltip state from colliding with the owner's local keys");
		CHECK(before->desc.size[AXIS_X].value == 13.f && after->desc.size[AXIS_X].value == 13.f && before->paint.flags == after->paint.flags, "independent owner declarations remain identical across tooltip construction");
		CHECK(ui->content_root->parent == root && ui->overlay_root->parent == root && root->first == ui->content_root && root->last == ui->overlay_root && ordinary_tail->parent == ui->content_root && ui->content_root->last == ordinary_tail, "the frame root separates ordinary linear content from overlays");
		CHECK(tooltip->parent == ui->overlay_root && tooltip_child->parent == tooltip, "tooltip children live outside the clipped source subtree");
		CHECK(tooltip->paint.flags == UI_BOX_DRAW_BACKDROP, "tooltips use the compositor backdrop without a background fill or border");
		CHECK(tooltip_child->paint.z == UI_Z_OVERLAY, "the explicit z stack places tooltip descendants at overlay depth");
		CHECK(playground_near(tooltip->rect.x, 56.f) && playground_near(tooltip->rect.y, 40.f), "a measured tooltip clamps against the bottom-right window edges");
		CHECK(playground_near(tooltip->clip_rect.x, root->clip_rect.x) && playground_near(tooltip->clip_rect.y, root->clip_rect.y) && playground_near(tooltip->clip_rect.w, root->clip_rect.w) && playground_near(tooltip->clip_rect.h, root->clip_rect.h), "the overlay escapes panel clipping while retaining the window clip");
		CHECK(ui_box_find_deepest(root, v2(1.f, 1.f)) == host, "an empty portion of the overlay frame passes hit discovery through to ordinary content");
		CHECK(ui_box_find_deepest(root, v2(tooltip_child->rect.x + 1.f, tooltip_child->rect.y + 1.f)) == tooltip_child, "actual overlay content wins hit discovery over the underlying tree");
		ui_end_frame(ui);

		ui_begin_frame(ui);
		root = ui_build_begin(ui, UI_KEY("tooltip test"), LIT("root"), root_desc);
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fixed(60.f));
		ui_size(ui, AXIS_Y, ui_fixed(40.f));
		ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
		ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_CLIP);
		host = ui_box_begin(ui, 1, LIT("clipped host"));
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fixed(13.f));
		ui_background(ui, color_srgba(0x123456));
		before = ui_box_make(ui, 1, LIT("before tooltip"));
		tooltip = ui_tooltip_begin(ui, 1, v2(-100.f, -100.f));
		ui_push_box_z(ui, UI_Z_OVERLAY);
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fixed(40.f));
		ui_size(ui, AXIS_Y, ui_fixed(20.f));
		tooltip_child = ui_box_make(ui, 1, LIT("arbitrary tooltip child"));
		ui_clean(ui);
		ui_pop_box_z(ui);
		ui_tooltip_end(ui);
		ui_clean(ui);
		ui_box_end(ui);
		ui_build_end(ui);
		CHECK(before->has_previous && tooltip->has_previous && tooltip_child->has_previous, "tooltip and owner geometry persist independently under stable IDs");
		CHECK(playground_near(before->state->rect.w, 13.f) && playground_near(tooltip->state->rect.w, 56.f), "the tooltip does not overwrite the owner's previous rectangle");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(120.f, 80.f), .max = v2(120.f, 80.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 120.f, 80.f });
		CHECK(playground_near(tooltip->rect.x, 8.f) && playground_near(tooltip->rect.y, 8.f), "tooltip placement also clamps against the top-left window edges");
		CHECK(playground_near(tooltip->state->rect.x, 8.f) && playground_near(tooltip_child->state->rect.x, 16.f), "clamped tooltip geometry commits after prepare-layout translation");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	SCRATCH_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(100, 100) };
		UI_Context *ui = playground_test_ui_create(&arena, &window);
		ui_begin_frame(ui);
		UI_BoxDesc root_desc = ui_defaults();
		root_desc.axis = AXIS_X;
		root_desc.horz_padd[0] = root_desc.horz_padd[1] = 10.f;
		root_desc.overflow[AXIS_X] = UI_BOX_OVERFLOW_CLIP;
		root_desc.overflow[AXIS_Y] = UI_BOX_OVERFLOW_CLIP;
		UI_Box *root = ui_build_begin(ui, UI_KEY("clipped scene root"), LIT("clipped scene root"), root_desc);
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fixed(200.f));
		ui_size(ui, AXIS_Y, ui_fixed(20.f));
		UI_Box *child = ui_box_make(ui, 1, LIT("oversized child"));
		ui_build_end(ui);
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(100.f, 100.f), .max = v2(100.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		CHECK(playground_near(ui->content_root->rect.x, 10.f) && playground_near(ui->content_root->rect.w, 80.f) && playground_near(ui->content_root->rect.h, 100.f) && playground_near(ui->overlay_root->rect.x, 10.f) && playground_near(ui->overlay_root->rect.w, 80.f) && playground_near(ui->overlay_root->rect.h, 100.f), "the internal content and overlay roots both receive the complete padded scene viewport");
		CHECK(ui_id_equal(child->id, ui_id_child(root->id, 1)), "the internal content root does not perturb caller box IDs");
		CHECK(playground_near(child->clip_rect.x, 10.f) && playground_near(child->clip_rect.w, 80.f) && playground_near(child->state->hit_rect.x, 10.f) && playground_near(child->state->hit_rect.w, 80.f) && playground_near(child->state->hit_rect.h, 20.f), "root overflow clipping propagates through the internal linear content root");
		CHECK(ui_box_find_deepest(root, v2(50.f, 10.f)) == child, "an empty structural overlay does not hide ordinary content from hit discovery");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	SCRATCH_SCOPE(&arena)
	{
		UI_BoxDesc desc = ui_defaults();
		UI_Builder builder;
		UI_Box *root = ui_box_builder_begin(&builder, &arena, 0, 1, LIT("root"), desc);
		ui_builder_clean(&builder);
		UI_Box *a = ui_builder_box_begin_desc(&builder, 1, LIT("a"), desc);
		ui_builder_clean(&builder);
		UI_Box *b = ui_builder_box_make_desc(&builder, 1, LIT("b"), desc);
		ui_builder_clean(&builder);
		UI_Box *c = ui_builder_box_make_desc(&builder, 2, LIT("c"), desc);
		ui_builder_box_end(&builder);
		ui_builder_clean(&builder);
		UI_Box *d = ui_builder_box_make_desc(&builder, 2, LIT("d"), desc);
		ui_box_builder_end(&builder);
		CHECK(root->child_count == 2 && root->first == a && root->last == d && a->parent == root && d->parent == root && a->next == d && d->prev == a, "builder links root children in both directions and in order");
		CHECK(a->child_count == 2 && a->first == b && a->last == c && b->parent == a && c->parent == a && b->next == c && c->prev == b, "builder links nested children in both directions and in order");
		CHECK(root->id.value && ui_id_equal(a->id, ui_id_child(root->id, 1)) && ui_id_equal(b->id, ui_id_child(a->id, 1)), "box IDs derive from their structural parent and construction key");
		CHECK(!ui_id_equal(b->id, d->id), "the same local key in a different structural scope produces a different ID");
	}

	SCRATCH_SCOPE(&arena)
	{
		UI_Builder builder;
		UI_Box *root = ui_box_builder_begin(&builder, &arena, 0, 1, LIT("root"), ui_defaults());
		ui_builder_clean(&builder);
		ui_builder_background(&builder, color_srgba(0x123456));
		UI_Box *background = ui_builder_box_make(&builder, 1, LIT("background"));
		ui_builder_clean(&builder);
		ui_builder_background(&builder, color_srgba(0x123456));
		ui_builder_border(&builder, color_srgba(0xABCDEF), 2.f);
		ui_builder_roundness(&builder, 4.f);
		ui_builder_inset_shadow(&builder, 0.25f);
		UI_Box *styled = ui_builder_box_make(&builder, 2, LIT("styled"));
		ui_builder_clean(&builder);
		ui_builder_background(&builder, color_srgba(0x123456));
		UI_Box *restored = ui_builder_box_make(&builder, 3, LIT("restored"));
		ui_builder_push_box_z(&builder, 100);
		ui_builder_clean(&builder);
		ui_builder_paint_z(&builder, 3);
		UI_Box *scoped_z = ui_builder_box_make(&builder, 4, LIT("scoped z"));
		ui_builder_push_box_z(&builder, 200);
		ui_builder_clean(&builder);
		ui_builder_paint_z(&builder, 3);
		UI_Box *nested_z = ui_builder_box_make(&builder, 5, LIT("nested z"));
		ui_builder_pop_box_z(&builder);
		ui_builder_clean(&builder);
		ui_builder_paint_z(&builder, 3);
		UI_Box *restored_z = ui_builder_box_make(&builder, 6, LIT("restored z"));
		ui_builder_pop_box_z(&builder);
		ui_builder_clean(&builder);
		ui_builder_paint_z(&builder, 3);
		UI_Box *local_z = ui_builder_box_make(&builder, 7, LIT("local z"));
		ui_builder_clean(&builder);
		ui_box_builder_end(&builder);
		CHECK(!root->paint.flags, "the box root snapshots the initial paint description");
		CHECK(background->paint.flags == UI_BOX_DRAW_BACKGROUND, "a box snapshots the active background");
		CHECK(styled->paint.flags == (UI_BOX_DRAW_BACKGROUND | UI_BOX_DRAW_BORDER | UI_BOX_DRAW_INSET_SHADOW) && playground_near(styled->paint.border_width, 2.f) && playground_near(styled->paint.roundness, 4.f) && playground_near(styled->paint.inset_shadow, 0.25f), "a complete paint declaration composes on one box");
		CHECK(restored->paint.flags == UI_BOX_DRAW_BACKGROUND && playground_near(restored->paint.roundness, 0.f) && playground_near(restored->paint.inset_shadow, 0.f), "an independent paint declaration does not retain unrelated attributes");
		CHECK(scoped_z->paint.z == 103 && nested_z->paint.z == 203 && restored_z->paint.z == 103 && local_z->paint.z == 3, "the manual box z stack offsets local paint depth and restores the previous top");
	}

	SCRATCH_SCOPE(&arena)
	{
		UI_BoxDesc desc = ui_defaults();
		UI_Builder builder;
		UI_Box *root = ui_box_builder_begin(&builder, &arena, 0, 1, LIT("root"), desc);
		ui_builder_push_id(&builder, 100);
		ui_builder_clean(&builder);
		UI_Box *first = ui_builder_box_make_desc(&builder, 1, LIT("first"), desc);
		ui_builder_pop_id(&builder);
		ui_builder_push_id(&builder, 200);
		ui_builder_clean(&builder);
		UI_Box *second = ui_builder_box_make_desc(&builder, 1, LIT("second"), desc);
		ui_builder_pop_id(&builder);
		ui_box_builder_end(&builder);
		CHECK(root->child_count == 2 && !ui_id_equal(first->id, second->id), "explicit ID scopes disambiguate repeated component-local keys");
	}

	SCRATCH_SCOPE(&arena)
	{
		UI_Builder builder;
		UI_Box *root = ui_box_builder_begin(&builder, &arena, 0, 1, LIT("root"), ui_defaults());
		ui_builder_clean(&builder);
		ui_builder_size(&builder, AXIS_Y, ui_grow(1.f));
		ui_builder_size(&builder, AXIS_X, ui_fixed(60.f));
		UI_Box *first = ui_builder_box_make(&builder, 1, LIT("first"));
		ui_builder_clean(&builder);
		ui_builder_size(&builder, AXIS_Y, ui_grow(1.f));
		ui_builder_size(&builder, AXIS_X, ui_fixed(60.f));
		UI_Box *second = ui_builder_box_make(&builder, 2, LIT("second"));
		ui_builder_clean(&builder);
		ui_builder_size(&builder, AXIS_Y, ui_grow(1.f));
		ui_builder_size(&builder, AXIS_X, ui_fixed(10.f));
		UI_Box *nested = ui_builder_box_make(&builder, 3, LIT("nested"));
		ui_builder_clean(&builder);
		ui_builder_size(&builder, AXIS_Y, ui_grow(1.f));
		ui_builder_size(&builder, AXIS_X, ui_fixed(60.f));
		UI_Box *restored = ui_builder_box_make(&builder, 4, LIT("restored"));
		ui_builder_clean(&builder);
		UI_Box *defaults = ui_builder_box_make(&builder, 5, LIT("defaults"));
		ui_builder_clean(&builder);
		ui_box_builder_end(&builder);
		CHECK(root->child_count == 5 && first->desc.size[AXIS_X].value == 60.f && second->desc.size[AXIS_X].value == 60.f, "repeated independent declarations produce matching box descriptions");
		CHECK(nested->desc.size[AXIS_X].value == 10.f && restored->desc.size[AXIS_X].value == 60.f, "an isolated declaration does not modify a later repeated declaration");
		CHECK(restored->desc.size[AXIS_Y].kind == UI_BOX_SIZE_FILL && defaults->desc.size[AXIS_X].kind == UI_BOX_SIZE_CONTENT, "clean construction starts from the complete default description");
	}

	SCRATCH_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(300, 100) };
		UI_Context *ui = playground_test_ui_create(&arena, &window);
		ui_begin_frame(ui);
		ui_build_begin(ui, 1, LIT("root"), ui_defaults());
		UI_BoxTableColumn columns[] = {
			ui_box_table_content(),
			ui_box_table_fixed(50.f),
			ui_box_table_flex(1.f),
		};
		ui_clean(ui);
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
		r0c1->desc.layout = &UI_FlatLayoutHooks;
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_fixed(10.f));
		ui_size(ui, AXIS_Y, ui_fixed(10.f));
		ui_align(ui, AXIS_X, 1.f);
		UI_Box *right_aligned = ui_box_make(ui, 1, LIT("right aligned"));
		ui_box_table_cell_end(&table);
		UI_Box *r0c2 = ui_box_table_cell_begin(&table);
		r0c2->intrinsic_size.x = 20.f;
		ui_box_table_cell_end(&table);
		ui_box_table_row_end(&table);
		ui_box_table_row_begin(&table, 2);
		UI_Box *r1c0 = ui_box_table_cell_begin(&table);
		u32 nested_measure_count = 0;
		ui_clean(ui);
		UI_Box *nested = ui_box_make(ui, 1, LIT("nested"));
		nested->hooks = &playground_test_counted_ops;
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
		UI_Box *second_row = table_box->first->next;
		UI_Box *r1c1 = second_row->first->next;
		UI_Box *r1c2 = second_row->last;
		CHECK(playground_near(r0c0->rect.w, 70.f) && playground_near(r1c0->rect.w, 70.f), "content table tracks use the widest cell subtree across rows");
		CHECK(playground_near(r0c1->rect.w, 50.f) && playground_near(r1c1->rect.w, 50.f), "fixed table tracks remain aligned across rows");
		CHECK(playground_near(r0c2->rect.w, 170.f) && playground_near(r1c2->rect.w, 170.f), "flex table tracks receive the remaining width");
		CHECK(playground_near(r0c1->rect.x, r1c1->rect.x) && playground_near(r0c2->rect.x, r1c2->rect.x), "cell boundaries align across every table row");
		CHECK(playground_near(right_aligned->rect.x + right_aligned->rect.w, r0c1->viewport.x + r0c1->viewport.w), "a table cell can use frame alignment without involving linear layout");
		CHECK(nested_measure_count == 1, "table cells are measured once before shared tracks are resolved");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	SCRATCH_SCOPE(&arena)
	{
		UI_Box *root = playground_test_row(&arena, 400.f, ui_flex(0.f, 3.f), 300.f, 0.f, UI_BOX_INFINITY, ui_flex(0.f, 1.f), 300.f, 0.f, UI_BOX_INFINITY);
		CHECK(playground_near(root->first->rect.w, 150.f) && playground_near(root->last->rect.w, 250.f), "negative space follows shrink weights");
	}

	SCRATCH_SCOPE(&arena)
	{
		UI_BoxDesc desc = ui_defaults();
		desc.axis = AXIS_X;
		UI_Builder builder;
		UI_Box *root = ui_box_builder_begin(&builder, &arena, 0, 1, LIT("root"), desc);
		root->intrinsic_size.x = 270.f;
		ui_builder_clean(&builder);
		UI_Box *child = ui_builder_box_make_desc(&builder, 1, LIT("child"), desc);
		child->intrinsic_size.x = 100.f;
		ui_box_builder_end(&builder);
		ui_box_measure(root, (UI_BoxConstraints) { .max = v2(UI_BOX_INFINITY, UI_BOX_INFINITY) });
		CHECK(playground_near(root->measured_size.x, 270.f), "intrinsic basis remains independent from child content");
	}

	SCRATCH_SCOPE(&arena)
	{
		UI_Box *root = playground_test_row(&arena, 300.f, ui_flex(0.f, 3.f), 300.f, 200.f, UI_BOX_INFINITY, ui_flex(0.f, 1.f), 300.f, 0.f, UI_BOX_INFINITY);
		CHECK(playground_near(root->first->rect.w, 200.f) && playground_near(root->last->rect.w, 100.f), "shrink deficit redistributes after a child reaches its minimum");
	}

	SCRATCH_SCOPE(&arena)
	{
		UI_Box *root = playground_test_row(&arena, 400.f, ui_grow(1.f), 0.f, 0.f, 100.f, ui_grow(1.f), 0.f, 0.f, UI_BOX_INFINITY);
		CHECK(playground_near(root->first->rect.w, 100.f) && playground_near(root->last->rect.w, 300.f), "grow surplus redistributes after a child reaches its maximum");
	}

	SCRATCH_SCOPE(&arena)
	{
		UI_BoxDesc root_desc = playground_fill_desc();
		root_desc.axis = AXIS_X;
		root_desc.gap = 20.f;
		UI_Builder builder;
		UI_Box *root = ui_box_builder_begin(&builder, &arena, 0, 1, LIT("root"), root_desc);

		UI_BoxDesc fixed = playground_fill_desc();
		fixed.size[AXIS_X] = ui_fixed(100.f);
		fixed.horz_margin[0] = 10.f;
		fixed.horz_margin[1] = 10.f;
		ui_builder_clean(&builder);
		ui_builder_box_make_desc(&builder, 1, LIT("fixed"), fixed);

		UI_BoxDesc fill = playground_fill_desc();
		fill.size[AXIS_X] = ui_grow(1.f);
		ui_builder_clean(&builder);
		ui_builder_box_make_desc(&builder, 2, LIT("fill"), fill);

		ui_box_builder_end(&builder);
		ui_box_measure(root, (UI_BoxConstraints) { .max = v2(400.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 0.f, 0.f, 400.f, 100.f });
		CHECK(playground_near(root->last->rect.w, 260.f), "margins and gaps are deducted before distributing free space");
	}

	SCRATCH_SCOPE(&arena)
	{
		UI_BoxDesc root_desc = ui_defaults();
		root_desc.layout = &UI_FlatLayoutHooks;
		UI_Builder builder;
		UI_Box *root = ui_box_builder_begin(&builder, &arena, 0, 1, LIT("root"), root_desc);

		UI_BoxDesc fixed = ui_defaults();
		fixed.size[AXIS_X] = ui_fixed(50.f);
		fixed.size[AXIS_Y] = ui_fixed(20.f);
		ui_builder_clean(&builder);
		UI_Box *first = ui_builder_box_make_desc(&builder, 1, LIT("first"), fixed);

		ui_builder_clean(&builder);
		ui_builder_rect(&builder, (rect_f32) { 35.f, 20.f, 80.f, 40.f });
		UI_Box *positioned = ui_builder_box_begin(&builder, 2, LIT("positioned"));

		UI_BoxDesc contents = playground_fill_desc();
		ui_builder_clean(&builder);
		UI_Box *nested = ui_builder_box_make_desc(&builder, 1, LIT("nested"), contents);
		ui_builder_clean(&builder);
		ui_builder_box_end(&builder);

		fixed.size[AXIS_X] = ui_fixed(60.f);
		fixed.size[AXIS_Y] = ui_fixed(10.f);
		ui_builder_clean(&builder);
		UI_Box *last = ui_builder_box_make_desc(&builder, 3, LIT("last"), fixed);

		UI_BoxDesc fill = playground_fill_desc();
		fill.horz_margin[0] = fill.horz_margin[1] = 5.f;
		fill.vert_margin[0] = fill.vert_margin[1] = 5.f;
		ui_builder_clean(&builder);
		UI_Box *filled = ui_builder_box_make_desc(&builder, 4, LIT("filled"), fill);

		ui_box_builder_end(&builder);
		ui_box_measure(root, (UI_BoxConstraints) { .max = v2(UI_BOX_INFINITY, UI_BOX_INFINITY) });
		CHECK(playground_near(root->measured_size.x, 115.f) && playground_near(root->measured_size.y, 60.f), "a wrapping frame measures the maximum positioned child extent instead of summing its children");
		ui_box_layout(root, rect_f32_from_size(root->measured_size));
		CHECK(playground_near(root->content_size.x, 115.f) && playground_near(root->content_size.y, 60.f), "frame content size retains the viewport-relative extent represented by positioned content bounds");
		ui_box_measure(root, (UI_BoxConstraints) { .min = v2(300.f, 100.f), .max = v2(300.f, 100.f) });
		ui_box_layout(root, (rect_f32) { 10.f, 5.f, 300.f, 100.f });
		CHECK(playground_near(first->rect.x, 10.f) && playground_near(first->rect.y, 5.f) && playground_near(last->rect.x, 10.f) && playground_near(last->rect.y, 5.f), "ordinary frame children overlap at the parent viewport origin");
		CHECK(playground_near(positioned->rect.x, 45.f) && playground_near(positioned->rect.y, 25.f) && playground_near(positioned->rect.w, 80.f) && playground_near(positioned->rect.h, 40.f), "ui_builder_rect assigns an explicit frame-relative rectangle");
		CHECK(playground_near(nested->rect.x, 45.f) && playground_near(nested->rect.y, 25.f) && playground_near(nested->rect.w, 80.f) && playground_near(nested->rect.h, 40.f), "a positioned frame child may lay out its own descendants linearly");
		CHECK(playground_near(filled->rect.x, 15.f) && playground_near(filled->rect.y, 10.f) && playground_near(filled->rect.w, 290.f) && playground_near(filled->rect.h, 90.f), "a frame child fills the independently available area inside its margins");
	}

	SCRATCH_SCOPE(&arena)
	{
		UI_BoxDesc root_desc = ui_defaults();
		root_desc.layout = &UI_FlatLayoutHooks;
		UI_Builder builder;
		UI_Box *root = ui_box_builder_begin(&builder, &arena, 0, 1, LIT("alignment frame"), root_desc);

		ui_builder_clean(&builder);
		ui_builder_size(&builder, AXIS_X, ui_fixed(20.f));
		ui_builder_size(&builder, AXIS_Y, ui_fixed(10.f));
		ui_builder_margin(&builder, AXIS_X, 5.f, 15.f);
		ui_builder_margin(&builder, AXIS_Y, 7.f, 3.f);
		ui_builder_align(&builder, AXIS_X, 1.f);
		ui_builder_align(&builder, AXIS_Y, 0.5f);
		UI_Box *aligned = ui_builder_box_make(&builder, 1, LIT("aligned"));

		UI_BoxDesc mixed_desc = ui_defaults();
		mixed_desc.size[AXIS_X] = ui_fixed(30.f);
		mixed_desc.size[AXIS_Y] = ui_fixed(15.f);
		mixed_desc.vert_margin[0] = 2.f;
		mixed_desc.vert_margin[1] = 8.f;
		mixed_desc.position[AXIS_X] = (UI_BoxPosition) { .kind = UI_BOX_POSITION_PARENT, .value = -10.f };
		mixed_desc.position[AXIS_Y] = (UI_BoxPosition) { .kind = UI_BOX_POSITION_ALIGN, .value = 1.f };
		ui_builder_clean(&builder);
		UI_Box *mixed = ui_builder_box_make_desc(&builder, 2, LIT("mixed position"), mixed_desc);
		ui_builder_clean(&builder);

		ui_box_builder_end(&builder);
		vec2 measured = ui_box_measure(root, (UI_BoxConstraints) { .max = v2(UI_BOX_INFINITY, UI_BOX_INFINITY) });
		CHECK(playground_near(measured.x, 40.f) && playground_near(measured.y, 25.f), "aligned frame children contribute their natural size and margins during measurement");
		ui_box_layout(root, (rect_f32) { 100.f, 50.f, 100.f, 80.f });
		CHECK(playground_near(aligned->rect.x, 165.f) && playground_near(aligned->rect.y, 87.f), "frame alignment uses rect_f32_align inside the child's margins");
		CHECK(playground_near(mixed->rect.x, 90.f) && playground_near(mixed->rect.y, 107.f), "frame positioning kinds compose independently on each axis");
	}

	SCRATCH_SCOPE(&arena)
	{
		UI_Box *root = playground_test_row(&arena, 100.f, ui_flex(0.f, 1.f), 200.f, 80.f, UI_BOX_INFINITY, ui_flex(0.f, 1.f), 200.f, 80.f, UI_BOX_INFINITY);
		CHECK(playground_near(root->first->rect.w, 80.f) && playground_near(root->last->rect.w, 80.f), "unsatisfied deficit stops at child minimums without negative sizes");
	}

	SCRATCH_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(100, 100) };
		UI_Context *ui = playground_test_ui_create(&arena, &window);
		ui_begin_frame(ui);
		UI_BoxDesc root_desc = ui_defaults();
		ui_build_begin(ui, 1, LIT("root"), root_desc);
		ui_clean(ui);
		ui_gap(ui, 8.f);
		UI_Box *list = ui_virtual_list(ui, 1, LIT("list"), (UI_VirtualListDesc) {
			.item_count = 1,
			.build_item = playground_build_test_virtual_item,
		});
		ui_build_end(ui);
		ui_box_measure(list, (UI_BoxConstraints) { .max = v2(100.f, 100.f) });
		ui_box_layout(list, (rect_f32) { 0.f, 0.f, 100.f, list->measured_size.y });
		CHECK(playground_near(list->measured_size.y, 42.f) && playground_near(list->content_size.y, 42.f), "a short virtual list wraps its logical items");
		CHECK(list->child_count == 1 && list->first->child_count == 1, "a virtual item materializes as an arbitrary box subtree");
		CHECK(ui_id_equal(list->first->id, ui_id_child(ui_id_child(list->id, 0), 1)), "a virtual item ID includes its logical item scope");
		CHECK(playground_near(list->first->rect.w, 100.f), "a virtual item can fill the viewport perpendicular to the list axis");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	SCRATCH_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(100, 100) };
		UI_Context *ui = playground_test_ui_create(&arena, &window);
		ui_begin_frame(ui);
		UI_BoxDesc root_desc = ui_defaults();
		ui_build_begin(ui, 1, LIT("root"), root_desc);
		ui_clean(ui);
		ui_gap(ui, 8.f);
		UI_Box *list = ui_virtual_list(ui, 1, LIT("list"), (UI_VirtualListDesc) {
			.item_count = 1000,
			.build_item = playground_build_test_virtual_item,
		});
		ui_build_end(ui);
		ui_box_measure(list, (UI_BoxConstraints) { .max = v2(100.f, 100.f) });
		ui_box_layout(list, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		CHECK(playground_near(list->measured_size.y, 100.f) && playground_near(list->content_size.y, 49992.f), "a long virtual list clamps while preserving its full logical extent");
		CHECK(list->child_count == 4 && ui_id_equal(list->first->id, ui_id_child(ui_id_child(list->id, 0), 1)) && ui_id_equal(list->last->id, ui_id_child(ui_id_child(list->id, 3), 1)), "a virtual list materializes only visible and overscan items");
		list->desc.overflow[AXIS_Y] = UI_BOX_OVERFLOW_CLIP;
		ui_box_layout_clipped(list, (rect_f32) { 0.f, 0.f, 100.f, 100.f }, (rect_f32) { 0.f, 0.f, 100.f, 500.f });
		CHECK(list->child_count == 4 && ui_id_equal(list->last->id, ui_id_child(ui_id_child(list->id, 3), 1)), "virtualization uses the list's effective clip instead of realizing the larger ancestor clip");
		list->desc.overflow[AXIS_Y] = UI_BOX_OVERFLOW_VISIBLE;
		ui_box_layout_clipped(list, (rect_f32) { 0.f, -500.f, 100.f, 100.f }, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		CHECK(list->child_count == 6 && !list->first->prev && !list->last->next, "scrolling rematerializes a valid linked range of logical items");
		CHECK(ui_id_equal(list->first->id, ui_id_child(ui_id_child(list->id, 8), 1)) && ui_id_equal(list->last->id, ui_id_child(ui_id_child(list->id, 13), 1)), "rematerialized virtual items retain deterministic IDs");
		ui_box_layout_clipped(list, (rect_f32) { 0.f, -49892.f, 100.f, 100.f }, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		CHECK(ui_id_equal(list->first->id, ui_id_child(ui_id_child(list->id, 995), 1)) && ui_id_equal(list->last->id, ui_id_child(ui_id_child(list->id, 999), 1)), "a translated virtual list realizes its final items at the bottom boundary");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	SCRATCH_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(100, 100) };
		UI_Context *ui = playground_test_ui_create(&arena, &window);
		u32 build_count = 0;
		ui_begin_frame(ui);
		ui_build_begin(ui, 1, LIT("root"), ui_defaults());
		ui_clean(ui);
		UI_Box *list = ui_virtual_list(ui, 1, LIT("empty list"), (UI_VirtualListDesc) {
			.user = &build_count,
			.build_item = playground_build_counted_test_virtual_item,
		});
		ui_build_end(ui);
		ui_box_measure(list, (UI_BoxConstraints) { .max = v2(100.f, 100.f) });
		ui_box_layout(list, (rect_f32) { 0.f, 0.f, 100.f, 100.f });
		CHECK(!build_count && playground_near(list->content_size.y, 0.f), "an empty virtual list does not invoke its item builder or invent content");
		CHECK(!list->child_count, "an empty virtual list materializes no boxes");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	SCRATCH_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(100, 60) };
		UI_Context *ui = playground_test_ui_create(&arena, &window);
		ui_begin_frame(ui);
		ui_build_begin(ui, 1, LIT("root"), ui_defaults());
		ui_clean(ui);
		ui_axis(ui, AXIS_X);
		ui_gap(ui, 5.f);
		UI_Box *list = ui_virtual_list(ui, 1, LIT("horizontal list"), (UI_VirtualListDesc) {
			.item_count = 10,
			.build_item = playground_build_test_horizontal_virtual_item,
		});
		ui_build_end(ui);
		ui_box_measure(list, (UI_BoxConstraints) { .max = v2(100.f, 60.f) });
		ui_box_layout(list, (rect_f32) { 0.f, 0.f, 100.f, 60.f });
		CHECK(playground_near(list->content_size.x, 345.f) && playground_near(list->first->rect.h, 60.f), "virtual-list extent and perpendicular fill work on the horizontal axis");
		ui_box_layout_clipped(list, (rect_f32) { -175.f, 0.f, 100.f, 60.f }, (rect_f32) { 0.f, 0.f, 100.f, 60.f });
		CHECK(ui_id_equal(list->first->id, ui_id_child(ui_id_child(list->id, 3), 1)), "a horizontal virtual list rematerializes the current logical range");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
	}

	SCRATCH_SCOPE(&arena)
	{
		OS_Window window = { .size = v2i(100, 60) };
		UI_Context *ui = playground_test_ui_create(&arena, &window);
		ui_begin_frame(ui);
		ui_build_begin(ui, 1, LIT("root"), ui_defaults());
		ui_clean(ui);
		ui_gap(ui, 4.f);
		UI_Box *list = ui_virtual_list(ui, 1, LIT("margined list"), (UI_VirtualListDesc) {
			.item_count = 3,
			.build_item = playground_build_test_margined_virtual_item,
		});
		ui_build_end(ui);
		ui_box_measure(list, (UI_BoxConstraints) { .max = v2(100.f, 60.f) });
		ui_box_layout(list, (rect_f32) { 0.f, 0.f, 100.f, 60.f });
		CHECK(playground_near(list->content_size.y, 140.f) && playground_near(list->first->rect.y, 5.f) && playground_near(list->first->next->rect.y, 53.f), "virtual-list item extent includes margins exactly once");
		ui_end_frame(ui);
		arena_destroy(&ui->frame_arena);
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

static b32 playground_key_pressed(const OS_Window *window, OS_Key key)
{
	for (u32 event_index = 0; event_index < os_window_event_count(window); ++event_index)
	{
		const OS_Event *event = os_window_event(window, event_index);
		if (event->type == OS_EVENT_KEY_PRESS && event->key == key && !event->repeat) return true;
	}
	return false;
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

	GFX_Renderer *renderer = gfx_create_renderer(&arena);
	GFX_Window *gfx_window = gfx_create_window(&arena, renderer, window);
	Draw_Context *draw = draw_create(&arena, renderer);
	Text_Context *text = text_create(&arena);
	Text_GFX *text_gfx = text_gfx_create(&arena, renderer, text);
	Input_State input = {};
	UI_Context *ui = ui_create(&arena, window, &input, text, draw, ui_default_theme(font));
	text_preload_ascii(text, font, 14);
	text_preload_ascii(text, font, 16);

	vec2i previous_size = {};
	u32 density_index = 1;
	UI_Id selected_id = UI_ID_NONE;
	PlaygroundMode mode = PLAYGROUND_MODE_BASICS;
	while (os_window_is_open(window))
	{
		os_poll_windows();
		b32 reset_scroll_history = false;
		if (!os_window_is_open(window)) {
			break;
		}
		input_state_update(&input, window);
		if (playground_key_pressed(window, OS_Key_Space)) {
			density_index = (density_index + 1) % ArrayCount(playground_densities);
			ui_invalidate_layout(ui);
		}
		if (playground_key_pressed(window, OS_Key_Tab)) {
			mode = (mode + 1) % PLAYGROUND_MODE_COUNT;
			ui_invalidate_layout(ui);
		}
		if (playground_key_pressed(window, OS_Key_R) && mode == PLAYGROUND_MODE_SCROLL_HISTORY) reset_scroll_history = true;
		if (playground_key_pressed(window, OS_Key_Backspace)) {
			selected_id = UI_ID_NONE;
		}
		if (window->size.x <= 0 || window->size.y <= 0) {
			continue;
		}

		if (window->size.x != previous_size.x || window->size.y != previous_size.y)
		{
			gfx_resize_window(gfx_window, window->size);
			previous_size = window->size;
		}

		gfx_renderer_begin_frame(renderer);
		draw_begin_frame(draw);
		ui_begin_frame(ui);
		SCRATCH_SCOPE(&frame_arena)
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
			Input_KeyState mouse_left = input.keys[OS_Key_MouseLeft];

			vec2 size = v2_from_v2i(window->size);
			ui_box_measure(scene.root, (UI_BoxConstraints) { .min = size, .max = size });
			ui_box_layout(scene.root, rect_f32_from_size(size));
			UI_Box *hot = ui_box_find_deepest(scene.root, mouse);
			if (hot && (!hot->user || ui_is_active(ui, hot->id))) hot = 0;
			if (hot && (mouse_left & INPUT_KEY_PRESSED)) {
				selected_id = hot->id;
			}

			draw_begin_pass(draw, (GFX_PassDesc) {
				.output = gfx_window_texture(gfx_window),
				.clear = true,
				.clear_color = color_srgba(0x071013),
			});
			playground_draw_tree(&frame_arena, draw, text, text_gfx, font, scene.root, hot, selected_id);
			draw_end_pass(draw);
			draw_compose(draw, text_gfx, gfx_window_texture(gfx_window), rect_f32_from_size(v2_from_v2i(window->size)));
			text_gfx_sync(text_gfx);
			draw_end_frame(draw);
			gfx_present_window(gfx_window);
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
