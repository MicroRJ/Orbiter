
typedef struct
{
	Str    title;
	Str    path;
   u64    play_time_ms;
   u64    id;
   u32    mapper;
   b32    is_supported;
}
DisplayCard;

static UI_Box *app_build_catalog_card(UI_Context *ui, vec2 size, DisplayCard entry)
{
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_fixed(size.x));
	ui_size(ui, AXIS_Y, ui_fixed(size.y));
	ui_padd(ui, AXIS_X, 16.f, 16.f);
	ui_padd(ui, AXIS_Y, 16.f, 16.f);
	ui_gap(ui, 8.f);
	ui_background(ui, ui->theme.panel_outline);
	ui_roundness(ui, size.x * 0.02f);
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_overflow(ui, AXIS_Y, UI_BOX_OVERFLOW_CLIP);
	UI_Box *card_box = ui_box_begin(ui, entry.id, LIT("catalog card"));
	{
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_wrap());
		ui_size(ui, AXIS_Y, ui_wrap());
		UI_TextStyle style = app.ui->theme.code;
		style.size = 32;
		ui_text_box_string(ui, 0, style, entry.title);
		style.size = 18;
		style.color = app.ui->theme.text_subtle;
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_wrap());
		ui_size(ui, AXIS_Y, ui_wrap());
		ui_text_box(ui, 1, style, "MAPPER %u", entry.mapper);
		if (!entry.is_supported) ui_text_box_string(ui, 1, style, LIT("UNSUPPORTED CARTRIDGE"));

		ui_text_box(ui, 1, style, "%lluh %02llum", entry.play_time_ms / (60 * 60 * 1000), entry.play_time_ms / (60 * 1000) % 60);
	}
	ui_box_end(ui);
	return card_box;
}

// TODO(RJ) padding hardclips scrolling lists, we need to fade out the edges!
static DisplayCard *app_build_catalog_shelf(UI_Context *ui, UI_Key key, Str title, vec2 card_size, DisplayCard *entries, u32 entry_count)
{
	ui_box_push_id(ui, key);
	UI_TextStyle title_style = app.ui->theme.code;
	title_style.color = app.ui->theme.palette.amber;
	title_style.size = 64;
	title_style.align.y = 0.5f;
	title_style.align.x = 0.5f;
	ui_clean(ui);
	ui_text_box_string(ui, 1, title_style, title);

	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_grow(1.f));
	ui_size(ui, AXIS_Y, ui_wrap());
	UI_ScrollBox *scroll = ui_scroll_box_begin(ui, 2, AXIS_X);

	ui_clean(ui);
	ui_axis(ui, AXIS_X);
	ui_size(ui, AXIS_X, ui_fill());
	ui_size(ui, AXIS_Y, ui_wrap());
	ui_gap(ui, 16.f);
	ui_box_begin(ui, 3, LIT(""));

	DisplayCard *selected = 0;
	for (u32 index = 0; index < entry_count; index ++)
	{
		UI_Box *card_box = app_build_catalog_card(ui, card_size, entries[index]);
		if (entries[index].is_supported)
		{
			if (ui_signal_from_box(card_box).pressed)
			{
				ui_feedback_emit(ui, UI_FEEDBACK_PRESS);
				selected = & entries[index];
			}
		}
	}
	ui_box_end(ui);

	ui_scroll_box_end(scroll);
	ui_box_pop_id(ui);
	return selected;
}

static DisplayCard *library_build_ui(UI_Context *ui, rect_f32 window_rect)
{
	ui_clean(ui);
	ui_size(ui, AXIS_X, ui_fill());
	ui_size(ui, AXIS_Y, ui_fill());
	UI_ScrollBox *scroll = ui_scroll_box_begin(ui, UI_KEY("library vertical scroll"), AXIS_Y);

	ui_clean(ui);
	ui_axis(ui, AXIS_Y);
	ui_size(ui, AXIS_X, ui_fill());
	ui_size(ui, AXIS_Y, ui_fill());
	ui_padd(ui, AXIS_X, 32.f, 32.f);
	ui_padd(ui, AXIS_Y, 32.f, 32.f);
	ui_gap(ui, 16.f);
	ui_overflow(ui, AXIS_X, UI_BOX_OVERFLOW_CLIP);
	ui_box_begin(ui, UI_KEY("library shelves"), LIT("library shelves"));
	{
		DisplayCard *recent_saves = 0;
		u32 recent_save_count = 0;
		DisplayCard *games = 0;
		u32 game_count = 0;
		f32 card_width = window_rect.w * 0.13f;
		f32 card_height = card_width * 1.35f;
		app_build_catalog_shelf(ui, UI_KEY("saves"), LIT("Recent Saves"), v2(card_width, card_height), recent_saves, recent_save_count);
		app_build_catalog_shelf(ui, UI_KEY("games"), LIT("Games"), v2(card_width, card_height), games, game_count);
	}
	ui_box_end(ui);
	ui_scroll_box_end(scroll);
	DisplayCard *selected = 0;
	return selected;
}
