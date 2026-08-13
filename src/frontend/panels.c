#include "panels.h"
#include "elf.h"

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

static b32 panel_elf_string_match(elf_StrSlice value, Str expected)
{
	return value.size == expected.size && memory_match(value.data, expected.data, expected.size);
}

static const ViewDesc *panel_elf_view_desc(elf_StrSlice name)
{
	for (u32 i = 0; i < view_desc_count; ++i)
	{
		const ViewDesc *desc = &view_descs[i];
		if (panel_elf_string_match(name, str_from_cstr(desc->name))) return desc;
	}
	return 0;
}

static void panel_elf_set_string(elf_State *state, i32 table, const char *field, const char *value)
{
	elf_push_cstr(state, value);
	Assert(elf_set_field(state, table, field));
}

static b32 panel_elf_read_string(elf_State *state, i32 table, const char *field, elf_StrSlice *result)
{
	Assert(result);
	if (!elf_get_field(state, table, field)) return false;
	b32 valid = elf_to_str(state, -1, result);
	Assert(elf_pop(state, 1));
	return valid;
}

static b32 panel_elf_read_integer(elf_State *state, i32 table, const char *field, elf_Integer *result)
{
	Assert(result);
	if (!elf_get_field(state, table, field)) return false;
	b32 valid = elf_to_int(state, -1, result);
	Assert(elf_pop(state, 1));
	return valid;
}

static b32 panel_elf_read_number(elf_State *state, i32 table, const char *field, elf_Number *result)
{
	Assert(result);
	if (!elf_get_field(state, table, field)) return false;
	b32 valid = elf_to_num(state, -1, result);
	Assert(elf_pop(state, 1));
	return valid;
}

static b32 panel_elf_push_table_field(elf_State *state, i32 table, const char *field)
{
	if (!elf_get_field(state, table, field)) return false;
	if (elf_type(state, -1) == ELF_VALUE_TYPE_TABLE) return true;
	Assert(elf_pop(state, 1));
	return false;
}

static void panel_layout_push(elf_State *state, const Panel *panel)
{
	Assert(state);
	Assert(panel);
	i32 top = elf_get_top(state);
	elf_new_table(state);
	i32 table = elf_abs_index(state, -1);

	switch (panel->kind)
	{
		case PANEL_EMPTY:
		{
			panel_elf_set_string(state, table, "kind", "empty");
		} break;

		case PANEL_VIEW:
		{
			Assert(panel->view);
			Assert(panel->view->desc);
			Assert(panel->view->desc->name);
			panel_elf_set_string(state, table, "kind", "view");
			panel_elf_set_string(state, table, "view", panel->view->desc->name);
		} break;

		case PANEL_SPLIT:
		{
			Assert(panel->left);
			Assert(panel->right);
			Assert(panel->axis == AXIS_X || panel->axis == AXIS_Y);
			panel_elf_set_string(state, table, "kind", "split");
			panel_elf_set_string(state, table, "axis", panel->axis == AXIS_X ? "x" : "y");
			elf_push_num(state, panel->ratio);
			Assert(elf_set_field(state, table, "ratio"));
			panel_layout_push(state, panel->left);
			Assert(elf_set_field(state, table, "left"));
			panel_layout_push(state, panel->right);
			Assert(elf_set_field(state, table, "right"));
		} break;

		default: Assert(!"invalid panel kind"); break;
	}
	Assert(elf_get_top(state) == top + 1);
}

void panels_layout_push(elf_State *state, const Panels *panels)
{
	Assert(state);
	Assert(panels);
	Assert(panels->root);
	i32 top = elf_get_top(state);
	elf_new_table(state);
	i32 table = elf_abs_index(state, -1);
	elf_push_int(state, 1);
	Assert(elf_set_field(state, table, "version"));
	panel_layout_push(state, panels->root);
	Assert(elf_set_field(state, table, "root"));
	Assert(elf_get_top(state) == top + 1);
}

static Panel *panel_layout_read(elf_State *state, i32 index, Panels *panels, Panel *parent, u32 depth)
{
	if (depth >= 64 || elf_type(state, index) != ELF_VALUE_TYPE_TABLE) return 0;
	i32 table = elf_abs_index(state, index);
	elf_StrSlice kind;
	if (!panel_elf_read_string(state, table, "kind", &kind)) return 0;

	if (panel_elf_string_match(kind, LIT("empty"))) return panel_new(panels, parent, PANEL_EMPTY);

	if (panel_elf_string_match(kind, LIT("view")))
	{
		elf_StrSlice view;
		if (!panel_elf_read_string(state, table, "view", &view)) return 0;
		const ViewDesc *desc = panel_elf_view_desc(view);
		if (!desc) return 0;
		Panel *panel = panel_new(panels, parent, PANEL_EMPTY);
		panel_open_view(panels, panel, desc);
		return panel;
	}

	if (panel_elf_string_match(kind, LIT("split")))
	{
		elf_StrSlice axis;
		elf_Number ratio;
		if (!panel_elf_read_string(state, table, "axis", &axis) || !panel_elf_read_number(state, table, "ratio", &ratio) || !(ratio >= 0.05 && ratio <= 0.95)) return 0;

		AXIS split_axis;
		if (panel_elf_string_match(axis, LIT("x"))) split_axis = AXIS_X;
		else if (panel_elf_string_match(axis, LIT("y"))) split_axis = AXIS_Y;
		else return 0;

		Panel *panel = panel_new(panels, parent, PANEL_SPLIT);
		panel->axis = split_axis;
		panel->ratio = (f32)ratio;
		if (panel_elf_push_table_field(state, table, "left"))
		{
			panel->left = panel_layout_read(state, -1, panels, panel, depth + 1);
			Assert(elf_pop(state, 1));
		}
		if (panel->left && panel_elf_push_table_field(state, table, "right"))
		{
			panel->right = panel_layout_read(state, -1, panels, panel, depth + 1);
			Assert(elf_pop(state, 1));
		}
		if (!panel->left || !panel->right)
		{
			panel_free_tree(panels, panel);
			return 0;
		}
		return panel;
	}

	return 0;
}

static b32 panels_layout_read_impl(elf_State *state, i32 index, Panels *panels)
{
	if (elf_type(state, index) != ELF_VALUE_TYPE_TABLE) return false;
	i32 table = elf_abs_index(state, index);
	elf_Integer version;
	if (!panel_elf_read_integer(state, table, "version", &version) || version != 1 || !panel_elf_push_table_field(state, table, "root")) return false;
	Panel *root = panel_layout_read(state, -1, panels, 0, 0);
	Assert(elf_pop(state, 1));
	if (!root) return false;

	panel_free_tree(panels, panels->root);
	panels->root = root;
	panels->focused = panel_first_leaf(root);
	panels->split_drag_offset = 0.f;
	return true;
}

b32 panels_layout_read(elf_State *state, i32 index, Panels *panels)
{
	Assert(state);
	Assert(panels);
	i32 top = elf_get_top(state);
	b32 result = panels_layout_read_impl(state, index, panels);
	Assert(elf_get_top(state) == top);
	return result;
}

static void panel_split_rects(Panel *panel, rect_f32 rect,
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
		if (ui_pointer_over(ui, rect, UI_Z_CONTENT))
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
