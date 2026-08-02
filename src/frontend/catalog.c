#include "catalog.h"
#include "nes/emulator.h"
#include "platform.h"

#define TABULA_USE_EXTERNAL_TYPES
#include "tabula/tabula.h"

enum
{
	CATALOG_SOURCE_VERSION = 2,
	CATALOG_LEGACY_SOURCE_VERSION = 1,
	CATALOG_LEGACY_MAX_RECENTS = 16,
	CATALOG_PATH_CAPACITY = 32768,
	CATALOG_DIRECTORY_NAME_CAPACITY = 1024,
	CATALOG_MAX_RECURSION_DEPTH = 64,
	CATALOG_PATH_BUCKET_COUNT = 4096,
	CATALOG_INES_HEADER_SIZE = 16,
};

typedef struct Catalog_EntryNode Catalog_EntryNode;
struct Catalog_EntryNode
{
	Catalog_EntryNode *next;
	Catalog_EntryNode *hash_next;
	Catalog_Entry entry;
	b32 has_game;
};

typedef struct
{
	Catalog *catalog;
	Arena *scratch;
	Catalog_EntryNode *first;
	Catalog_EntryNode *last;
	Catalog_EntryNode *buckets[CATALOG_PATH_BUCKET_COUNT];
	u32 entry_count;
	u32 error_count;
}
Catalog_Builder;

static Catalog_Result catalog_result_ok(void)
{
	return (Catalog_Result) { .status = CATALOG_STATUS_OK };
}

static Catalog_Result catalog_error(Arena *arena, Catalog_Status status, u64 line, u64 column, const char *format, ...)
{
	Assert(arena);
	va_list arguments;
	va_start(arguments, format);
	Str message = str_push_copy_v(arena, format, arguments);
	va_end(arguments);
	return (Catalog_Result) {
		.status = status,
		.line = line <= MAX_VALUE_U32 ? (u32)line : 0,
		.column = column <= MAX_VALUE_U32 ? (u32)column : 0,
		.message = message,
	};
}

static u8 catalog_path_fold(u8 byte)
{
	if (byte == '/') return '\\';
	if (byte >= 'A' && byte <= 'Z') return byte + ('a' - 'A');
	return byte;
}

static u64 catalog_path_hash(Str path)
{
	u64 hash = 14695981039346656037ull;
	for (u32 index = 0; index < path.size; index ++)
	{
		hash ^= catalog_path_fold((u8)path.text[index]);
		hash *= 1099511628211ull;
	}
	return hash ? hash : 1;
}

static b32 catalog_path_match(Str left, Str right)
{
	if (left.size != right.size) return false;
	for (u32 index = 0; index < left.size; index ++) {
		if (catalog_path_fold((u8)left.text[index]) != catalog_path_fold((u8)right.text[index])) return false;
	}
	return true;
}

static b32 catalog_path_is_valid(Str path)
{
	if (!path.text || !path.size) return false;
	for (u32 index = 0; index < path.size; index ++)
	{
		u8 byte = (u8)path.text[index];
		if (byte < 0x20 || byte == 0x7F) return false;
	}
	return true;
}

static Str catalog_title_from_path(Str path)
{
	u32 separator = str_find_last(path, LIT("/\\"));
	u32 first = separator == MAX_VALUE_U32 ? 0 : separator + 1;
	Str title = str_slice(path, first, path.size - first);
	u32 extension = str_find_last(title, LIT("."));
	return extension == MAX_VALUE_U32 ? title : str_slice(title, 0, extension);
}

static i32 catalog_find_source(const Catalog *catalog, Str path)
{
	for (u32 index = 0; index < catalog->source_count; index ++) {
		if (catalog_path_match(catalog->sources[index], path)) return (i32)index;
	}
	return -1;
}

void catalog_init(Catalog *catalog, Arena *arena)
{
	Assert(catalog);
	Assert(arena);
	*catalog = (Catalog) { .arena = arena, .entry_arena = arena_create(0, "catalog entries") };
}

void catalog_destroy(Catalog *catalog)
{
	if (!catalog) return;
	arena_destroy(&catalog->entry_arena);
	*catalog = (Catalog) {};
}

b32 catalog_add_source(Catalog *catalog, Str path)
{
	Assert(catalog);
	Assert(catalog->arena);
	if (!catalog_path_is_valid(path) || catalog_find_source(catalog, path) >= 0 || catalog->source_count == CATALOG_MAX_SOURCES) return false;
	catalog->sources[catalog->source_count ++] = str_push_copy(catalog->arena, path);
	catalog->dirty = true;
	return true;
}

b32 catalog_remove_source(Catalog *catalog, Str path)
{
	Assert(catalog);
	i32 found = catalog_find_source(catalog, path);
	if (found < 0) return false;
	u32 index = (u32)found;
	for (; index + 1 < catalog->source_count; index ++) catalog->sources[index] = catalog->sources[index + 1];
	catalog->source_count --;
catalog->sources[catalog->source_count] = (Str) {};
catalog->dirty = true;
return true;
}

const Catalog_Entry *catalog_find_path(const Catalog *catalog, Str path)
{
	Assert(catalog);
	for (u32 index = 0; index < catalog->entry_count; index ++) {
		if (catalog_path_match(catalog->entries[index].path, path)) return &catalog->entries[index];
	}
	return 0;
}

static Catalog_EntryNode *catalog_builder_add(Catalog_Builder *builder, Str path, Catalog_EntryKind kind, Platform_File_Info file_info)
{
	u64 hash = catalog_path_hash(path);
	u32 bucket = hash & (CATALOG_PATH_BUCKET_COUNT - 1);
	for (Catalog_EntryNode *node = builder->buckets[bucket]; node; node = node->hash_next) {
		if (catalog_path_match(node->entry.path, path)) return 0;
	}

	Catalog_EntryNode *node = arena_push_zero(&builder->catalog->entry_arena, sizeof(*node));
	node->entry = (Catalog_Entry) {
		.id = hash,
		.kind = kind,
		.status = CATALOG_ENTRY_INVALID,
		.system = kind == CATALOG_ENTRY_ROM ? ORB_SYSTEM_NES : 0,
		.path = str_push_copy(&builder->catalog->entry_arena, path),
		.file_size = file_info.size,
		.modified_unix_ms = file_info.modified_unix_ms,
	};
	node->entry.title = catalog_title_from_path(node->entry.path);
	node->hash_next = builder->buckets[bucket];
	builder->buckets[bucket] = node;
	if (builder->last) builder->last->next = node;
	else               builder->first = node;
	builder->last = node;
	builder->entry_count ++;
	return node;
}

static Platform_File catalog_open_for_inspection(const char *path)
{
	return platform_access_file(path, PLATFORM_FILE_OPEN_EXISTING,
	PLATFORM_FILE_READ | PLATFORM_FILE_SHARE_READ | PLATFORM_FILE_SHARE_WRITE | PLATFORM_FILE_SHARE_DELETE);
}

static b32 catalog_read_at(Platform_File file, u64 offset, void *data, u64 size)
{
	u64 position = 0;
	u64 read = 0;
	return platform_set_file_cursor(file, PLATFORM_SEEK_BEGIN, offset, &position) && position == offset && platform_read_file(file, data, size, &read) && read == size;
}

static void catalog_inspect_rom(Catalog_Builder *builder, Str path, Platform_File_Info file_info)
{
	Catalog_EntryNode *node = catalog_builder_add(builder, path, CATALOG_ENTRY_ROM, file_info);
	if (!node) return;
	Platform_File file = catalog_open_for_inspection(path.text);
	u8 header[CATALOG_INES_HEADER_SIZE];
	NES_CartridgeInfo cartridge = {};
	u64 file_size = 0;
	b32 header_read = platform_file_is_valid(file) && platform_get_file_size(file, &file_size) && catalog_read_at(file, 0, header, sizeof(header));
	b32 inspected = header_read && nes_cartridge_inspect_ines(byte_span(header, sizeof(header)), file_size, &cartridge);
	if (inspected)
	{
		node->entry.file_size = file_size;
		node->entry.rom.cartridge = cartridge;
		node->entry.status = nes_emulator_supports_cartridge(cartridge) ? CATALOG_ENTRY_AVAILABLE : CATALOG_ENTRY_UNSUPPORTED;
		node->has_game = true;
	}
	else if (header_read && header[0] == 'N' && header[1] == 'E' && header[2] == 'S' && header[3] == 0x1A && (header[7] & 0x0C) == 0x08)
	{
		node->entry.file_size = file_size;
		node->entry.status = CATALOG_ENTRY_UNSUPPORTED;
	}
	else builder->error_count ++;
	if (platform_file_is_valid(file)) platform_close_file(file);
}

static void catalog_inspect_orb(Catalog_Builder *builder, Str path, Platform_File_Info file_info)
{
	Catalog_EntryNode *node = catalog_builder_add(builder, path, CATALOG_ENTRY_ORB, file_info);
	if (!node) return;
	Platform_File file = catalog_open_for_inspection(path.text);
	if (!platform_file_is_valid(file)) {
		builder->error_count ++;
		return;
	}
	u64 file_size = 0;
	b32 got_file_size = platform_get_file_size(file, &file_size);
	if (got_file_size) node->entry.file_size = file_size;
	Orb_Result result = { .status = ORB_STATUS_INVALID_FORMAT };
	if (got_file_size && file_size <= builder->catalog->arena->reserved_size - builder->catalog->arena->position)
	{
		// SCRATCH_SCOPE(builder->scratch)
		{
			u8 *data = arena_push_aligned(builder->catalog->arena, file_size, 1);
			Orb_Descriptor descriptor = {};
			if (catalog_read_at(file, 0, data, file_size)) {
				result = orb_parse(byte_span(data, file_size), &descriptor);
			}
			if (result.status == ORB_STATUS_OK)
			{
				Orb_Metadata metadata = descriptor.metadata;
				metadata.title = str_push_copy(&builder->catalog->entry_arena, metadata.title);
				metadata.source_path = str_push_copy(&builder->catalog->entry_arena, metadata.source_path);
				node->entry.system = metadata.system;
				node->entry.orb = (Catalog_OrbInfo) {
					.metadata = metadata,
					.state_size = descriptor.state_chunk.data.size,
					.thumbnail = descriptor.thumbnail,
					.has_thumbnail = descriptor.thumbnail.pixels.size != 0,
				};
				if (metadata.title.size) node->entry.title = metadata.title;
				node->entry.status = metadata.system == ORB_SYSTEM_NES ? CATALOG_ENTRY_AVAILABLE : CATALOG_ENTRY_UNSUPPORTED;
				node->has_game = true;
			}
		}
	}
	if (result.status == ORB_STATUS_UNSUPPORTED_VERSION || result.status == ORB_STATUS_UNSUPPORTED_CHUNK) node->entry.status = CATALOG_ENTRY_UNSUPPORTED;
	else if (result.status != ORB_STATUS_OK) builder->error_count ++;
	platform_close_file(file);
}

static Catalog_Game catalog_game_from_entry(const Catalog_Entry *entry)
{
	Catalog_Game game = {
		.id = entry->id,
		.system = entry->system,
		.status = entry->status,
		.title = entry->title,
		.source = entry,
	};
	if (entry->kind == CATALOG_ENTRY_ORB)
	{
		game.content_hash = entry->orb.metadata.content_hash;
		game.first_played_unix_ms = entry->orb.metadata.first_played_unix_ms;
		game.last_played_unix_ms = entry->orb.metadata.last_played_unix_ms;
		game.play_time_ms = entry->orb.metadata.play_time_ms;
		game.thumbnail = entry->orb.thumbnail;
		game.has_thumbnail = entry->orb.has_thumbnail;
	}
	return game;
}

static void catalog_scan_path(Catalog_Builder *builder, Str path, const Platform_File_Info *known_info, u32 depth)
{
	if (depth > CATALOG_MAX_RECURSION_DEPTH) { builder->error_count ++; return; }
	Platform_File_Info file_info = {};
	if (known_info) file_info = *known_info;
	else if (!platform_get_file_info(path.text, &file_info)) { builder->error_count ++; return; }

	if (!file_info.is_directory)
	{
		if (str_ends_nocase(path, LIT(".nes"))) catalog_inspect_rom(builder, path, file_info);
		else if (str_ends_nocase(path, LIT(".orb"))) catalog_inspect_orb(builder, path, file_info);
		return;
	}
	if (file_info.is_symbolic_link) return;

	Platform_Directory_Open_Result opened = platform_open_directory(path.text);
	if (opened.error != PLATFORM_ERROR_NONE) { builder->error_count ++; return; }
	for (;;)
	{
		// TODO(RJ) use path builder!
		char name[CATALOG_DIRECTORY_NAME_CAPACITY];
		Platform_Directory_Next_Result next = platform_next_directory(&opened.directory, name, sizeof(name));
		if (next.error != PLATFORM_ERROR_NONE) { builder->error_count ++; break; }
		if (!next.has_entry) break;
		if (next.info.is_directory && next.info.is_symbolic_link) continue;
		SCRATCH_SCOPE(builder->scratch)
		{
			b32 has_separator = path.size && (path.text[path.size - 1] == '/' || path.text[path.size - 1] == '\\');
			Str child = str_push_copy_f(builder->scratch, "%.*s%s%s", path.size, path.text, has_separator ? "" : "\\", name);
			catalog_scan_path(builder, child, &next.info, depth + 1);
		}
	}
	platform_close_directory(&opened.directory);
}

static void catalog_scan_source(Catalog_Builder *builder, Str source)
{
	SCRATCH_SCOPE(builder->scratch)
	{
		Str terminated = str_push_copy(builder->scratch, source);
		char *absolute = arena_push_aligned(builder->scratch, CATALOG_PATH_CAPACITY, 1);
		Platform_String_Result resolved = platform_get_absolute_path(terminated.text, absolute, CATALOG_PATH_CAPACITY);
		Str path = resolved.error == PLATFORM_ERROR_NONE && resolved.size <= MAX_VALUE_U32 ? str_from_data(absolute, (u32)resolved.size) : terminated;
		catalog_scan_path(builder, path, 0, 0);
	}
}

void catalog_refresh(Catalog *catalog, Arena *scratch, const Str *additional_sources, u32 additional_source_count)
{
	Assert(catalog);
	Assert(scratch);
	Assert(scratch != &catalog->entry_arena);
	Assert(additional_sources || !additional_source_count);
	arena_reset(&catalog->entry_arena);
	Catalog_Builder builder = { .catalog = catalog, .scratch = scratch };
	for (u32 index = 0; index < catalog->source_count; index ++) catalog_scan_source(&builder, catalog->sources[index]);
	for (u32 index = 0; index < additional_source_count; index ++) catalog_scan_source(&builder, additional_sources[index]);

	catalog->entry_count = builder.entry_count;
	catalog->entries = catalog->entry_count ? arena_push(&catalog->entry_arena, sizeof(*catalog->entries) * catalog->entry_count) : 0;
	catalog->game_count = 0;
	for (Catalog_EntryNode *node = builder.first; node; node = node->next) catalog->game_count += node->has_game;
	catalog->games = catalog->game_count ? arena_push(&catalog->entry_arena, sizeof(*catalog->games) * catalog->game_count) : 0;
	catalog->scan_error_count = builder.error_count;
	u32 entry_index = 0;
	u32 game_index = 0;
	for (Catalog_EntryNode *node = builder.first; node; node = node->next)
	{
		Catalog_Entry *entry = &catalog->entries[entry_index ++];
		*entry = node->entry;
		if (node->has_game) catalog->games[game_index ++] = catalog_game_from_entry(entry);
	}
	Assert(entry_index == catalog->entry_count);
	Assert(game_index == catalog->game_count);
	catalog->generation ++;
}

static Str catalog_string_from_tabula(Tabula_String string)
{
	Assert(string.length <= MAX_VALUE_U32);
	return str_from_data(string.data, (u32)string.length);
}

static b32 catalog_tabula_string_match(Tabula_String left, const char *right)
{
	return left.length <= MAX_VALUE_U32 && str_match(catalog_string_from_tabula(left), str_from_cstr(right));
}

static const Tabula_TableEntry *catalog_require_field(const Tabula_Table *table, const char *name)
{
	Str string = str_from_cstr(name);
	return tabula_table_get_entry(table, (Tabula_String) { string.text, string.size });
}

static Catalog_Result catalog_collect_paths(const Tabula_Table *table, const char *field_name, u32 maximum, Str *paths, u32 *path_count, Arena *error_arena)
{
	u64 count = tabula_table_count(table);
	if (count > maximum) return catalog_error(error_arena, CATALOG_STATUS_TOO_MANY_ENTRIES, 0, 0, "'%s' contains %llu entries; the maximum is %u", field_name, count, maximum);
	for (u32 index = 0; index < (u32)count; index ++)
	{
		const Tabula_TableEntry *entry = tabula_table_entry_at(table, index);
		char expected_key[32];
		i32 expected_size = snprintf(expected_key, sizeof(expected_key), "item_%u", index);
		Assert(expected_size > 0 && (u32)expected_size < sizeof(expected_key));
		if (!catalog_tabula_string_match(entry->key, expected_key)) return catalog_error(error_arena, CATALOG_STATUS_INVALID_SCHEMA, entry->key_range.line, entry->key_range.column, "expected '%s.%s', got '%.*s'", field_name, expected_key, (i32)entry->key.length, entry->key.data);
		if (entry->value.type != TABULA_VALUE_STRING) return catalog_error(error_arena, CATALOG_STATUS_INVALID_SCHEMA, entry->value_range.line, entry->value_range.column, "'%s.%s' must be a string, got %s", field_name, expected_key, tabula_value_type_name(entry->value.type));
		if (entry->value.as.string.length > MAX_VALUE_U32) return catalog_error(error_arena, CATALOG_STATUS_INVALID_SCHEMA, entry->value_range.line, entry->value_range.column, "'%s.%s' is too long", field_name, expected_key);
		Str path = catalog_string_from_tabula(entry->value.as.string);
		if (!catalog_path_is_valid(path)) return catalog_error(error_arena, CATALOG_STATUS_INVALID_SCHEMA, entry->value_range.line, entry->value_range.column, "'%s.%s' must contain a nonempty path without control characters", field_name, expected_key);
		for (u32 previous = 0; previous < index; previous ++) {
			if (catalog_path_match(paths[previous], path)) return catalog_error(error_arena, CATALOG_STATUS_INVALID_SCHEMA, entry->value_range.line, entry->value_range.column, "'%s.%s' duplicates an earlier path", field_name, expected_key);
		}
		paths[index] = path;
	}
	*path_count = (u32)count;
	return catalog_result_ok();
}

Catalog_Result catalog_from_source(Catalog *catalog, Str source_name, Str source, Arena *error_arena)
{
	Assert(catalog);
	Assert(catalog->arena);
	Assert(error_arena);
	Tabula_Context *context = tabula_context_create(0);
	if (!context) return catalog_error(error_arena, CATALOG_STATUS_EVALUATION_ERROR, 0, 0, "could not create the Tabula context");
	Tabula_ParseResult parsed = tabula_parse(context, (Tabula_Source) { .name = { source_name.text, source_name.size }, .text = { source.text, source.size } });
	if (!parsed.success)
	{
		const Tabula_Diagnostic *diagnostic = parsed.diagnostic_count ? parsed.diagnostics : 0;
		Catalog_Result result = diagnostic ? catalog_error(error_arena, CATALOG_STATUS_PARSE_ERROR, diagnostic->range.line, diagnostic->range.column, "%.*s", (i32)diagnostic->message.length, diagnostic->message.data) : catalog_error(error_arena, CATALOG_STATUS_PARSE_ERROR, 0, 0, "could not parse catalog state");
		tabula_context_destroy(context);
		return result;
	}
	Tabula_EvalResult evaluated = tabula_evaluate(context, parsed.ast, 0);
	if (!evaluated.success)
	{
		const Tabula_Diagnostic *diagnostic = evaluated.diagnostic_count ? evaluated.diagnostics : 0;
		Catalog_Result result = diagnostic ? catalog_error(error_arena, CATALOG_STATUS_EVALUATION_ERROR, diagnostic->range.line, diagnostic->range.column, "%.*s", (i32)diagnostic->message.length, diagnostic->message.data) : catalog_error(error_arena, CATALOG_STATUS_EVALUATION_ERROR, 0, 0, "could not evaluate catalog state");
		tabula_context_destroy(context);
		return result;
	}

	Catalog_Result result = catalog_result_ok();
	b32 migrated = false;
	Str source_paths[CATALOG_MAX_SOURCES] = {};
	u32 source_count = 0;
	Tabula_Table *root = evaluated.value.type == TABULA_VALUE_TABLE ? evaluated.value.as.table : 0;
	const Tabula_TableEntry *version = root ? catalog_require_field(root, "version") : 0;
	if (!root || !version) result = catalog_error(error_arena, CATALOG_STATUS_INVALID_SCHEMA, 0, 0, "expected root field 'version'");
	else if (version->value.type != TABULA_VALUE_INTEGER) result = catalog_error(error_arena, CATALOG_STATUS_INVALID_SCHEMA, version->value_range.line, version->value_range.column, "'version' must be an integer, got %s", tabula_value_type_name(version->value.type));
	else if (version->value.as.integer == CATALOG_LEGACY_SOURCE_VERSION)
	{
		const Tabula_TableEntry *library = catalog_require_field(root, "library");
		if (tabula_table_count(root) != 2 || !library || library->value.type != TABULA_VALUE_TABLE) result = catalog_error(error_arena, CATALOG_STATUS_INVALID_SCHEMA, 0, 0, "version 1 expects root fields 'version' and 'library'");
		else
		{
			const Tabula_TableEntry *folders = catalog_require_field(library->value.as.table, "folders");
			const Tabula_TableEntry *recents = catalog_require_field(library->value.as.table, "recents");
			if (tabula_table_count(library->value.as.table) != 2 || !folders || !recents || folders->value.type != TABULA_VALUE_TABLE || recents->value.type != TABULA_VALUE_TABLE) result = catalog_error(error_arena, CATALOG_STATUS_INVALID_SCHEMA, 0, 0, "version 1 expects library tables 'folders' and 'recents'");
			else
			{
				result = catalog_collect_paths(folders->value.as.table, "folders", CATALOG_MAX_SOURCES, source_paths, &source_count, error_arena);
				Str ignored_recents[CATALOG_LEGACY_MAX_RECENTS] = {};
				u32 ignored_recent_count = 0;
				if (result.status == CATALOG_STATUS_OK) result = catalog_collect_paths(recents->value.as.table, "recents", CATALOG_LEGACY_MAX_RECENTS, ignored_recents, &ignored_recent_count, error_arena);
				migrated = result.status == CATALOG_STATUS_OK;
			}
		}
	}
	else if (version->value.as.integer == CATALOG_SOURCE_VERSION)
	{
		const Tabula_TableEntry *catalog_entry = catalog_require_field(root, "catalog");
		if (tabula_table_count(root) != 2 || !catalog_entry || catalog_entry->value.type != TABULA_VALUE_TABLE) result = catalog_error(error_arena, CATALOG_STATUS_INVALID_SCHEMA, 0, 0, "version 2 expects root fields 'version' and 'catalog'");
		else
		{
			const Tabula_TableEntry *sources = catalog_require_field(catalog_entry->value.as.table, "sources");
			if (tabula_table_count(catalog_entry->value.as.table) != 1 || !sources || sources->value.type != TABULA_VALUE_TABLE) result = catalog_error(error_arena, CATALOG_STATUS_INVALID_SCHEMA, 0, 0, "version 2 expects catalog table 'sources'");
			else result = catalog_collect_paths(sources->value.as.table, "sources", CATALOG_MAX_SOURCES, source_paths, &source_count, error_arena);
		}
	}
	else result = catalog_error(error_arena, CATALOG_STATUS_UNSUPPORTED_VERSION, version->value_range.line, version->value_range.column, "unsupported catalog version %lld", version->value.as.integer);

	if (result.status == CATALOG_STATUS_OK)
	{
		catalog->source_count = 0;
		for (u32 index = 0; index < source_count; index ++) catalog->sources[catalog->source_count ++] = str_push_copy(catalog->arena, source_paths[index]);
		for (u32 index = source_count; index < CATALOG_MAX_SOURCES; index ++) catalog->sources[index] = (Str) {};
		catalog->dirty = migrated;
}
tabula_context_destroy(context);
return result;
}

static b32 catalog_write_path_table(Tabula_Context *context, Tabula_Table *table, const Str *paths, u32 count)
{
	for (u32 index = 0; index < count; index ++)
	{
		char key[32];
		i32 key_size = snprintf(key, sizeof(key), "item_%u", index);
		Assert(key_size > 0 && (u32)key_size < sizeof(key));
		Tabula_Value value = tabula_value_string(context, (Tabula_String) { paths[index].text, paths[index].size });
		if (value.type != TABULA_VALUE_STRING || !tabula_table_set(table, (Tabula_String) { key, (u32)key_size }, value)) return false;
	}
	return true;
}

Catalog_EncodeResult catalog_to_source(const Catalog *catalog, Arena *output_arena)
{
	Assert(catalog);
	Assert(output_arena);
	Catalog_EncodeResult result = {};
	Tabula_Context *context = tabula_context_create(0);
	if (!context)
	{
		result.result = catalog_error(output_arena, CATALOG_STATUS_ENCODING_ERROR, 0, 0, "could not create the Tabula context");
		return result;
	}
	Tabula_Table *root = tabula_table_create(context);
	Tabula_Table *catalog_table = tabula_table_create(context);
	Tabula_Table *sources = tabula_table_create(context);
	b32 success = root && catalog_table && sources;
	if (success) success = tabula_table_set(root, TABULA_STRING_LITERAL("version"), tabula_value_integer(CATALOG_SOURCE_VERSION));
	if (success) success = catalog_write_path_table(context, sources, catalog->sources, catalog->source_count);
	if (success) success = tabula_table_set(catalog_table, TABULA_STRING_LITERAL("sources"), (Tabula_Value) { .type = TABULA_VALUE_TABLE, .as.table = sources });
	if (success) success = tabula_table_set(root, TABULA_STRING_LITERAL("catalog"), (Tabula_Value) { .type = TABULA_VALUE_TABLE, .as.table = catalog_table });
	Tabula_String encoded = success ? tabula_table_to_source(context, root) : (Tabula_String) {};
	if (!encoded.data || encoded.length > MAX_VALUE_U32) result.result = catalog_error(output_arena, CATALOG_STATUS_ENCODING_ERROR, 0, 0, "could not encode catalog state");
	else
	{
		result.result = catalog_result_ok();
		result.source = str_push_copy(output_arena, str_from_data(encoded.data, (u32)encoded.length));
	}
	tabula_context_destroy(context);
	return result;
}

void catalog_mark_saved(Catalog *catalog)
{
	Assert(catalog);
	catalog->dirty = false;
}
