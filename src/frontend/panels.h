#ifndef NES_FRONTEND_PANELS_H
#define NES_FRONTEND_PANELS_H

#include "base.h"
#include "os_graphical.h"
#include "views.h"


typedef struct Panels Panels;
typedef struct Panel Panel;

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
	PanelViewData view;
};

struct Panels
{
	Arena *arena;
	u64 next_panel_id;
	Panel *root;
	Panel *focused;
	f32 split_drag_offset;
};

Panels *panels_create(Arena *owner);
void panels_update_and_draw(Panels *panels, OS_Window *window, ViewFrameData *frame, rect_f32 rect);
String panels_save_layout(Panels *panels, Arena *arena);
b32 panels_restore_layout(Panels *panels, String text);


#endif
