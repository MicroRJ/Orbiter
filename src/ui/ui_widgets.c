#include "ui_widgets.h"

typedef struct
{
	String string;
	String sizing_string;
	UI_TextStyle style;
}
UI_TextBoxData;

static vec2 ui_box__measure_text(UI_Box *box, UI_BoxConstraints constraints)
{
	(void)constraints;
	UI_TextBoxData *text = box->content;
	String measured_string = text->sizing_string.size ? text->sizing_string : text->string;
	return ui_measure_text(box->ui, text->style, measured_string);
}

static void ui_box__paint_text(UI_Box *box)
{
	UI_TextBoxData *text = box->content;
	vec2 text_size = ui_measure_text(box->ui, text->style, text->string);
	vec2 remaining = v2(Max(0.f, box->viewport.w - text_size.x), Max(0.f, box->viewport.h - text_size.y));
	vec2 position = v2_add(box->viewport.pos, v2_mul(remaining, text->style.align));
	ui_push_clip(box->ui, box->clip_rect);
	ui_push_clip(box->ui, box->viewport);
	ui_draw_text(box->ui, (rect_f32) { .pos = position, .size = text_size }, text->style, text->string);
	ui_pop_clip(box->ui);
	ui_pop_clip(box->ui);
}

static const UI_BoxOps ui_box__text_ops = {
	.measure = ui_box__measure_text,
	.paint = ui_box__paint_text,
};

UI_Box *ui_text_box(UI_BoxBuilder *builder, u64 key, String string, UI_BoxDesc desc, UI_TextStyle style)
{
	return ui_text_box_sized(builder, key, string, (String) {}, desc, style);
}

UI_Box *ui_text_box_sized(UI_BoxBuilder *builder, u64 key, String string, String sizing_string, UI_BoxDesc desc, UI_TextStyle style)
{
	Assert(builder);
	Assert(builder->ui);
	Assert(builder->ui->text);
	Assert(style.font);
	Assert(style.size > 0);
	UI_TextBoxData *text = arena_push_zero(builder->arena, sizeof(*text));
	text->string = string;
	text->sizing_string = sizing_string;
	text->style = style;
	UI_Box *box = ui_box_make(builder, key, string, desc);
	box->ops = &ui_box__text_ops;
	box->content = text;
	return box;
}
