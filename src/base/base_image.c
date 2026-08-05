#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static Image_r_u8 image_r_u8_from_data(vec2i reso, u32 elem_stride, void *data)
{
	return (Image_r_u8) { .reso = reso, .elem_stride = elem_stride, .data = data };
}

static Image_rgba_u8 image_rgba_u8_from_data(vec2i reso, u32 elem_stride, void *data)
{
	return (Image_rgba_u8) { .reso = reso, .elem_stride = elem_stride, .data = data };
}

Image_rgba_u8 push_image_rgba_u8_filled(Arena *arena, vec2i reso, u32 fill)
{
	void *data = arena_push_fill(arena, sizeof(vec4_u8) * reso.x * reso.y, fill);
	return image_rgba_u8_from_data(reso, reso.x, data);
}

Image_r_u8 push_image_r_u8_filled(Arena *arena, vec2i reso, u32 fill)
{
	void *data = arena_push_fill(arena, sizeof(u8) * reso.x * reso.y, fill);
	return image_r_u8_from_data(reso, reso.x, data);
}

Image_r_u8 push_image_r_u8(Arena *arena, vec2i reso)
{
	return push_image_r_u8_filled(arena, reso, 0);
}

Image_rgba_u8 push_image_rgba_u8(Arena *arena, vec2i reso)
{
	return push_image_rgba_u8_filled(arena, reso, 0);
}

Image_r_u8 slice_image_r_u8(Image_r_u8 image, rect_i32 rect)
{
	rect = rect_i32_intersect(rect_i32_from_size(image.reso), rect);
	return image_r_u8_from_data(rect.size, image.elem_stride, image.data + image.elem_stride * rect.y + rect.x);
}

typedef struct
{
	Platform_File file;
	u64 size;
	u64 position;
}
Image_FileStream;

static int image_file_read(void *user, char *data, int size)
{
	Image_FileStream *stream = user;
	if (size <= 0) return 0;
	u64 bytes_read = 0;
	if (!platform_read_file(stream->file, data, (u64)size, &bytes_read)) return 0;
	stream->position += bytes_read;
	return (int)bytes_read;
}

static void image_file_skip(void *user, int size)
{
	Image_FileStream *stream = user;
	u64 position = stream->position;
	if (platform_set_file_cursor(stream->file, PLATFORM_SEEK_CURRENT, size, &position)) stream->position = position;
}

static int image_file_eof(void *user)
{
	Image_FileStream *stream = user;
	return stream->position >= stream->size;
}

Image_rgba_u8 push_image_rgba_u8_from_file(Arena *arena, Str path)
{
	Assert(arena);
	if (!path.data || !path.size) return (Image_rgba_u8) {};

	u64 arena_position = arena->position;
	char *path_cstr = arena_push_aligned(arena, (u64)path.size + 1, 1);
	memory_copy(path_cstr, path.data, path.size);
	path_cstr[path.size] = 0;
	Platform_File file = platform_access_file(path_cstr, PLATFORM_FILE_OPEN_EXISTING, PLATFORM_FILE_READ | PLATFORM_FILE_SHARE_READ);
	arena->position = arena_position;
	if (!platform_file_is_valid(file)) return (Image_rgba_u8) {};

	u64 file_size = 0;
	if (!platform_get_file_size(file, &file_size))
	{
		platform_close_file(file);
		return (Image_rgba_u8) {};
	}

	Image_FileStream stream = { .file = file, .size = file_size };
	stbi_io_callbacks callbacks = { image_file_read, image_file_skip, image_file_eof };
	i32 width = 0;
	i32 height = 0;
	u8 *decoded = stbi_load_from_callbacks(&callbacks, &stream, &width, &height, 0, 4);
	platform_close_file(file);
	if (!decoded || width <= 0 || height <= 0)
	{
		stbi_image_free(decoded);
		return (Image_rgba_u8) {};
	}

	Image_rgba_u8 image = push_image_rgba_u8(arena, v2i(width, height));
	memory_copy(image.data, decoded, (u64)(u32)width * (u32)height * sizeof(*image.data));
	stbi_image_free(decoded);
	return image;
}
