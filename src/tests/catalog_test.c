#include "catalog.h"
#include "platform.h"

static u32 failures;

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); failures ++; } } while (0)

static b32 write_file(const char *path, const void *data, u64 size)
{
	Platform_File file = platform_access_file(path, PLATFORM_FILE_CREATE_ALWAYS, PLATFORM_FILE_WRITE);
	if (!platform_file_is_valid(file)) return false;
	u64 written = 0;
	b32 result = platform_write_file(file, data, size, &written) && written == size;
	platform_close_file(file);
	return result;
}

static void test_sources_and_round_trip(void)
{
	Arena source_arena = arena_create(MB(4), "catalog source test");
	Arena destination_arena = arena_create(MB(4), "catalog destination test");
	Arena scratch = arena_create(MB(4), "catalog scratch test");
	Catalog source;
	Catalog destination;
	catalog_init(&source, &source_arena);
	catalog_init(&destination, &destination_arena);
	CHECK(catalog_add_source(&source, LIT("D:\\Games\\NES")));
	CHECK(catalog_add_source(&source, LIT("E:\\Classics")));
	CHECK(!catalog_add_source(&source, LIT("d:/games/nes")));
	CHECK(catalog_remove_source(&source, LIT("E:\\Classics")));

	Catalog_EncodeResult encoded = catalog_to_source(&source, &scratch);
	CHECK(encoded.result.status == CATALOG_STATUS_OK && encoded.source.size);
	Catalog_Result decoded = catalog_from_source(&destination, LIT("round_trip.tab"), encoded.source, &scratch);
	CHECK(decoded.status == CATALOG_STATUS_OK);
	CHECK(destination.source_count == 1 && str_match(destination.sources[0], source.sources[0]));
	CHECK(!destination.dirty);

	catalog_destroy(&destination);
	catalog_destroy(&source);
	arena_destroy(&scratch);
	arena_destroy(&destination_arena);
	arena_destroy(&source_arena);
}

static void test_version_one_migration_is_transactional(void)
{
	Arena arena = arena_create(MB(4), "catalog migration test");
	Arena errors = arena_create(MB(4), "catalog error test");
	Catalog catalog;
	catalog_init(&catalog, &arena);
	Str legacy = LIT("version = 1\nlibrary = { folders = { item_0 = \"D:\\\\Games\\\\NES\" } recents = { item_0 = \"D:\\\\Games\\\\NES\\\\Metroid.nes\" } }\n");
	Catalog_Result result = catalog_from_source(&catalog, LIT("legacy.tab"), legacy, &errors);
	CHECK(result.status == CATALOG_STATUS_OK);
	CHECK(catalog.source_count == 1 && str_match(catalog.sources[0], LIT("D:\\Games\\NES")));
	CHECK(catalog.dirty);

	Str invalid = LIT("version = 99\ncatalog = { sources = {} }\n");
	result = catalog_from_source(&catalog, LIT("invalid.tab"), invalid, &errors);
	CHECK(result.status == CATALOG_STATUS_UNSUPPORTED_VERSION);
	CHECK(catalog.source_count == 1 && str_match(catalog.sources[0], LIT("D:\\Games\\NES")));
	CHECK(catalog.dirty);

	catalog_destroy(&catalog);
	arena_destroy(&errors);
	arena_destroy(&arena);
}

static void test_discovery(void)
{
	Arena arena = arena_create(MB(8), "catalog discovery test");
	Arena scratch = arena_create(MB(8), "catalog discovery scratch");
	char temporary[1024];
	Platform_Environment_Result environment = platform_get_environment("TEMP", temporary, sizeof(temporary));
	CHECK(environment.error == PLATFORM_ERROR_NONE && environment.found);
	if (environment.error != PLATFORM_ERROR_NONE || !environment.found) goto cleanup_arenas;
	Str directory = str_push_copy_f(&scratch, "%s\\orbiter-catalog-test-%llu", temporary, platform_counter());
	CHECK(platform_create_directory(directory.text));
	Str rom_path = str_push_copy_f(&scratch, "%.*s\\demo.nes", directory.size, directory.text);
	Str orb_path = str_push_copy_f(&scratch, "%.*s\\demo.orb", directory.size, directory.text);

	u64 rom_size = 16 + KiB(16) + KiB(8);
	u8 *rom = arena_push_zero(&scratch, rom_size);
	rom[0] = 'N'; rom[1] = 'E'; rom[2] = 'S'; rom[3] = 0x1A; rom[4] = 1; rom[5] = 1;
	rom[7] = 0x08; rom[12] = 2; rom[15] = 1;
	CHECK(write_file(rom_path.text, rom, rom_size));

	u8 state[] = { 1, 2, 3, 4 };
	Orb_Contents contents = {
		.metadata = {
			.system = ORB_SYSTEM_NES,
			.kind = ORB_SAVE_RESUME,
			.last_played_unix_ms = 1234,
			.title = LIT("Catalog Demo"),
			.source_path = LIT("games/catalog-demo.nes"),
		},
		.state = byte_span(state, sizeof(state)),
	};
	ByteSpan encoded = {};
	CHECK(orb_encode(&scratch, contents, &encoded).status == ORB_STATUS_OK);
	CHECK(write_file(orb_path.text, encoded.data, encoded.size));

	Catalog catalog;
	catalog_init(&catalog, &arena);
	CHECK(catalog_add_source(&catalog, directory));
	catalog_refresh(&catalog, &scratch, 0, 0);
	memory_fill(arena_push_aligned(&scratch, KiB(64), 1), 0xCD, KiB(64));
	CHECK(catalog.entry_count == 2);
	u32 rom_count = 0;
	u32 orb_count = 0;
	for (u32 index = 0; index < catalog.entry_count; index ++)
	{
		const Catalog_Entry *entry = &catalog.entries[index];
		if (entry->kind == CATALOG_ENTRY_ROM)
		{
			rom_count ++;
			CHECK(entry->status == CATALOG_ENTRY_AVAILABLE);
			CHECK(entry->rom.cartridge.prg_rom_size == KiB(16));
		}
		else if (entry->kind == CATALOG_ENTRY_ORB)
		{
			orb_count ++;
			CHECK(entry->status == CATALOG_ENTRY_AVAILABLE);
			CHECK(str_match(entry->title, LIT("Catalog Demo")));
			CHECK(entry->orb.metadata.last_played_unix_ms == 1234);
			CHECK(str_match(entry->orb.metadata.title, LIT("Catalog Demo")));
			CHECK(str_match(entry->orb.metadata.source_path, LIT("games/catalog-demo.nes")));
			CHECK(entry->orb.state_size == sizeof(state));
		}
	}
	CHECK(rom_count == 1 && orb_count == 1);
	catalog_destroy(&catalog);

	CHECK(platform_remove_file(orb_path.text));
	CHECK(platform_remove_file(rom_path.text));
	CHECK(platform_remove_directory(directory.text));

cleanup_arenas:
	arena_destroy(&scratch);
	arena_destroy(&arena);
}

int main(void)
{
	test_sources_and_round_trip();
	test_version_one_migration_is_transactional();
	test_discovery();
	if (failures)
	{
		fprintf(stderr, "catalog tests failed: %u\n", failures);
		return 1;
	}
	printf("catalog tests passed\n");
	return 0;
}
