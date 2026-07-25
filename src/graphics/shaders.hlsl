// Shared renderer constants. Keep this layout synchronized with _CBUFFER in
// d3d11.c. Individual shaders assign names to g_custom slots near their entry
// point rather than spreading raw component indices through their code.
cbuffer Globals : register(b0)
{
	float4x4 g_transform;
	float4x4 g_mixer;
	float2 g_texture0_reso;
	float2 g_padding;
	float g_opacity;
	float3 g_padding2;
	float4 g_custom[4];
}

Texture2D g_texture0 : register(t0);
SamplerState g_sampler0 : register(s0);

float4 SampleTexture(float2 uv)
{
	return g_texture0.Sample(g_sampler0, uv);
}

float3 EncodeSRGB(float3 linear_color)
{
	float3 low = linear_color * 12.92f;
	float3 high = 1.055f * pow(linear_color, 1.f / 2.4f) - 0.055f;
	float3 use_high = step(0.0031308f, linear_color);
	return lerp(low, high, use_high);
}

float PixelNoise(float2 position)
{
	uint2 pixel = uint2(position);
	uint hash = pixel.x * 0x1F123BB5u ^ pixel.y * 0x5F356495u;
	hash ^= hash >> 16;
	hash *= 0x7FEB352Du;
	hash ^= hash >> 15;
	hash *= 0x846CA68Bu;
	hash ^= hash >> 16;
	return (float)hash / 4294967295.f;
}

// Rectangle pipeline

struct RectVertexInput
{
	float4 rect : POS;
	float4 source_uv : TEX;
	float4 color_top_left : COL0;
	float4 color_bottom_left : COL1;
	float4 color_top_right : COL2;
	float4 color_bottom_right : COL3;
	float4 corner_radii : CRAD;
	// x: border thickness, y: edge softness, z: omit texture, w: text coverage gamma.
	float4 style : STY;
};

struct RectPixelInput
{
	float4 position : SV_POSITION;
	float2 half_size : PSIZE;
	float2 uv : TEX;
	float2 local_uv : COLC;
	float4 tint : TINT;
	float corner_radius : CRAD;
	float border_thickness : BTHC;
	float edge_softness : SFT;
	float omit_texture : OTX;
	float coverage_gamma : GRAIN;
};

RectPixelInput VS_Rect(uint vertex_id : SV_VertexID, RectVertexInput input)
{
	float2 rect_min = input.rect.xy;
	float2 rect_max = input.rect.xy + input.rect.zw;
	float2 positions[4] = {
		float2(rect_min.x, rect_max.y),
		float2(rect_min.x, rect_min.y),
		float2(rect_max.x, rect_max.y),
		float2(rect_max.x, rect_min.y),
	};
	float2 uvs[4] = {
		float2(input.source_uv.x, input.source_uv.w),
		float2(input.source_uv.x, input.source_uv.y),
		float2(input.source_uv.z, input.source_uv.w),
		float2(input.source_uv.z, input.source_uv.y),
	};
	float2 local_uvs[4] = {
		float2(0.f, 1.f),
		float2(0.f, 0.f),
		float2(1.f, 1.f),
		float2(1.f, 0.f),
	};
	float corner_radii[4] = {
		input.corner_radii.y,
		input.corner_radii.x,
		input.corner_radii.w,
		input.corner_radii.z,
	};

	float2 local_uv = local_uvs[vertex_id];
	float4 top_color = lerp(input.color_top_left, input.color_top_right, local_uv.x);
	float4 bottom_color = lerp(input.color_bottom_left, input.color_bottom_right, local_uv.x);

	RectPixelInput output;
	output.position = mul(float4(positions[vertex_id], 0.f, 1.f), g_transform);
	output.half_size = input.rect.zw * 0.5f;
	output.uv = uvs[vertex_id];
	output.local_uv = local_uv;
	output.tint = lerp(top_color, bottom_color, local_uv.y);
	output.corner_radius = corner_radii[vertex_id];
	output.border_thickness = input.style.x;
	output.edge_softness = input.style.y;
	output.omit_texture = input.style.z;
	output.coverage_gamma = input.style.w;
	return output;
}

float RoundedRectDistance(float2 position, float2 half_size, float corner_radius)
{
	return length(max(abs(position) - half_size + corner_radius, 0.f)) - corner_radius;
}

float4 PS_RectSDF(RectPixelInput input) : SV_TARGET
{
	float4 texture_color = mul(SampleTexture(input.uv), g_mixer);
	float4 albedo = input.tint * lerp(texture_color, 1.f, input.omit_texture);
	float2 position = (input.local_uv * 2.f - 1.f) * input.half_size;
	float2 outer_half_size = input.half_size - input.edge_softness * 2.f;
	float outer_distance = RoundedRectDistance(position, outer_half_size, input.corner_radius);
	float outer_coverage = 1.f - smoothstep(0.f, 2.f * input.edge_softness, outer_distance);

	float inner_radius = max(input.corner_radius - input.border_thickness, 0.f);
	float2 inner_half_size = outer_half_size - input.border_thickness;
	float inner_distance = RoundedRectDistance(position, inner_half_size, inner_radius);
	float inner_coverage = smoothstep(0.f, 2.f * input.edge_softness, inner_distance);
	if (input.border_thickness == 0.f) {
		inner_coverage = 1.f;
	}

	albedo.a *= outer_coverage * inner_coverage;
	return albedo;
}

float4 PS_TextMask(RectPixelInput input) : SV_TARGET
{
	float coverage = SampleTexture(input.uv).r;
	coverage = pow(saturate(coverage), input.coverage_gamma);
	return float4(input.tint.rgb, input.tint.a * coverage);
}

// Frame composition

float4 PS_Copy(RectPixelInput input) : SV_TARGET
{
	return SampleTexture(input.uv) * input.tint;
}

float2 DisplayBarrelUV(float2 uv)
{
	float2 position = uv * 2.f - 1.f;
	float radius_squared = dot(position, position);
	position *= 1.f + radius_squared * 0.008f;
	return position * 0.5f + 0.5f;
}

float4 PS_Blit(RectPixelInput input) : SV_TARGET
{
	float4 color = SampleTexture(input.uv);
	color.rgb = EncodeSRGB(max(color.rgb, 0.f));
	float dither = PixelNoise(input.position.xy) - 0.5f;
	color.rgb += dither / 255.f;
	return color * input.tint;
}

float4 PS_Barrel(RectPixelInput input) : SV_TARGET
{
	float strength = saturate(g_custom[0].x);
	float2 uv = lerp(input.uv, DisplayBarrelUV(input.uv), strength);
	if (any(uv < 0.f) || any(uv > 1.f)) {
		return float4(0.f, 0.f, 0.f, 1.f);
	}
	return SampleTexture(uv) * input.tint;
}

// Final-frame analog rewind treatment. The image stays readable while broad
// horizontal sync errors, chromatic lag, rolling streaks, and tape grain make
// time travel visually distinct from normal execution.
float4 PS_Rewind(RectPixelInput input) : SV_TARGET
{
	float time = g_custom[0].x;
	float strength = g_custom[0].y;
	float2 uv = input.uv;
	float band_id = floor(uv.y * 38.f - time * 23.f);
	float band_noise = PixelNoise(float2(band_id, floor(time * 18.f))) * 2.f - 1.f;
	float broad_tear = pow(saturate(abs(band_noise) * 1.18f - 0.46f), 3.f);
	float fine_wobble = sin(uv.y * 240.f + time * 37.f) * 0.0008f;
	float horizontal_shift = (band_noise * broad_tear * 0.022f + fine_wobble) * strength;
	float2 shifted_uv = saturate(uv + float2(horizontal_shift, 0.f));
	float chromatic_lag = (1.5f + broad_tear * 5.f) / g_texture0_reso.x * strength;

	float4 color = SampleTexture(shifted_uv);
	color.r = SampleTexture(saturate(shifted_uv + float2(chromatic_lag, 0.f))).r;
	color.b = SampleTexture(saturate(shifted_uv - float2(chromatic_lag, 0.f))).b;
	float luminance = dot(color.rgb, float3(0.2126f, 0.7152f, 0.0722f));
	color.rgb = lerp(color.rgb, luminance.xxx, 0.42f * strength);

	float rolling_position = frac(uv.y - time * 1.7f);
	float rolling_streak = exp(-rolling_position * rolling_position * 1800.f);
	float scanline = 0.94f + 0.06f * sin(input.position.y * 3.14159265f);
	float grain = PixelNoise(input.position.xy + float2(time * 997.f, time * 431.f)) - 0.5f;
	color.rgb *= lerp(1.f, scanline, strength);
	color.rgb += rolling_streak * 0.18f * strength;
	color.rgb += grain * 0.075f * strength;
	return float4(max(color.rgb, 0.f), 1.f);
}

// Separable Gaussian blur. g_custom[0] stores direction.xy and the center
// weight in z. The remaining three slots store paired offset/weight values.
float4 PS_Gaussian(RectPixelInput input) : SV_TARGET
{
	float2 texel_direction = g_custom[0].xy / g_texture0_reso;
	float4 color = SampleTexture(input.uv) * g_custom[0].z;
	for (uint pair = 1; pair < 4; ++pair)
	{
		float2 sample_offset = texel_direction * g_custom[pair].x;
		float pair_weight = g_custom[pair].y;
		color += SampleTexture(input.uv + sample_offset) * pair_weight;
		color += SampleTexture(input.uv - sample_offset) * pair_weight;
	}
	return color * input.tint;
}

// Blurred material. g_custom[0] is saturation, tint opacity, grain strength;
// g_custom[1].rgb is the tint color. Every input and output remains linear.
float4 PS_BlurMaterial(RectPixelInput input) : SV_TARGET
{
	float saturation = g_custom[0].x;
	float tint_opacity = g_custom[0].y;
	float grain_strength = g_custom[0].z;
	float3 tint_color = g_custom[1].rgb;

	float4 color = SampleTexture(input.uv);
	float luminance = dot(color.rgb, float3(0.2126f, 0.7152f, 0.0722f));
	color.rgb = lerp(luminance.xxx, color.rgb, saturation);
	color.rgb = lerp(color.rgb, tint_color, tint_opacity);
	color.rgb += (PixelNoise(input.position.xy) - 0.5f) * grain_strength;
	return color * input.tint;
}

// Frosted glass over a full-frame blurred texture. Refraction is strongest at
// the rounded boundary and settles toward the undistorted interior.
float4 PS_Glass(RectPixelInput input) : SV_TARGET
{
	float saturation = g_custom[0].x;
	float tint_opacity = g_custom[0].y;
	float grain_strength = g_custom[0].z;
	float corner_radius = g_custom[0].w;
	float distortion = g_custom[1].x;
	float distortion_width = max(g_custom[1].y, 1.f);
	float highlight_strength = g_custom[1].z;
	float shadow_strength = g_custom[1].w;
	float3 tint_color = g_custom[2].rgb;

	float2 local_position = (input.local_uv * 2.f - 1.f) * input.half_size;
	float distance = RoundedRectDistance(local_position, input.half_size - 1.f, corner_radius);
	float coverage = 1.f - smoothstep(0.f, 1.5f, distance);
	float edge = 1.f - saturate(-distance / distortion_width);
	float2 distance_gradient = float2(ddx(distance), ddy(distance));
	float2 normal = -distance_gradient / max(length(distance_gradient), 0.0001f);
	float refraction_profile = edge * edge * (3.f - 2.f * edge);
	float2 refracted_uv = saturate(input.uv - normal * refraction_profile * distortion / g_texture0_reso);
	float2 chromatic_offset = normal * refraction_profile * 0.65f / g_texture0_reso;
	float4 color = SampleTexture(refracted_uv);
	color.r = SampleTexture(saturate(refracted_uv + chromatic_offset)).r;
	color.b = SampleTexture(saturate(refracted_uv - chromatic_offset)).b;

	float luminance = dot(color.rgb, float3(0.2126f, 0.7152f, 0.0722f));
	color.rgb = lerp(luminance.xxx, color.rgb, saturation);
	color.rgb = lerp(color.rgb, tint_color, tint_opacity);
	float2 light_direction = normalize(float2(-0.65f, -0.76f));
	float rim = (1.f - smoothstep(0.f, 3.f, -distance)) * coverage;
	color.rgb += saturate(dot(normal, light_direction)) * rim * highlight_strength;
	color.rgb -= saturate(dot(normal, -light_direction)) * rim * shadow_strength;
	color.rgb += (PixelNoise(input.position.xy) - 0.5f) * grain_strength;
	return float4(color.rgb * input.tint.rgb, coverage * input.tint.a);
}

// CRT passes

float4 PS_CRTScanlines(RectPixelInput input) : SV_TARGET
{
	float3 color = SampleTexture(input.uv).rgb;
	float source_row = input.uv.y * g_texture0_reso.y;
	float scanline = 0.90f + 0.10f * cos(source_row * 6.28318530718f);
	float mask_phase = fmod(floor(input.position.x), 3.f);
	float3 mask = mask_phase < 1.f ? float3(1.08f, 0.96f, 0.96f)
		: mask_phase < 2.f ? float3(0.96f, 1.08f, 0.96f)
		: float3(0.96f, 0.96f, 1.08f);
	mask *= 3.f / (mask.r + mask.g + mask.b);
	return float4(color * mask * scanline, 1.f) * input.tint;
}

float4 PS_Luminance(RectPixelInput input) : SV_TARGET
{
	float3 color = SampleTexture(input.uv).rgb;
	float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
	float threshold = g_custom[0].x;
	float gain = g_custom[0].y;
	float bloom = saturate((luminance - threshold) / max(1.f - threshold, 0.001f));
	return float4(color * bloom * gain, 1.f);
}
