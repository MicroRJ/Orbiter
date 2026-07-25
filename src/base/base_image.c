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
