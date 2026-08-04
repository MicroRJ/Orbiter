#include "orb_runtime.h"

static void orb_test_write_u16(u8 *data, u16 value)
{
	data[0] = (u8)value;
	data[1] = (u8)(value >> 8);
}

static void orb_test_write_u32(u8 *data, u32 value)
{
	for (u32 index = 0; index < 4; index ++) data[index] = (u8)(value >> (index * 8));
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
	Arena encoded_arena = arena_create(MB(8), "ORB encoded test");
	Arena runtime_arena = arena_create(MB(8), "ORB runtime test");
	Arena roundtrip_arena = arena_create(MB(8), "ORB roundtrip test");
	u8 prg_rom[] = { 0x10, 0x20, 0x30, 0x40 };
	u8 chr_rom[] = { 0x50, 0x60, 0x70 };
	u8 first_state[] = { 1, 2, 3, 4, 5 };
	u8 second_state[] = { 9, 8, 7 };
	u8 thumbnail_pixels[] = {
		255, 0, 0, 255, 0, 255, 0, 255,
		0, 0, 255, 255, 255, 255, 255, 255,
	};
	NES_CartridgeDesc cartridge = {
		.prg_rom = byte_span(prg_rom, sizeof(prg_rom)),
		.chr_rom = byte_span(chr_rom, sizeof(chr_rom)),
		.mapper = 9,
		.vmirror = true,
	};
	Orb_Metadata metadata = {
		.content_hash = orb_cartridge_hash(cartridge),
		.first_played_unix_ms = 100,
		.last_played_unix_ms = 500,
		.play_time_ms = 400,
		.title = LIT("Incremental ORB"),
		.source_path = LIT("games/incremental.nes"),
	};
	Orb_Game cartridge_metadata = {
		.mapper = cartridge.mapper,
		.prg_rom_size = sizeof(prg_rom),
		.chr_rom_size = sizeof(chr_rom),
		.vmirror = cartridge.vmirror,
	};
	Orb_SaveMetadata first_save = {
		.id = { .bytes = { 1 } },
		.kind = ORB_SAVE_RESUME,
		.created_unix_ms = 100,
		.updated_unix_ms = 300,
		.play_time_ms = 200,
	};
	Orb_SaveMetadata second_save = {
		.id = { .bytes = { 2 } },
		.kind = ORB_SAVE_MANUAL,
		.created_unix_ms = 400,
		.updated_unix_ms = 500,
		.play_time_ms = 400,
	};
	Orb_Thumbnail thumbnail = {
		.width = 2,
		.height = 2,
		.stride = 8,
		.format = ORB_PIXEL_FORMAT_RGBA8,
		.pixels = byte_span(thumbnail_pixels, sizeof(thumbnail_pixels)),
	};

	Orb_Encoder encoder = orb_begin_encoding(&encoded_arena);
	Assert(orb_write_metadata_chunk(&encoder, metadata));
	Assert(orb_write_cartridge_chunk(&encoder, cartridge_metadata));
	Assert(orb_write_prg_rom_chunk(&encoder, cartridge.prg_rom));
	Assert(orb_write_chr_rom_chunk(&encoder, cartridge.chr_rom));
	Assert(orb_begin_save_chunk(&encoder));
	Assert(orb_write_save_metadata_chunk(&encoder, first_save));
	Assert(orb_write_save_state_chunk(&encoder, byte_span(first_state, sizeof(first_state))));
	Assert(orb_write_save_thumbnail_chunk(&encoder, thumbnail));
	Assert(orb_end_save_chunk(&encoder));
	Assert(orb_begin_save_chunk(&encoder));
	Assert(orb_write_save_metadata_chunk(&encoder, second_save));
	Assert(orb_write_save_state_chunk(&encoder, byte_span(second_state, sizeof(second_state))));
	Assert(orb_end_save_chunk(&encoder));
	ByteSpan encoded = {};
	Orb_Result result = orb_end_encoding(&encoder, &encoded);
	Assert(result.status == ORB_STATUS_OK && encoded.size);

	Orb_Decoder decoder = {};
	result = orb_begin_decoding(&decoder, encoded);
	Assert(result.status == ORB_STATUS_OK && decoder.version == ORB_FILE_VERSION_CURRENT);
	u32 root_chunk_count = 0;
	u32 save_count = 0;
	u64 first_state_payload_offset = 0;
	Orb_Chunk chunk = {};
	while (orb_read_chunk(&decoder, &chunk))
	{
		root_chunk_count ++;
		if (chunk.type == ORB_CHUNK_METADATA)
		{
			Orb_Metadata decoded = {};
			Assert(orb_decode_metadata_chunk(chunk, &decoded).status == ORB_STATUS_OK);
			Assert(hash256_match(decoded.content_hash, metadata.content_hash));
			Assert(str_match(decoded.title, metadata.title) && str_match(decoded.source_path, metadata.source_path));
		}
		else if (chunk.type == ORB_CHUNK_CARTRIDGE)
		{
			Orb_Game decoded = {};
			Assert(orb_decode_cartridge_chunk(chunk, &decoded).status == ORB_STATUS_OK);
			Assert(decoded.mapper == cartridge_metadata.mapper && decoded.prg_rom_size == sizeof(prg_rom) && decoded.chr_rom_size == sizeof(chr_rom));
		}
		else if (chunk.type == ORB_CHUNK_PRG_ROM)
		{
			ByteSpan decoded = {};
			Assert(orb_decode_prg_rom_chunk(chunk, &decoded).status == ORB_STATUS_OK);
			Assert(decoded.size == sizeof(prg_rom) && memory_match(decoded.data, prg_rom, sizeof(prg_rom)));
		}
		else if (chunk.type == ORB_CHUNK_CHR_ROM)
		{
			ByteSpan decoded = {};
			Assert(orb_decode_chr_rom_chunk(chunk, &decoded).status == ORB_STATUS_OK);
			Assert(decoded.size == sizeof(chr_rom) && memory_match(decoded.data, chr_rom, sizeof(chr_rom)));
		}
		else if (chunk.type == ORB_CHUNK_SAVE)
		{
			Orb_Decoder save_decoder = {};
			Assert(orb_begin_container_decoding(&save_decoder, &decoder, chunk).status == ORB_STATUS_OK);
			Assert(decoder.child_active);
			u32 save_child_count = 0;
			Orb_Chunk save_chunk = {};
			while (orb_read_chunk(&save_decoder, &save_chunk))
			{
				save_child_count ++;
				if (!save_count && save_chunk.type == ORB_CHUNK_STATE) first_state_payload_offset = save_chunk.payload_offset;
			}
			Assert(orb_end_decoding(&save_decoder).status == ORB_STATUS_OK);
			Assert(!decoder.child_active);
			Assert(save_child_count == (save_count ? 2 : 3));
			save_count ++;
		}
	}
	Assert(orb_end_decoding(&decoder).status == ORB_STATUS_OK);
	Assert(root_chunk_count == 6 && save_count == 2 && first_state_payload_offset < encoded.size);

	Orb_Decoder blocked_parent = {};
	Assert(orb_begin_decoding(&blocked_parent, encoded).status == ORB_STATUS_OK);
	Orb_Chunk blocked_container = {};
	while (orb_read_chunk(&blocked_parent, &blocked_container) && blocked_container.type != ORB_CHUNK_SAVE) {}
	Assert(blocked_container.type == ORB_CHUNK_SAVE);
	Orb_Decoder active_child = {};
	Assert(orb_begin_container_decoding(&active_child, &blocked_parent, blocked_container).status == ORB_STATUS_OK);
	Assert(blocked_parent.child_active);
	Orb_Chunk blocked_chunk = {};
	Assert(!orb_read_chunk(&blocked_parent, &blocked_chunk));
	Assert(blocked_parent.result.status == ORB_STATUS_INVALID_SEQUENCE);
	while (orb_read_chunk(&active_child, &blocked_chunk)) {}
	Assert(orb_end_decoding(&active_child).status == ORB_STATUS_OK);
	Assert(!blocked_parent.child_active);
	Assert(orb_end_decoding(&blocked_parent).status == ORB_STATUS_INVALID_SEQUENCE);

	Orb orb = {};
	result = orb_runtime_decode(&runtime_arena, encoded, &orb);
	Assert(result.status == ORB_STATUS_OK);
	Assert(orb.cartridge.mapper == cartridge.mapper && orb.cartridge.vmirror == cartridge.vmirror);
	Assert(orb.cartridge.prg_rom.size == sizeof(prg_rom) && memory_match(orb.cartridge.prg_rom.data, prg_rom, sizeof(prg_rom)));
	Assert(orb.cartridge.chr_rom.size == sizeof(chr_rom) && memory_match(orb.cartridge.chr_rom.data, chr_rom, sizeof(chr_rom)));
	Assert(orb.save_count == 2 && orb.first_save && orb.last_save && orb.first_save != orb.last_save);
	Assert(orb.first_save->state.size == sizeof(first_state) && memory_match(orb.first_save->state.data, first_state, sizeof(first_state)));
	Assert(orb.first_save->thumbnail.pixels.size == sizeof(thumbnail_pixels));
	Assert(orb.last_save->state.size == sizeof(second_state) && memory_match(orb.last_save->state.data, second_state, sizeof(second_state)));

	Orb_Encoder empty_encoder = orb_begin_encoding(&encoded_arena);
	Assert(orb_write_metadata_chunk(&empty_encoder, metadata));
	Assert(orb_write_cartridge_chunk(&empty_encoder, cartridge_metadata));
	Assert(orb_write_prg_rom_chunk(&empty_encoder, cartridge.prg_rom));
	Assert(orb_write_chr_rom_chunk(&empty_encoder, cartridge.chr_rom));
	ByteSpan empty_encoded = {};
	Assert(orb_end_encoding(&empty_encoder, &empty_encoded).status == ORB_STATUS_OK);
	Orb empty_orb = {};
	Assert(orb_runtime_decode(&runtime_arena, empty_encoded, &empty_orb).status == ORB_STATUS_OK);
	Assert(empty_orb.save_count == 0 && !empty_orb.first_save && !empty_orb.last_save);

	ByteSpan roundtrip = {};
	result = orb_runtime_encode(&roundtrip_arena, &orb, &roundtrip);
	Assert(result.status == ORB_STATUS_OK && roundtrip.size == encoded.size && memory_match(roundtrip.data, encoded.data, encoded.size));

	// Unknown optional chunks remain readable, but the runtime refuses to
	// rewrite them until it can preserve their payloads.
	u8 *extended_data = arena_push_aligned(&roundtrip_arena, encoded.size + 32, 1);
	memory_copy(extended_data, encoded.data, encoded.size);
	memory_zero(extended_data + encoded.size, 32);
	orb_test_write_u32(extended_data + encoded.size, ORB_FOURCC('E', 'X', 'T', 'N'));
	orb_test_write_u16(extended_data + encoded.size + 4, 1);
	orb_test_write_u16(extended_data + encoded.size + 30, 32);
	Orb extended_orb = {};
	Assert(orb_runtime_decode(&runtime_arena, byte_span(extended_data, encoded.size + 32), &extended_orb).status == ORB_STATUS_OK);
	Assert(extended_orb.has_unpreserved_chunks);
	ByteSpan unsupported_output = {};
	Assert(orb_runtime_encode(&roundtrip_arena, &extended_orb, &unsupported_output).status == ORB_STATUS_UNSUPPORTED_CHUNK);
	Assert(!unsupported_output.data && !unsupported_output.size);

	u64 rollback_position = encoded_arena.position;
	Orb_Encoder invalid = orb_begin_encoding(&encoded_arena);
	Assert(!orb_write_prg_rom_chunk(&invalid, cartridge.prg_rom));
	ByteSpan invalid_output = {};
	Assert(orb_end_encoding(&invalid, &invalid_output).status == ORB_STATUS_INVALID_SEQUENCE);
	Assert(!invalid_output.data && !invalid_output.size && encoded_arena.position == rollback_position);

	Arena small_arena = arena_create(64, "ORB small arena test");
	arena_push_aligned(&small_arena, 49, 1);
	u64 small_rollback_position = small_arena.position;
	Orb_Encoder too_small = orb_begin_encoding(&small_arena);
	Assert(too_small.result.status == ORB_STATUS_OUTPUT_TOO_LARGE);
	ByteSpan too_small_output = {};
	Assert(orb_end_encoding(&too_small, &too_small_output).status == ORB_STATUS_OUTPUT_TOO_LARGE);
	Assert(!too_small_output.data && !too_small_output.size && small_arena.position == small_rollback_position);
	arena_destroy(&small_arena);

	u8 *corrupt = arena_push_copy(&roundtrip_arena, encoded.size, encoded.data);
	corrupt[first_state_payload_offset] ^= 0x80;
	u64 corrupt_position = runtime_arena.position;
	Orb corrupt_orb = {};
	result = orb_runtime_decode(&runtime_arena, byte_span(corrupt, encoded.size), &corrupt_orb);
	Assert(result.status == ORB_STATUS_CHECKSUM_MISMATCH && runtime_arena.position == corrupt_position);

	const char store_path[] = "orb_store_test.orb";
	Assert(orb_test_write_file(store_path, encoded));
	Orb_Store store = {};
	orb_store_init(&store);
	Orb_StoreResult store_result = orb_store_load(&store, str_from_cstr(store_path));
	Assert(store_result.status == ORB_STORE_STATUS_OK);
	Assert(store.orb.save_count == 2);
	Assert(store.orb.cartridge.prg_rom.size == sizeof(prg_rom));
	orb_store_destroy(&store);
	Assert(platform_remove_file(store_path));

	arena_destroy(&roundtrip_arena);
	arena_destroy(&runtime_arena);
	arena_destroy(&encoded_arena);
	return 0;
}
