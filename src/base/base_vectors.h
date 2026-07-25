typedef union
{
	struct{ i32 x, y; };
	i32 xy[2];
}
vec2i;

typedef union
{
	struct{ f32 x, y; };
	f32 xy[2];
	f32 v[2];
}
vec2;

typedef union
{
	struct{ f32 x, y, z, w; };
	struct{ f32 r, g, b, a; };
	struct{ f32 x0, y0, x1, y1; };
	struct{ f32 u0, v0, u1, v1; };
	f32 xyzw[4];
	f32 rgba[4];
	f32 v[4];
	struct{
		vec2 xy;
		vec2 zw;
	};
}
vec4;

typedef vec4 UV_Coords;

typedef union
{
	struct{ u8 x, y, z, w; };
	struct{ u8 r, g, b, a; };
	u8 xyzw[4];
	u8 rgba[4];
}
vec4_u8;

static inline f32 f32_mix(f32 minimum, f32 maximum, f32 amount)
{
	return minimum + (maximum - minimum) * amount;
}

static inline vec2 v2(f32 x, f32 y)
{
	return (vec2) { x, y };
}

static inline vec2i v2i(i32 x, i32 y)
{
	return (vec2i) { x, y };
}

static inline vec2 v2_from_v2i(vec2i value)
{
	return v2((f32)value.x, (f32)value.y);
}

static inline vec4 v4(f32 x, f32 y, f32 z, f32 w)
{
	return (vec4) { x, y, z, w };
}

static inline vec2 v2_sub(vec2 a, vec2 b)
{
	return v2(a.x - b.x, a.y - b.y);
}

static inline vec2 v2_add(vec2 a, vec2 b)
{
	return v2(a.x + b.x, a.y + b.y);
}

static inline vec2 v2_mul(vec2 a, vec2 b)
{
	return v2(a.x * b.x, a.y * b.y);
}

static inline vec2 v2_div(vec2 a, vec2 b)
{
	return v2(a.x / b.x, a.y / b.y);
}

static inline vec2 v2_mul_add(vec2 a, vec2 b, vec2 c)
{
	return v2_add(v2_mul(a, b), c);
}

static inline f32 v2_cross(vec2 a, vec2 b)
{
	return a.x * b.y - a.y * b.x;
}

static inline vec2 v2_mix(vec2 a, vec2 b, f32 amount)
{
	return v2(f32_mix(a.x, b.x, amount), f32_mix(a.y, b.y, amount));
}

static inline f32 v2_dot(vec2 a, vec2 b)
{
	return a.x * b.x + a.y * b.y;
}

static inline f32 v2_dot2(vec2 a)
{
	return v2_dot(a, a);
}

static inline f32 v2_len(vec2 a)
{
	return sqrtf(v2_dot2(a));
}

static inline vec2i v2i_add(vec2i a, vec2i b)
{
	return v2i(a.x + b.x, a.y + b.y);
}
