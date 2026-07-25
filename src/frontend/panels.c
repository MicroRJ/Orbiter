#include "panels.h"

typedef struct
{
	const char *at;
	const char *end;
}
LayoutParser;

static const char *view_type_names[VIEW_COUNT] = {
	[VIEW_NONE] = "none",
	[VIEW_VIDEO] = "video",
	[VIEW_PROGRAM] = "program",
	[VIEW_CPU] = "cpu",
	[VIEW_PROFILER] = "profiler",
	[VIEW_CPU_MAPPING] = "cpu_mapping",
	[VIEW_PRG_ACTIVITY] = "prg_activity",
	[VIEW_CHR_MAP] = "chr_map",
};

static void layout_push_formatted(Arena *arena, const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	push_formatted_v(arena, format, arguments);
	va_end(arguments);
	arena_pop(arena, 1);
}

static
Panel *panel_new(Panels *panels, Panel *parent, PanelType kind)
{
	Panel *panel = arena_push_zero(panels->arena, sizeof(*panel));
	panel->parent = parent;
	panel->id = ++panels->next_panel_id;
	panel->kind = kind;
	return panel;
}

static
Panel *panel_first_leaf(Panel *panel)
{
	while (panel && panel->kind == PANEL_SPLIT)
	{
		panel = panel->left;
	}
	return panel;
}

static
void panel_open_view(Panels *panels, Panel *panel, ViewType kind)
{
	Assert(panel);
	Assert(panel->kind != PANEL_SPLIT);
	Assert(kind > VIEW_NONE && kind < VIEW_COUNT);

	(void)panels;
	panel->left = 0;
	panel->right = 0;
	memory_zero(&panel->view, sizeof(panel->view));
	panel->view.kind = kind;
	panel->kind = PANEL_VIEW;
}

static
void panel_split(Panels *panels, Panel *panel, AXIS axis, f32 ratio)
{
	Assert(panel);
	Assert(panel->kind != PANEL_SPLIT);

	PanelType previous_kind = panel->kind;
	PanelViewData previous_view = previous_kind == PANEL_VIEW ? panel->view : (PanelViewData) {};

	// The old implementation copied the complete panel node here. That duplicated
	// its identity and intrusive-list links and left the child pointing at the old
	// parent. Rebuild both children explicitly so the tree owns unambiguous links.
	Panel *first = panel_new(panels, panel, previous_kind);
	if (previous_kind == PANEL_VIEW) {
		first->view = previous_view;
	}
	Panel *second = panel_new(panels, panel, PANEL_EMPTY);

	panel->kind = PANEL_SPLIT;
	panel->left = first;
	panel->right = second;
	panel->axis = axis;
	panel->ratio = ratio;
	memory_zero(&panel->view, sizeof(panel->view));
	panels->focused = second;
}

static
void panel_close(Panels *panels, Panel *panel)
{
	Assert(panel);
	Panel *parent = panel->parent;
	if (!parent)
	{
		panel->kind = PANEL_EMPTY;
		panel->left = 0;
		panel->right = 0;
		memory_zero(&panel->view, sizeof(panel->view));
		return;
	}

	Assert(parent->kind == PANEL_SPLIT);
	Panel *sibling = panel == parent->left ? parent->right : parent->left;
	Panel *grandparent = parent->parent;
	u64 parent_id = parent->id;

	// Collapse the sibling into the existing parent identity. Copying the old
	// panel payload made parent links self-referential when a child was closed.
	parent->parent = grandparent;
	parent->id = parent_id;
	parent->kind = sibling->kind;
	parent->left = sibling->left;
	parent->right = sibling->right;
	parent->axis = sibling->axis;
	parent->ratio = sibling->ratio;
	parent->view = sibling->view;
	if (sibling->kind == PANEL_SPLIT)
	{
		parent->left->parent = parent;
		parent->right->parent = parent;
	}

	panels->focused = panel_first_leaf(parent);
}

Panels *panels_create(Arena *owner)
{
	Assert(owner);
	Panels *panels = arena_push_zero(owner, sizeof(*panels));
	panels->arena = owner;
	panels->root = panel_new(panels, 0, PANEL_EMPTY);
	panels->focused = panels->root;
	return panels;
}

static void panel_save_layout(Panel *panel, Arena *arena)
{
	switch (panel->kind)
	{
		case PANEL_EMPTY: layout_push_formatted(arena, "empty\n"); break;
		case PANEL_VIEW: layout_push_formatted(arena, "view %s\n", view_type_names[panel->view.kind]); break;
		case PANEL_SPLIT:
		{
			layout_push_formatted(arena, "split %c %.6f\n", panel->axis == AXIS_X ? 'x' : 'y', panel->ratio);
			panel_save_layout(panel->left, arena);
			panel_save_layout(panel->right, arena);
		}
		break;
	}
}

String panels_save_layout(Panels *panels, Arena *arena)
{
	char *begin = arena_top(arena);
	panel_save_layout(panels->root, arena);
	return string_from_data(begin, (u32)arena_distance(arena, begin));
}

static String layout_read_line(LayoutParser *parser)
{
	const char *begin = parser->at;
	while (parser->at < parser->end && *parser->at != '\n') {
		++parser->at;
	}
	const char *end = parser->at;
	if (end > begin && end[-1] == '\r') {
		--end;
	}
	if (parser->at < parser->end) {
		++parser->at;
	}
	return string_from_data(begin, (u32)(end - begin));
}

static ViewType layout_parse_view_type(String name)
{
	for (ViewType type = VIEW_VIDEO; type < VIEW_COUNT; ++type)
	{
		u32 size = (u32)strlen(view_type_names[type]);
		if (name.size == size && memory_match(name.text, view_type_names[type], size)) {
			return type;
		}
	}
	return VIEW_NONE;
}

static Panel *panel_restore_layout(Panels *panels, Panel *parent, LayoutParser *parser)
{
	if (parser->at >= parser->end) {
		return 0;
	}
	String line = layout_read_line(parser);
	if (string_match(line, LIT("empty"))) {
		return panel_new(panels, parent, PANEL_EMPTY);
	}
	if (line.size > 5 && memory_match(line.text, "view ", 5))
	{
		ViewType type = layout_parse_view_type(string_slice(line, 5, line.size - 5));
		if (type == VIEW_NONE) {
			return 0;
		}
		Panel *panel = panel_new(panels, parent, PANEL_EMPTY);
		panel_open_view(panels, panel, type);
		return panel;
	}
	if (line.size > 8 && memory_match(line.text, "split ", 6))
	{
		char axis_name = 0;
		f32 ratio = 0.f;
		char buffer[64] = {};
		if (line.size >= sizeof(buffer)) {
			return 0;
		}
		memory_copy(buffer, line.text, line.size);
		if (sscanf_s(buffer, "split %c %f", &axis_name, 1, &ratio) != 2 || (axis_name != 'x' && axis_name != 'y') || ratio < 0.05f || ratio > 0.95f) {
			return 0;
		}
		Panel *panel = panel_new(panels, parent, PANEL_SPLIT);
		panel->axis = axis_name == 'x' ? AXIS_X : AXIS_Y;
		panel->ratio = ratio;
		panel->left = panel_restore_layout(panels, panel, parser);
		panel->right = panel_restore_layout(panels, panel, parser);
		if (!panel->left || !panel->right) {
			return 0;
		}
		return panel;
	}
	return 0;
}

b32 panels_restore_layout(Panels *panels, String text)
{
	LayoutParser parser = { text.text, text.text + text.size };
	Panel *root = panel_restore_layout(panels, 0, &parser);
	while (parser.at < parser.end && (*parser.at == '\r' || *parser.at == '\n' || *parser.at == ' ' || *parser.at == '\t')) {
		++parser.at;
	}
	if (!root || parser.at != parser.end) {
		return false;
	}
	panels->root = root;
	panels->focused = panel_first_leaf(root);
	return true;
}

static void panels_handle_commands(Panels *panels, OS_Window *window)
{
	OS_KeyState *keys = window->keys;
	Panel *panel = panels->focused;
	if (!panel || panel->kind == PANEL_SPLIT) return;

	if (keys[OS_Key_1] & OS_KEY_RELEASED)
	{
		panel_open_view(panels, panel, VIEW_VIDEO);
	}
	if (keys[OS_Key_2] & OS_KEY_RELEASED)
	{
		panel_open_view(panels, panel, VIEW_PROGRAM);
	}
	if (keys[OS_Key_3] & OS_KEY_RELEASED)
	{
		panel_open_view(panels, panel, VIEW_CPU);
	}
	if (keys[OS_Key_4] & OS_KEY_RELEASED)
	{
		panel_open_view(panels, panel, VIEW_PROFILER);
	}
	if (keys[OS_Key_5] & OS_KEY_RELEASED)
	{
		panel_open_view(panels, panel, VIEW_CPU_MAPPING);
	}
	if (keys[OS_Key_6] & OS_KEY_RELEASED)
	{
		panel_open_view(panels, panel, VIEW_PRG_ACTIVITY);
	}
	if (keys[OS_Key_7] & OS_KEY_RELEASED)
	{
		panel_open_view(panels, panel, VIEW_CHR_MAP);
	}

	if (keys[OS_Key_LeftControl] & OS_KEY_DOWN)
	{
		if (keys[OS_Key_V] & OS_KEY_RELEASED)
		{
			panel_split(panels, panel, AXIS_Y, 0.5f);
		}
		else if (keys[OS_Key_H] & OS_KEY_RELEASED)
		{
			panel_split(panels, panel, AXIS_X, 0.5f);
		}
		else if (keys[OS_Key_Q] & OS_KEY_RELEASED)
		{
			panel_close(panels, panel);
		}
	}
}

static
void panel_split_rects(Panel *panel, rect_f32 rect,
	rect_f32 *first, rect_f32 *second, rect_f32 *handle)
{
	Assert(panel->kind == PANEL_SPLIT);
	*second = rect;
	*first = rect_f32_split(second, panel->axis, panel->ratio);
	*first = rect_f32_round_out(*first);
	*second = rect_f32_round_out(*second);
	*handle = rect_f32_from_slice(*second, panel->axis, 16.f);
	*handle = rect_f32_translate_axis(*handle,
		panel->axis, -8.f);
}

static void panel_update_interaction(Panels *panels, OS_Window *window, UI_Context *ui, Panel *panel, rect_f32 rect)
{
	if (panel->kind != PANEL_SPLIT)
	{
		if (rect_f32_contains(rect, ui->mouse))
		{
			panels->focused = panel;
		}
		return;
	}

	rect_f32 first;
	rect_f32 second;
	rect_f32 handle;
	panel_split_rects(panel, rect, &first, &second, &handle);
	UI_Id resize_id = ui_id_child(ui_id_from_ptr(panel), 1);
	UI_Response response = ui_interact(ui, resize_id, handle);
	if (response.hovered || response.held)
	{
		os_window_set_cursor(window, panel->axis == AXIS_X
			? OS_CURSOR_RESIZE_HORIZONTAL : OS_CURSOR_RESIZE_VERTICAL);
	}
	if (response.pressed)
	{
		f32 local_mouse = ui->mouse.xy[panel->axis] - rect.xy[panel->axis];
		panels->split_drag_offset = first.wh[panel->axis] - local_mouse;
	}
	if (response.held && rect.wh[panel->axis] > 0.f)
	{
		f32 local_mouse = ui->mouse.xy[panel->axis] - rect.xy[panel->axis];
		f32 new_size = local_mouse + panels->split_drag_offset;
		panel->ratio = CLAMP(new_size / rect.wh[panel->axis], 0.05f, 0.95f);
		panel_split_rects(panel, rect, &first, &second, &handle);
	}

	panel_update_interaction(panels, window, ui, panel->left, first);
	panel_update_interaction(panels, window, ui, panel->right, second);
}

static void panel_draw(Panels *panels, Panel *panel, ViewFrameData *source, rect_f32 rect)
{
	UI_Context *ui = source->ui;
	switch (panel->kind)
	{
		case PANEL_EMPTY:
		{
			ui_draw_panel(ui, rect, panel == panels->focused);
		}
		break;
		case PANEL_VIEW:
		{
			ui_draw_panel(ui, rect, panel == panels->focused);
			rect_f32 content = rect_f32_inset(rect, 3.f);
			ui_push_clip(ui, content);
			ViewFrameData frame = *source;
			frame.view = &panel->view;
			frame.rect = content;
			view_frame(&frame);
			ui_pop_clip(ui);
		}
		break;
		case PANEL_SPLIT:
		{
			rect_f32 first;
			rect_f32 second;
			rect_f32 handle;
			panel_split_rects(panel, rect, &first, &second, &handle);
			panel_draw(panels, panel->left, source, first);
			panel_draw(panels, panel->right, source, second);

			rect_f32 line = rect_f32_from_slice(second, panel->axis, 2.f);
			line = rect_f32_translate_axis(line, panel->axis, -1.f);
			ui_draw_splitter(ui, line, ui_id_child(ui_id_from_ptr(panel), 1));
		}
		break;
	}
}

void panels_update_and_draw(Panels *panels, OS_Window *window, ViewFrameData *frame, rect_f32 rect)
{
	Assert(panels);
	Assert(window);
	Assert(frame);
	panels_handle_commands(panels, window);
	panel_update_interaction(panels, window, frame->ui, panels->root, rect);
	panel_draw(panels, panels->root, frame, rect);
}
