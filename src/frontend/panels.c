#include "panels.h"

struct PanelViewAllocation
{
	PanelViewAllocation *next_free;
	DF_PanelViewData view;
};

static UI_Id panel_ui_id(const Panel *panel, u64 key)
{
	UI_Id namespace = ui_id_child(UI_ID_NONE, UI_KEY("panels"));
	return ui_id_child(ui_id_child(namespace, panel->id), key);
}

static
Panel *panel_new(Panels *panels, Panel *parent, PanelType kind)
{
	Panel *panel = panels->free_panels;
	if (panel) {
		panels->free_panels = panel->next_free;
	} else {
		panel = arena_push(panels->arena, sizeof(*panel));
	}
	memory_zero(panel, sizeof(*panel));
	panel->parent = parent;
	panel->id = ++panels->next_panel_id;
	panel->kind = kind;
	return panel;
}

static DF_PanelViewData *panel_view_new(Panels *panels, const ViewDesc *desc)
{
	Assert(desc);
	Assert(desc->build_ui);
	PanelViewAllocation *allocation = panels->free_views;
	if (allocation) {
		panels->free_views = allocation->next_free;
	} else {
		allocation = arena_push(panels->arena, sizeof(*allocation));
	}
	memory_zero(allocation, sizeof(*allocation));
	allocation->view.id = ++panels->next_view_id;
	allocation->view.desc = desc;
	return &allocation->view;
}

static void panel_view_free(Panels *panels, DF_PanelViewData *view)
{
	if (!view) return;
	PanelViewAllocation *allocation = (PanelViewAllocation *)((u8 *)view - offsetof(PanelViewAllocation, view));
	memory_zero(&allocation->view, sizeof(allocation->view));
	allocation->next_free = panels->free_views;
	panels->free_views = allocation;
}

static void panel_free(Panels *panels, Panel *panel)
{
	Assert(panel);
	Assert(!panel->view);
	panel->next_free = panels->free_panels;
	panels->free_panels = panel;
}

static void panel_free_tree(Panels *panels, Panel *panel)
{
	if (!panel) return;
	if (panel->kind == PANEL_SPLIT)
	{
		panel_free_tree(panels, panel->left);
		panel_free_tree(panels, panel->right);
	}
	panel_view_free(panels, panel->view);
	panel->view = 0;
	panel_free(panels, panel);
}

static Panel *panel_first_leaf(Panel *panel)
{
	while (panel && panel->kind == PANEL_SPLIT)
	{
		panel = panel->left;
	}
	return panel;
}

void panel_open_view(Panels *panels, Panel *panel, const ViewDesc *desc)
{
	Assert(panel);
	Assert(panel->kind != PANEL_SPLIT);
	Assert(desc);
	Assert(desc->build_ui);

	panel_view_free(panels, panel->view);
	panel->left = 0;
	panel->right = 0;
	panel->view = panel_view_new(panels, desc);
	panel->kind = PANEL_VIEW;
}

void panel_split(Panels *panels, Panel *panel, AXIS axis, f32 ratio)
{
	Assert(panel);
	Assert(panel->kind != PANEL_SPLIT);

	PanelType previous_kind = panel->kind;
	DF_PanelViewData *previous_view = panel->view;

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
	panel->view = 0;
	panels->focused = second;
}

void panel_close(Panels *panels, Panel *panel)
{
	Assert(panel);
	Panel *parent = panel->parent;
	if (!parent)
	{
		panel_view_free(panels, panel->view);
		panel->kind = PANEL_EMPTY;
		panel->left = 0;
		panel->right = 0;
		panel->view = 0;
		return;
	}

	Assert(panel->kind != PANEL_SPLIT);
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
	sibling->view = 0;
	if (sibling->kind == PANEL_SPLIT)
	{
		parent->left->parent = parent;
		parent->right->parent = parent;
	}

	panel_view_free(panels, panel->view);
	panel->view = 0;
	panel_free(panels, panel);
	panel_free(panels, sibling);
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
		case PANEL_EMPTY: str_push_f(arena, "empty\n"); break;
		case PANEL_VIEW: str_push_f(arena, "view %s\n", panel->view->desc->name); break;
		case PANEL_SPLIT:
		{
			str_push_f(arena, "split %c %.6f\n", panel->axis == AXIS_X ? 'x' : 'y', panel->ratio);
			panel_save_layout(panel->left, arena);
			panel_save_layout(panel->right, arena);
		}
		break;
	}
}

Str panels_save_layout(Panels *panels, Arena *arena)
{
	char *begin = str_top(arena);
	panel_save_layout(panels->root, arena);
	return str_from_data(begin, str_end(arena, begin));
}

static const ViewDesc *layout_parse_view_desc(Str name)
{
	for (u32 i = 0; i < view_desc_count; ++i)
	{
		const ViewDesc *desc = &view_descs[i];
		if (str_match(name, str_from_cstr(desc->name))) {
			return desc;
		}
	}
	return 0;
}

static Panel *panel_restore_layout(Panels *panels, Panel *parent, Str *remaining)
{
	if (!remaining->size) return 0;
	Str line = str_consume_line(remaining);
	if (str_match(line, LIT("empty"))) {
		return panel_new(panels, parent, PANEL_EMPTY);
	}
	if (str_consume_prefix(&line, LIT("view ")))
	{
		const ViewDesc *desc = layout_parse_view_desc(line);
		if (!desc) {
			return 0;
		}
		Panel *panel = panel_new(panels, parent, PANEL_EMPTY);
		panel_open_view(panels, panel, desc);
		return panel;
	}
	if (str_consume_prefix(&line, LIT("split ")))
	{
		char axis_name = 0;
		f32 ratio = 0.f;
		char buffer[64] = {};
		if (line.size >= sizeof(buffer)) {
			return 0;
		}
		memory_copy(buffer, line.text, line.size);
		if (sscanf_s(buffer, "%c %f", &axis_name, 1, &ratio) != 2 || (axis_name != 'x' && axis_name != 'y') || ratio < 0.05f || ratio > 0.95f) {
			return 0;
		}
		Panel *panel = panel_new(panels, parent, PANEL_SPLIT);
		panel->axis = axis_name == 'x' ? AXIS_X : AXIS_Y;
		panel->ratio = ratio;
		panel->left = panel_restore_layout(panels, panel, remaining);
		panel->right = panel_restore_layout(panels, panel, remaining);
		if (!panel->left || !panel->right)
		{
			panel_free_tree(panels, panel->left);
			panel_free_tree(panels, panel->right);
			panel->left = 0;
			panel->right = 0;
			panel_free(panels, panel);
			return 0;
		}
		return panel;
	}
	return 0;
}

b32 panels_restore_layout(Panels *panels, Str text)
{
	Str remaining = text;
	Panel *root = panel_restore_layout(panels, 0, &remaining);
	while (remaining.size && (*remaining.text == '\r' || *remaining.text == '\n' || *remaining.text == ' ' || *remaining.text == '\t')) {
		remaining.text ++;
		remaining.size --;
	}
	if (!root || remaining.size)
	{
		panel_free_tree(panels, root);
		return false;
	}
	panel_free_tree(panels, panels->root);
	panels->root = root;
	panels->focused = panel_first_leaf(root);
	return true;
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
	UI_Id resize_id = panel_ui_id(panel, 1);
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

static void panel_frame_props(UI_Context *ui, rect_f32 rect, vec2 root_position)
{
	rect.x -= root_position.x;
	rect.y -= root_position.y;
	ui_clean(ui);
	ui_rect(ui, rect);
}

static void panel_leaf_props(UI_Context *ui, rect_f32 rect, vec2 root_position)
{
	panel_frame_props(ui, rect, root_position);
	ui_padd(ui, AXIS_X, 3.f, 3.f);
	ui_padd(ui, AXIS_Y, 3.f, 3.f);
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_CLIP);
}

static void panel_build_ui(Panels *panels, Panel *panel, ViewFrameData *source, rect_f32 rect, vec2 root_position)
{
	UI_Context *ui = source->ui;
	switch (panel->kind)
	{
		case PANEL_EMPTY:
		{
			panel_leaf_props(ui, rect, root_position);
			ui_background(ui, ui->theme.panel_background);
			ui_inset_shadow(ui, 0.035f);
			ui_box_make(ui, ui_key_child(UI_KEY("empty panel"), panel->id), LIT("empty panel"));
		}
		break;
		case PANEL_VIEW:
		{
			panel_leaf_props(ui, rect, root_position);
			ui_background(ui, ui->theme.panel_background);
			ui_inset_shadow(ui, 0.035f);
			ui_box_begin(ui, ui_key_child(UI_KEY("view panel"), panel->view->id), LIT("view panel"));

			rect_f32 content = rect_f32_inset(rect, 3.f);
			ViewFrameData frame = *source;
			frame.view = panel->view;
			frame.rect = content;
			view_build_ui(&frame);

			ui_box_end(ui);
		}
		break;
		case PANEL_SPLIT:
		{
			rect_f32 first;
			rect_f32 second;
			rect_f32 handle;
			panel_split_rects(panel, rect, &first, &second, &handle);
			panel_build_ui(panels, panel->left, source, first, root_position);
			panel_build_ui(panels, panel->right, source, second, root_position);

			rect_f32 line = rect_f32_from_slice(second, panel->axis, 2.f);
			line = rect_f32_translate_axis(line, panel->axis, -1.f);
			panel_frame_props(ui, line, root_position);
			UI_Box *splitter = ui_box_make(ui, ui_key_child(UI_KEY("panel splitter"), panel->id), LIT("panel splitter"));
			splitter->paint = ui_default_paint();
			splitter->paint.flags = UI_BOX_DRAW_BACKGROUND;
			splitter->paint.background = ui->theme.slider_track;
			splitter->paint.edge_softness = 0.f;
		}
		break;
	}
}

UI_Box *panels_build_ui(Panels *panels, OS_Window *window, ViewFrameData *frame, rect_f32 rect)
{
	Assert(panels);
	Assert(window);
	Assert(frame);
	panel_update_interaction(panels, window, frame->ui, panels->root, rect);

	ui_clean(frame->ui);
	ui_size(frame->ui, AXIS_X, ui_grow(1.f));
	ui_size(frame->ui, AXIS_Y, ui_grow(1.f));
	ui_overflow(frame->ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_overflow(frame->ui, AXIS_Y, UI_BOX_OVERFLOW_CLIP);
	ui_layout(frame->ui, &UI_FlatLayoutHooks);
	UI_Box *root = ui_box_begin(frame->ui, UI_KEY("panels"), LIT("panels"));
	panel_build_ui(panels, panels->root, frame, rect, rect.pos);
	ui_box_end(frame->ui);
	return root;
}
