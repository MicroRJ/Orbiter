#ifndef FRONTEND_VIEWS_H
#define FRONTEND_VIEWS_H

#include "debugger.h"
#include "execution_activity.h"
#include "graphics.h"
#include "nes_target.h"
#include "os_graphical.h"
#include "ui.h"
#include "ui_box.h"

typedef struct ViewFrameData ViewFrameData;
typedef void ViewBuildUIProc(ViewFrameData *frame);

typedef struct
{
	const char *name;
	OS_Key hotkey;
	ViewBuildUIProc *build_ui;
}
ViewDesc;

extern const ViewDesc view_descs[];
extern const u32 view_desc_count;

typedef struct
{
	f32 scroll;
	f32 scroll_target;
	Seconds tracking_resume_time;
	u16 tracking_failed_cpu_address;
	b32 tracking_failed;
}
ViewState;

typedef struct
{
	u32 cell_size;
}
PRGActivityViewState;

typedef struct
{
	f64 offset;
	f64 target_offset;
	f32 bar_width;
	u64 first_visible_frame;
	u64 visible_frame_count;
	b32 following;
	b32 initialized;
}
Profiler_View_State;

// Todo, eventually remove this, just make this a pointer that the view allocates ...
typedef struct DF_PanelViewData DF_PanelViewData;
struct DF_PanelViewData
{
	u64 id;
	const ViewDesc *desc;
	union
	{
		ViewState program;
		PRGActivityViewState prg_activity;
		Profiler_View_State profiler;
	};
};

struct ViewFrameData
{
	DF_PanelViewData *view;
	Debugger      *debugger;
	UI_Context    *ui;
	Arena         *scratch;
	rect_f32       rect;
	f32            header_height;
	UI_Box        *frame_box;
	UI_Box        *content_box;

	// The application prepares shared publication resources once per frame.
	const NES_TargetPublication *publication;
	const ExecutionGraph *execution_graph;
	const ExecutionActivity *execution_activity;
	GFX_Texture *video_texture;
	GFX_Texture *chr_texture;
};

ViewFrameData view_begin_frame(ViewFrameData *frame, String title);
void view_end_frame(ViewFrameData *frame);
void view_build_ui(ViewFrameData *frame);
void video_view_build_ui(ViewFrameData *frame);
void program_view_build_ui(ViewFrameData *frame);
void cpu_view_build_ui(ViewFrameData *frame);
void profiler_view_build_ui(ViewFrameData *frame);
void prg_activity_view_build_ui(ViewFrameData *frame);
void chr_map_view_build_ui(ViewFrameData *frame);

#endif
