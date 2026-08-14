#ifndef NES_FRONTEND_PANELS_H
#define NES_FRONTEND_PANELS_H

#include "base.h"
#include "os_graphical.h"
#include "views.h"


typedef struct Panels Panels;
typedef struct Panel Panel;
typedef struct PanelViewAllocation PanelViewAllocation;
typedef struct elf_State elf_State;

typedef enum
{
	PANEL_EMPTY,
	PANEL_VIEW,
	PANEL_SPLIT,
}
PanelType;

struct Panel
{
	Panel *parent;
	Panel *left;
	Panel *right;
	u64 id;
	PanelType kind;
	AXIS axis;
	f32 ratio;
	DF_PanelViewData *view;
	Panel *next_free;
};

struct Panels
{
	Arena *arena;
	u64 next_panel_id;
	u64 next_view_id;
	Panel *root;
	Panel *focused;
	Panel *free_panels;
	PanelViewAllocation *free_views;
	f32 split_drag_offset;
};

Panels *panels_create(Arena *owner);
UI_Box *panels_build_ui(Panels *panels, OS_Window *window, ViewFrameData *frame, rect_f32 rect);
void panels_layout_push(elf_State *state, const Panels *panels);
b32 panels_layout_read(elf_State *state, i32 index, Panels *panels);
void panel_close(Panels *panels, Panel *panel);
void panel_split(Panels *panels, Panel *panel, AXIS axis, f32 ratio);
void panel_open_view(Panels *panels, Panel *panel, const ViewDesc *desc);


#endif
