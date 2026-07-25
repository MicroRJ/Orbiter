#ifndef GRAPHICS_COLOR_H
#define GRAPHICS_COLOR_H

typedef vec4_u8 Color_RGBA8;
typedef vec4 Color_Linear;

typedef struct
{
	f32 r, g, b, a;
}
Color_SRGBA;

static inline Color_SRGBA color_srgba(u32 packed)
{
	return (Color_SRGBA) {
		.r = (u8)(packed >> 16) / 255.f,
		.g = (u8)(packed >>  8) / 255.f,
		.b = (u8)(packed >>  0) / 255.f,
		.a = 1.f,
	};
}

static inline f32 color_decode_srgb_channel(f32 channel)
{
	return channel <= 0.04045f ? channel / 12.92f : powf((channel + 0.055f) / 1.055f, 2.4f);
}

static inline f32 color_encode_srgb_channel(f32 channel)
{
	return channel <= 0.0031308f ? channel * 12.92f : 1.055f * powf(channel, 1.f / 2.4f) - 0.055f;
}

static inline Color_Linear color_linear_from_srgba(Color_SRGBA color)
{
	return v4(
		color_decode_srgb_channel(color.r),
		color_decode_srgb_channel(color.g),
		color_decode_srgb_channel(color.b),
		color.a);
}

static inline Color_SRGBA color_with_alpha(Color_SRGBA color, f32 alpha)
{
	color.a = alpha;
	return color;
}

static inline Color_SRGBA color_srgba_mix(Color_SRGBA from, Color_SRGBA to, f32 amount)
{
	return (Color_SRGBA) {
		.r = f32_mix(from.r, to.r, amount),
		.g = f32_mix(from.g, to.g, amount),
		.b = f32_mix(from.b, to.b, amount),
		.a = f32_mix(from.a, to.a, amount),
	};
}

#define COLOR_TRANSPARENT ((Color_SRGBA) { 0.f, 0.f, 0.f, 0.f })
#define COLOR_BLACK       ((Color_SRGBA) { 0.f, 0.f, 0.f, 1.f })
#define COLOR_WHITE       ((Color_SRGBA) { 1.f, 1.f, 1.f, 1.f })
#define COLOR_RED         ((Color_SRGBA) { 1.f, 0.f, 0.f, 1.f })

#endif
