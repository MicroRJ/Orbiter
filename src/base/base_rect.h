typedef union
{
	struct { vec2 pos, size; };
	struct { f32 x, y, w, h; };
	struct { f32 xy[2], wh[2]; };
}
rect_f32;

typedef union
{
	struct { vec2i pos, size; };
	struct { i32 x, y, w, h; };
	struct { i32 xy[2], wh[2]; };
}
rect_i32;

typedef struct
{
	UV_Coords source;
	rect_f32 destination;
}
UV_Rect;

static b32 rect_i32_equal(rect_i32 a, rect_i32 b)
{
	return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static rect_i32 rect_i32_from_pos_size(vec2i pos, vec2i size)
{
	return (rect_i32) { .pos = pos, .size = size };
}

static rect_i32 rect_i32_from_size(vec2i size)
{
	return (rect_i32) { .size = size };
}

static rect_f32 rect_f32_from_size(vec2 size)
{
	return (rect_f32) { .size = size };
}

static rect_f32 rect_f32_from_i32(rect_i32 rect)
{
	return (rect_f32) { rect.x, rect.y, rect.w, rect.h };
}

static rect_i32 rect_i32_from_f32(rect_f32 rect)
{
	f32 x1 = floorf(rect.x);
	f32 y1 = floorf(rect.y);
	f32 x2 = ceilf(rect.x + rect.w);
	f32 y2 = ceilf(rect.y + rect.h);
	return (rect_i32) { (i32)x1, (i32)y1, (i32)(x2 - x1), (i32)(y2 - y1) };
}

static rect_f32 rect_f32_round_out(rect_f32 rect)
{
	f32 x2 = ceilf(rect.x + rect.w);
	f32 y2 = ceilf(rect.y + rect.h);
	rect.x = floorf(rect.x);
	rect.y = floorf(rect.y);
	rect.w = x2 - rect.x;
	rect.h = y2 - rect.y;
	return rect;
}

static rect_i32 rect_i32_intersect(rect_i32 a, rect_i32 b)
{
	i32 x1 = Max(a.x, b.x);
	i32 y1 = Max(a.y, b.y);
	i32 x2 = Min(a.x + a.w, b.x + b.w);
	i32 y2 = Min(a.y + a.h, b.y + b.h);
	return (rect_i32) { x1, y1, Max(0, x2 - x1), Max(0, y2 - y1) };
}

static rect_f32 rect_f32_inset(rect_f32 rect, f32 amount)
{
	rect.x += amount;
	rect.y += amount;
	rect.w -= amount * 2.f;
	rect.h -= amount * 2.f;
	return rect;
}

static rect_i32 rect_i32_inset(rect_i32 rect, i32 amount)
{
	rect.x += amount;
	rect.y += amount;
	rect.w -= amount * 2;
	rect.h -= amount * 2;
	return rect;
}

static rect_i32 rect_i32_from_slice(rect_i32 rect, AXIS axis, i32 amount)
{
	rect_i32 result;
	result.xy[axis] = rect.xy[axis] + (amount < 0) * (rect.wh[axis] + amount);
	result.xy[!axis] = rect.xy[!axis];
	result.wh[axis] = ABS(amount);
	result.wh[!axis] = rect.wh[!axis];
	return result;
}

static rect_i32 rect_i32_slice(rect_i32 *rect, AXIS axis, i32 amount)
{
	rect_i32 result = rect_i32_from_slice(*rect, axis, amount);
	rect->xy[axis] += Max(0, amount);
	rect->wh[axis] -= ABS(amount);
	return result;
}

static rect_f32 rect_f32_from_slice(rect_f32 rect, AXIS axis, f32 amount)
{
	rect_f32 result;
	result.xy[axis] = rect.xy[axis] + (amount < 0.f) * (rect.wh[axis] + amount);
	result.xy[!axis] = rect.xy[!axis];
	result.wh[axis] = fabsf(amount);
	result.wh[!axis] = rect.wh[!axis];
	return result;
}

static rect_f32 rect_f32_slice(rect_f32 *rect, AXIS axis, f32 amount)
{
	rect_f32 result = rect_f32_from_slice(*rect, axis, amount);
	rect->xy[axis] += Max(0.f, amount);
	rect->wh[axis] -= fabsf(amount);
	return result;
}

static rect_f32 rect_f32_split(rect_f32 *rect, AXIS axis, f32 ratio)
{
	f32 amount = rect->wh[axis] * ratio;
	return rect_f32_slice(rect, axis, amount);
}

static rect_f32 rect_f32_align(rect_f32 container, vec2 size, vec2 alignment)
{
	return (rect_f32) {
		.pos = v2(container.x + (container.w - size.x) * alignment.x, container.y + (container.h - size.y) * alignment.y),
		.size = size,
	};
}

static b32 rect_f32_contains(rect_f32 rect, vec2 point)
{
	return point.x >= rect.x && point.y >= rect.y && point.x < rect.x + rect.w && point.y < rect.y + rect.h;
}

static rect_f32 rect_f32_translate(rect_f32 rect, vec2 offset)
{
	rect.x += offset.x;
	rect.y += offset.y;
	return rect;
}

static rect_f32 rect_f32_translate_axis(rect_f32 rect, AXIS axis, f32 offset)
{
	rect.xy[axis] += offset;
	return rect;
}

static rect_i32 rect_i32_normalize(rect_i32 rect)
{
	if (rect.w < 0) {
		rect.x += rect.w;
		rect.w = -rect.w;
	}
	if (rect.h < 0) {
		rect.y += rect.h;
		rect.h = -rect.h;
	}
	return rect;
}
