#include "orb_runtime.h"

static u16 orb_test_u16(const u8 *data)
{
	u16 value = 0;
	ByteStream reader = byte_stream_reader(byte_span((void *)data, sizeof(value)));
	byte_transfer_u16(&reader, &value);
	Assert(!reader.failed);
	return value;
}

static u32 orb_test_u32(const u8 *data)
{
	u32 value = 0;
	ByteStream reader = byte_stream_reader(byte_span((void *)data, sizeof(value)));
	byte_transfer_u32(&reader, &value);
	Assert(!reader.failed);
	return value;
}

static u64 orb_test_u64(const u8 *data)
{
	u64 value = 0;
	ByteStream reader = byte_stream_reader(byte_span((void *)data, sizeof(value)));
	byte_transfer_u64(&reader, &value);
	Assert(!reader.failed);
	return value;
}

static b32 orb_test_write_file(const char *path, ByteSpan data)
{
	Platform_File file = platform_access_file(path, PLATFORM_FILE_CREATE_ALWAYS, PLATFORM_FILE_WRITE);
	if (!platform_file_is_valid(file)) return false;
	u64 written = 0;
	b32 success = platform_write_file(file, data.data, data.size, &written) && written == data.size;
	platform_close_file(file);
	return success;
}

int main(void)
{
	Arena arena = arena_create(MB(4), "ORB test");
	u8 thumbnail_pixels[] = {
		0x10, 0x20, 0x30, 0xFF, 0x40, 0x50, 0x60, 0xFF,
		0x70, 0x80, 0x90, 0xFF, 0xA0, 0xB0, 0xC0, 0xFF,
	};
	u8 state[] = { 1, 3, 5, 7, 9 };
	Orb_Contents expected = {
		.metadata = {
			.system = ORB_SYSTEM_NES,
			.kind = ORB_SAVE_RESUME,
			.id = { .bytes = { 1, 2, 3, 4 } },
			.content_hash = { .bytes = { 0xAA, 0xBB, 0xCC } },
			.created_unix_ms = 1000,
			.first_played_unix_ms = 2000,
			.last_played_unix_ms = 3000,
			.play_time_ms = 4000,
			.title = LIT("Demo"),
			.source_path = LIT("games/demo.nes"),
		},
		.thumbnail = {
			.width = 2,
			.height = 2,
			.stride = 8,
			.format = ORB_PIXEL_FORMAT_RGBA8,
			.pixels = { thumbnail_pixels, sizeof(thumbnail_pixels) },
		},
		.state = { state, sizeof(state) },
	};

	ByteSpan encoded = {};
	Orb_Result result = orb_encode(&arena, expected, &encoded);
	Assert(result.status == ORB_STATUS_OK);
	Assert(encoded.size == 280 && memory_match(encoded.data, "ORBS", 4));
	Assert(orb_test_u16(encoded.data + 4) == 1 && orb_test_u16(encoded.data + 6) == 16);
	Assert(!orb_test_u32(encoded.data + 8) && !orb_test_u32(encoded.data + 12));
	Assert(orb_test_u32(encoded.data + 16) == ORB_FOURCC('M', 'E', 'T', 'A'));
	Assert(orb_test_u16(encoded.data + 20) == 1 && orb_test_u16(encoded.data + 22) == 3);
	Assert(orb_test_u64(encoded.data + 24) == 122 && orb_test_u64(encoded.data + 32) == 122);
	Assert(!orb_test_u16(encoded.data + 44) && orb_test_u16(encoded.data + 46) == 32);
	Assert(orb_test_u32(encoded.data + 48) == ORB_SYSTEM_NES && orb_test_u32(encoded.data + 52) == ORB_SAVE_RESUME);
	Assert(orb_test_u64(encoded.data + 112) == 1000 && orb_test_u64(encoded.data + 120) == 2000);
	Assert(orb_test_u64(encoded.data + 128) == 3000 && orb_test_u64(encoded.data + 136) == 4000);
	Assert(orb_test_u32(encoded.data + 144) == 4 && orb_test_u32(encoded.data + 148) == 14);
	Assert(memory_match(encoded.data + 152, "Demogames/demo.nes", 18));
	Assert(orb_test_u32(encoded.data + 176) == ORB_FOURCC('T', 'H', 'M', 'B'));
	Assert(orb_test_u64(encoded.data + 184) == 32 && orb_test_u16(encoded.data + 206) == 32);
	Assert(orb_test_u32(encoded.data + 240) == ORB_FOURCC('S', 'T', 'A', 'T'));
	Assert(orb_test_u64(encoded.data + 248) == sizeof(state) && orb_test_u16(encoded.data + 270) == 32);

	Orb_Descriptor descriptor = {};
	result = orb_parse(encoded, &descriptor);
	Assert(result.status == ORB_STATUS_OK);
	Assert(descriptor.source.data == encoded.data && descriptor.source.size == encoded.size);
	Assert(descriptor.metadata.system == expected.metadata.system);
	Assert(descriptor.metadata.kind == expected.metadata.kind);
	Assert(memory_match(descriptor.metadata.id.bytes, expected.metadata.id.bytes, sizeof(expected.metadata.id.bytes)));
	Assert(memory_match(descriptor.metadata.content_hash.bytes, expected.metadata.content_hash.bytes, sizeof(expected.metadata.content_hash.bytes)));
	Assert(descriptor.metadata.created_unix_ms == expected.metadata.created_unix_ms);
	Assert(descriptor.metadata.first_played_unix_ms == expected.metadata.first_played_unix_ms);
	Assert(descriptor.metadata.last_played_unix_ms == expected.metadata.last_played_unix_ms);
	Assert(descriptor.metadata.play_time_ms == expected.metadata.play_time_ms);
	Assert(str_match(descriptor.metadata.title, expected.metadata.title));
	Assert(str_match(descriptor.metadata.source_path, expected.metadata.source_path));
	Assert(descriptor.metadata.title.data == (char *)encoded.data + 152 && descriptor.metadata.source_path.data == (char *)encoded.data + 156);
	Assert(descriptor.thumbnail.width == expected.thumbnail.width && descriptor.thumbnail.height == expected.thumbnail.height && descriptor.thumbnail.stride == expected.thumbnail.stride && descriptor.thumbnail.format == expected.thumbnail.format);
	Assert(descriptor.thumbnail.pixels.data == encoded.data + 224 && descriptor.thumbnail.pixels.size == sizeof(thumbnail_pixels) && memory_match(descriptor.thumbnail.pixels.data, thumbnail_pixels, sizeof(thumbnail_pixels)));
	Assert(descriptor.meta_chunk.type == ORB_FOURCC('M', 'E', 'T', 'A') && descriptor.meta_chunk.version == 1 && descriptor.meta_chunk.flags == 3);
	Assert(descriptor.meta_chunk.encoded.data == encoded.data + 16 && descriptor.meta_chunk.encoded.size == 154);
	Assert(descriptor.meta_chunk.data.data == encoded.data + 48 && descriptor.meta_chunk.data.size == 122);
	Assert(descriptor.thumbnail_chunk.type == ORB_FOURCC('T', 'H', 'M', 'B') && descriptor.thumbnail_chunk.encoded.data == encoded.data + 176 && descriptor.thumbnail_chunk.encoded.size == 64);
	Assert(descriptor.thumbnail_chunk.data.data == encoded.data + 208 && descriptor.thumbnail_chunk.data.size == 32);
	Assert(descriptor.state_chunk.type == ORB_FOURCC('S', 'T', 'A', 'T') && descriptor.state_chunk.encoded.data == encoded.data + 240 && descriptor.state_chunk.encoded.size == 37);
	Assert(descriptor.state_chunk.data.data == encoded.data + 272 && descriptor.state_chunk.data.size == sizeof(state) && memory_match(descriptor.state_chunk.data.data, state, sizeof(state)));
	u64 state_offset = (u8 *)descriptor.state_chunk.data.data - encoded.data;

	u8 *corrupt = arena_push_copy(&arena, encoded.size, encoded.data);
	u64 title_offset = (u8 *)descriptor.metadata.title.data - encoded.data;
	corrupt[title_offset] ^= 1;
	result = orb_parse(byte_span(corrupt, encoded.size), &descriptor);
	Assert(result.status == ORB_STATUS_CHECKSUM_MISMATCH && !descriptor.source.data);
	corrupt[title_offset] ^= 1;
	corrupt[state_offset] ^= 1;
	result = orb_parse(byte_span(corrupt, encoded.size), &descriptor);
	Assert(result.status == ORB_STATUS_CHECKSUM_MISMATCH);

	u8 *unsupported_thumbnail = arena_push_copy(&arena, encoded.size, encoded.data);
	unsupported_thumbnail[204] = 1;
	result = orb_parse(byte_span(unsupported_thumbnail, encoded.size), &descriptor);
	Assert(result.status == ORB_STATUS_OK && descriptor.thumbnail_chunk.data.size == 32 && !descriptor.thumbnail.pixels.size);

	result = orb_parse(byte_span(encoded.data, encoded.size - 1), &descriptor);
	Assert(result.status == ORB_STATUS_INVALID_FORMAT);

	const char store_path[] = "orb_store_test.orb";
	Assert(orb_test_write_file(store_path, encoded));
	Orb_Store store = {};
	orb_store_init(&store);
	Orb_StoreResult store_result = orb_store_load(&store, str_from_cstr(store_path));
	Assert(store_result.status == ORB_STORE_STATUS_OK);
	Assert(store.source_kind == ORB_STORE_SOURCE_ORB && store.orb.save_count == 1);
	Assert(store.orb.first_save == store.orb.last_save);
	Assert(str_match(store.orb.title, expected.metadata.title));
	Assert(store.orb.first_save->state.size == sizeof(state));
	Assert(memory_match(store.orb.first_save->state.data, state, sizeof(state)));
	orb_store_destroy(&store);
	Assert(platform_remove_file(store_path));

	arena_destroy(&arena);
	return 0;
}
