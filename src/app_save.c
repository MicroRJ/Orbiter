#include "app_save.h"
#include "nes_serialize.h"

enum
{
	APP_SAVE_MAGIC = (u32)'S' | (u32)'A' << 8 | (u32)'V' << 16 | (u32)'E' << 24,
	APP_SAVE_VERSION = 1,
};

static b32 app_save_thumbnail_valid(const Orb_Thumbnail *thumbnail)
{
	if (!thumbnail->pixels.data || !thumbnail->pixels.size) return false;
	if (!thumbnail->width || !thumbnail->height) return false;
	if (thumbnail->format != ORB_PIXEL_FORMAT_RGBA8) return false;
	if ((u64)thumbnail->stride < (u64)thumbnail->width * 4) return false;
	return thumbnail->pixels.size == (u64)thumbnail->stride * thumbnail->height;
}

ByteSpan app_save_encode(Arena *arena, const App_SaveData *data)
{
	Assert(arena && arena->memory && arena->position <= arena->reserved_size);
	Assert(data);
	u32 thumbnail_present = !!data->thumbnail.pixels.size;
	Assert(!thumbnail_present || app_save_thumbnail_valid(&data->thumbnail));

	ByteStream stream = byte_stream_arena_writer(arena);
	u32 magic = APP_SAVE_MAGIC;
	u32 version = APP_SAVE_VERSION;
	byte_transfer_u32(&stream, &magic);
	byte_transfer_u32(&stream, &version);
	orb_transfer_save_state(&stream, (Orb_SaveState *)&data->state);
	byte_transfer_u32(&stream, &thumbnail_present);
	if (thumbnail_present)
	{
		u32 width = data->thumbnail.width;
		u32 height = data->thumbnail.height;
		u32 stride = data->thumbnail.stride;
		u32 format = data->thumbnail.format;
		u64 pixel_size = data->thumbnail.pixels.size;
		byte_transfer_u32(&stream, &width);
		byte_transfer_u32(&stream, &height);
		byte_transfer_u32(&stream, &stride);
		byte_transfer_u32(&stream, &format);
		byte_transfer_u64(&stream, &pixel_size);
		byte_transfer_bytes(&stream, data->thumbnail.pixels);
	}
	return byte_stream_written(&stream);
}

b32 app_save_decode(Arena *arena, ByteSpan encoded, App_SaveData *data)
{
	Assert(arena && arena->memory && arena->position <= arena->reserved_size);
	Assert(data);
	u64 arena_position = arena->position;
	App_SaveData decoded = {};
	ByteSpan encoded_pixels = {};
	ByteStream stream = byte_stream_reader(encoded);
	u32 magic = 0;
	u32 version = 0;
	u32 thumbnail_present = 0;

	byte_transfer_u32(&stream, &magic);
	byte_transfer_u32(&stream, &version);
	if (stream.failed || magic != APP_SAVE_MAGIC || version != APP_SAVE_VERSION) goto failed;
	if (!orb_transfer_save_state(&stream, &decoded.state)) goto failed;
	byte_transfer_u32(&stream, &thumbnail_present);
	if (stream.failed || thumbnail_present > 1) goto failed;
	if (thumbnail_present)
	{
		u32 format = 0;
		u64 pixel_size = 0;
		byte_transfer_u32(&stream, &decoded.thumbnail.width);
		byte_transfer_u32(&stream, &decoded.thumbnail.height);
		byte_transfer_u32(&stream, &decoded.thumbnail.stride);
		byte_transfer_u32(&stream, &format);
		byte_transfer_u64(&stream, &pixel_size);
		decoded.thumbnail.format = (Orb_PixelFormat)format;
		encoded_pixels = byte_stream_take(&stream, pixel_size);
		decoded.thumbnail.pixels = encoded_pixels;
		if (stream.failed || !app_save_thumbnail_valid(&decoded.thumbnail)) goto failed;
	}
	if (stream.failed || byte_stream_remaining(&stream)) goto failed;

	if (encoded_pixels.size)
	{
		if (encoded_pixels.size > arena->reserved_size - arena->position) goto failed;
		u8 *pixels = arena_push_aligned(arena, encoded_pixels.size, 1);
		memory_copy(pixels, encoded_pixels.data, encoded_pixels.size);
		decoded.thumbnail.pixels = byte_span(pixels, encoded_pixels.size);
	}
	*data = decoded;
	return true;

failed:
	arena->position = arena_position;
	return false;
}
