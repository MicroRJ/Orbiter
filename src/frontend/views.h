#ifndef FRONTEND_VIEWS_H
#define FRONTEND_VIEWS_H

#include "debugger.h"
#include "activity_tracker.h"
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
	VIEW_CPU_MAPPING,
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

typedef struct
{
	f32 scroll;
}
CPUViewState;

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
	ActivityTracker tracker;
}
PRGActivityViewState;

typedef struct
{
	i64 right_frame_index;
	f32 frame_stride;
	b32 following;
	b32 initialized;
}
ProfilerViewState;

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

typedef struct
{
	ViewType kind;
	union
	{
		ViewState program;
		CPUViewState cpu;
		PRGActivityViewState prg_activity;
		ProfilerViewState profiler;
	};
}
PanelViewData;

typedef struct
{
	DebuggerState state;
	NES_ExecutionHistory execution_history;
	NES_ExecutionMapping execution_entries[NES_EXECUTION_HISTORY_CAPACITY];
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
	PanelViewData *view;
	Debugger      *debugger;
	UI_Context    *ui;
	Arena         *scratch;
	rect_f32       rect;
	f32            header_height;
	void         (*draw_box_tree)(UI_Box *box);

	// The application prepares shared publication resources once per frame.
	const FrontendPublication *publication;
	GFX_Texture *video_texture;
	GFX_Texture *chr_texture;
}
ViewFrameData;

ViewFrameData view_begin_frame(ViewFrameData *frame, String title);
void view_end_frame(ViewFrameData *frame);
void view_frame(ViewFrameData *frame);
void video_view_frame(ViewFrameData *frame);
void program_view_frame(ViewFrameData *frame);
void cpu_view_frame(ViewFrameData *frame);
void profiler_view_frame(ViewFrameData *frame);
void cpu_mapping_view_frame(ViewFrameData *frame);
void prg_activity_view_frame(ViewFrameData *frame);

void chr_map_view_frame(ViewFrameData *frame);

#endif
