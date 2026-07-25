#ifndef TEXT_GFX_H
#define TEXT_GFX_H

typedef struct Text_GFX Text_GFX;

Text_GFX *text_gfx_create(Arena *owner, GFX_Renderer *renderer, Text_Context *text);
void text_gfx_draw_run(Text_GFX *gfx, Draw_Context *draw, Text_DrawRun run, vec2 position, Color_SRGBA color);
void text_gfx_sync(Text_GFX *gfx);

#endif
