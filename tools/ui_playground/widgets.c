#include "widgets.h"

typedef struct
{
	Arena *arena;
	String string;
	String sizing_string;
	UIP_TextStyle style;
	Text_Layout layout;
	Text_DrawRun run;
}
UIP_TextData;

static vec2 uip__measure_text(UIP_Box *box, UIP_Constraints constraints)
{
	(void)constraints;
	UIP_TextData *text = box->content;
	text->layout = text_layout(text->arena, box->ui->text, text->style.font, text->style.size, text->string);
	text->run = text_make_draw_run(text->arena, &text->layout);
	if (!text->sizing_string.size) return text->layout.metrics.dim;
	return text_layout(text->arena, box->ui->text, text->style.font, text->style.size, text->sizing_string).metrics.dim;
}

static void uip__paint_text(UIP_Box *box)
{
	UIP_TextData *text = box->content;
	vec2 remaining = v2(Max(0.f, box->viewport.w - text->run.dim.x), Max(0.f, box->viewport.h - text->run.dim.y));
	vec2 position = v2_add(box->viewport.pos, v2_mul(remaining, text->style.align));
	draw_push_clip(box->ui->draw, box->clip_rect);
	text_gfx_draw_run(box->ui->text_gfx, box->ui->draw, text->run, position, text->style.color);
	draw_pop_clip(box->ui->draw);
}

static const UIP_BoxOps uip__text_ops = {
	.measure = uip__measure_text,
	.paint = uip__paint_text,
};

UIP_Box *uip_text(UIP_Builder *builder, String string, UIP_BoxDesc desc, UIP_TextStyle style)
{
	return uip_text_sized(builder, string, (String) {}, desc, style);
}

UIP_Box *uip_text_sized(UIP_Builder *builder, String string, String sizing_string, UIP_BoxDesc desc, UIP_TextStyle style)
{
	Assert(builder);
	Assert(builder->ui);
	Assert(builder->ui->text);
	Assert(style.font);
	Assert(style.size > 0);
	UIP_TextData *text = arena_push_zero(builder->arena, sizeof(*text));
	text->arena = builder->arena;
	text->string = string;
	text->sizing_string = sizing_string;
	text->style = style;
	UIP_Box *box = uip_make_box(builder, string, desc);
	box->ops = &uip__text_ops;
	box->content = text;
	return box;
}
