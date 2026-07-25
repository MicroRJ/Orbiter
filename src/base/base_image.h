#ifndef BASE_IMAGE_H
#define BASE_IMAGE_H

typedef struct
{
	vec2i reso;
	u32 elem_stride;
	u8 *data;
}
Image_r_u8;

typedef struct
{
	vec2i reso;
	u32 elem_stride;
	vec4_u8 *data;
}
Image_rgba_u8;

Image_rgba_u8 push_image_rgba_u8(Arena *arena, vec2i reso);
Image_r_u8 push_image_r_u8(Arena *arena, vec2i reso);
Image_r_u8 push_image_r_u8_filled(Arena *arena, vec2i reso, u32 color);
Image_rgba_u8 push_image_rgba_u8_filled(Arena *arena, vec2i reso, u32 fill);
Image_r_u8 slice_image_r_u8(Image_r_u8 image, rect_i32 rect);

#endif
