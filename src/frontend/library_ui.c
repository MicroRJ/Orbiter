

static b32 library_entry_before(const Catalog_Entry *left, const Catalog_Entry *right, Catalog_EntryKind kind)
{
	if (kind == CATALOG_ENTRY_ORB && left->orb.metadata.last_played_unix_ms != right->orb.metadata.last_played_unix_ms) return left->orb.metadata.last_played_unix_ms > right->orb.metadata.last_played_unix_ms;
	return str_compare_nocase(left->title, right->title) < 0;
}

static u32 library_collect_entries(Catalog_EntryKind kind, const Catalog_Entry **entries)
{
	u32 count = 0;
	for (u32 index = 0; index < app.catalog.entry_count; index ++) {
		if (app.catalog.entries[index].kind == kind) entries[count ++] = &app.catalog.entries[index];
	}
	for (u32 index = 1; index < count; index ++)
	{
		const Catalog_Entry *entry = entries[index];
		u32 insert = index;
		while (insert && library_entry_before(entry, entries[insert - 1], kind))
		{
			entries[insert] = entries[insert - 1];
			insert --;
		}
		entries[insert] = entry;
	}
	return count;
}

static UI_Box *app_build_catalog_card(UI_Context *ui, vec2 size, const Catalog_Entry *entry)
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
	UI_Box *card_box = ui_box_begin(ui, entry->id, LIT("catalog card"));
	{
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_wrap());
		ui_size(ui, AXIS_Y, ui_wrap());
		UI_TextStyle style = app.ui->theme.code;
		style.size = 32;
		ui_text_box_string(ui, 0, style, entry->title);
		style.size = 18;
		style.color = app.ui->theme.text_subtle;
		ui_clean(ui);
		ui_size(ui, AXIS_X, ui_wrap());
		ui_size(ui, AXIS_Y, ui_wrap());
		if (entry->status == CATALOG_ENTRY_INVALID) ui_text_box_string(ui, 1, style, LIT("INVALID FILE"));
		else if (entry->status == CATALOG_ENTRY_UNSUPPORTED && entry->kind == CATALOG_ENTRY_ROM) ui_text_box_string(ui, 1, style, LIT("UNSUPPORTED CARTRIDGE"));
		else if (entry->status == CATALOG_ENTRY_UNSUPPORTED) ui_text_box_string(ui, 1, style, LIT("UNSUPPORTED ORB"));
		else if (entry->kind == CATALOG_ENTRY_ROM) ui_text_box(ui, 1, style, "MAPPER %u", entry->rom.cartridge.mapper);
		else ui_text_box(ui, 1, style, "%lluh %02llum", entry->orb.metadata.play_time_ms / (60 * 60 * 1000), entry->orb.metadata.play_time_ms / (60 * 1000) % 60);
	}
	ui_box_end(ui);
	return card_box;
}

// TODO(RJ) padding hardclips scrolling lists, we need to fade out the edges!
static void app_build_catalog_shelf(UI_Context *ui, UI_Key key, Str title, vec2 card_size, const Catalog_Entry *const *entries, u32 entry_count)
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

	const Catalog_Entry *selected = 0;
	for (u32 index = 0; index < entry_count; index ++)
	{
		UI_Box *card_box = app_build_catalog_card(ui, card_size, entries[index]);
		if (ui_signal_from_box(card_box).pressed && entries[index]->status == CATALOG_ENTRY_AVAILABLE)
		{
			ui_feedback_emit(ui, UI_FEEDBACK_PRESS);
			selected = entries[index];
		}
	}
	ui_box_end(ui);

	ui_scroll_box_end(scroll);
	ui_box_pop_id(ui);
	if (selected)
	{
		if (selected->kind == CATALOG_ENTRY_ROM) app_open_rom_path(selected->path);
		else if (selected->kind == CATALOG_ENTRY_ORB) app_restore_state_path(selected->path.text);
	}
}

static void library_build_ui(UI_Context *ui, rect_f32 window_rect)
{
	if (app.catalog_refresh_pending) app_refresh_catalog();
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

	const Catalog_Entry **entries = app.catalog.entry_count ? arena_push(&app.frame_arena, sizeof(*entries) * app.catalog.entry_count) : 0;
	f32 card_width = window_rect.w * 0.13f;
	f32 card_height = card_width * 1.35f;
	u32 orb_count = library_collect_entries(CATALOG_ENTRY_ORB, entries);
	if (orb_count) app_build_catalog_shelf(ui, UI_KEY("your orbs"), LIT("Your Orbs"), v2(card_width, card_height), entries, orb_count);
	u32 rom_count = library_collect_entries(CATALOG_ENTRY_ROM, entries);
	if (rom_count) app_build_catalog_shelf(ui, UI_KEY("games"), LIT("Games"), v2(card_width, card_height), entries, rom_count);

	ui_box_end(ui);
	ui_scroll_box_end(scroll);
}
