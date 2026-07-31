#ifndef FRONTEND_VIEWS_H
#define FRONTEND_VIEWS_H

#include "debugger.h"
#include "execution_activity.h"
#include "graphics.h"
#include "os_graphical.h"
#include "ui.h"
#include "ui_box.h"

typedef enum
{
	VIEW_NONE,
	VIEW_VIDEO,
	VIEW_PROGRAM,
	VIEW_CPU,
	VIEW_PROFILER,
	VIEW_PRG_ACTIVITY,
	VIEW_CHR_MAP,
	VIEW_COUNT,
}
ViewType;

typedef struct
{
	f32 scroll;
	f32 scroll_target;
	Seconds tracking_resume_time;
	u16 tracking_failed_cpu_address;
	b32 tracking_failed;
}
ViewState;

enum
{
	CHR_MAP_TEXTURE_WIDTH = 256,
	CHR_MAP_PATTERN_HEIGHT = 128,
	CHR_MAP_SPRITE_HEIGHT = 64,
	CHR_MAP_TEXTURE_HEIGHT = CHR_MAP_PATTERN_HEIGHT + CHR_MAP_SPRITE_HEIGHT,
};

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

typedef struct
{
	rect_i32 texture_region;
	rect_i32 selection_region;
	NES_MapAddr pattern_mapping;
	u16 ppu_address;
	u8 oam_index;
	u8 x;
	u8 y;
	u8 tile;
	u8 palette;
	u8 behind_background;
	u8 flip_horizontal;
	u8 flip_vertical;
}
FrontendSprite;

typedef struct
{
	Color_RGBA8 color;
	u8 palette_address;
	u8 color_index;
}
FrontendPaletteColor;

typedef struct
{
	FrontendPaletteColor colors[4];
	u8 index;
	u8 is_sprite;
}
FrontendPalette;

// Todo, eventually remove this, just make this a pointer that the view allocates ...
typedef struct DF_PanelViewData DF_PanelViewData;
struct DF_PanelViewData
{
	u64        id;
	ViewType kind;
	union
	{
		ViewState program;
		PRGActivityViewState prg_activity;
		Profiler_View_State profiler;
	};
};

typedef struct
{
	DebuggerState state;
	u8 video[NES_VIDEO_HEIGHT][NES_VIDEO_WIDTH];
	NES_CHRMap chr_map;
	Color_RGBA8 palette[64];
	FrontendSprite sprites[64];
	FrontendPalette palettes[8];
	u32 prg_rom_size;
	u64 generation;
	b32 valid;
}
FrontendPublication;

typedef struct
{
	DF_PanelViewData *view;
	Debugger      *debugger;
	UI_Context    *ui;
	Arena         *scratch;
	rect_f32       rect;
	f32            header_height;
	UI_Box        *content_box;

	// The application prepares shared publication resources once per frame.
	const FrontendPublication *publication;
	const ExecutionGraph *execution_graph;
	const ExecutionActivity *execution_activity;
	GFX_Texture *video_texture;
	GFX_Texture *chr_texture;
}
ViewFrameData;

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
