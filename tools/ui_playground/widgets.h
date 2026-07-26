#ifndef ORBITER_UI_PLAYGROUND_WIDGETS_H
#define ORBITER_UI_PLAYGROUND_WIDGETS_H

#include "layout.h"
#include "graphics.h"
#include "text.h"
#include "text_gfx.h"

struct UIP_Context
{
	Draw_Context *draw;
	Text_Context *text;
	Text_GFX *text_gfx;
};

typedef struct
{
	Font_Handle font;
	i32 size;
	Color_SRGBA color;
	vec2 align;
}
UIP_TextStyle;

UIP_Box *uip_text(UIP_Builder *builder, u64 key, String text, UIP_BoxDesc desc, UIP_TextStyle style);
UIP_Box *uip_text_sized(UIP_Builder *builder, u64 key, String text, String sizing_text, UIP_BoxDesc desc, UIP_TextStyle style);

#endif
