//info bein
//pass begin::
//name: ui_default
//vert: VSMain
//pixel: PSMain
//Cull: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
//pass end::
//info end

#include "common.hlsli"

struct VSInput
{
	float3 position : POSITION;
    float2 uv : TEXCOORD;
	float4 color : COLOR;
	float4 rect : TEXCOORD1;
	float4 corner_radius : TEXCOORD2;
	float4 border_thickness : TEXCOORD3;
};

struct PSInput
{
	float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
	float4 color : TEXCOORD1;
	float2 rect_size : TEXCOORD2;
	float4 corner_radius : TEXCOORD3;
	float4 border_thickness : TEXCOORD4;
};
PerMaterialCBufferBegin
	float4 _Color;
PerMaterialCBufferEnd

TEXTURE2D(_MainTex)

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformToClipSpace(v.position);
    result.uv = v.uv;
	result.color = v.color * _Color;
	result.rect_size = v.rect.zw;
	result.corner_radius = v.corner_radius;
	result.border_thickness = v.border_thickness;
	return result;
}

float RoundedRectSDF(float2 p, float2 size, float4 corner_radius)
{
	float2 half_size = size * 0.5f;
	float2 centered = p - half_size;
	float radius = centered.x < 0.0f ? (centered.y < 0.0f ? corner_radius.x : corner_radius.w)
	                                 : (centered.y < 0.0f ? corner_radius.y : corner_radius.z);
	radius = clamp(radius, 0.0f, min(half_size.x, half_size.y));
	float2 q = abs(centered) - half_size + radius;
	return length(max(q, 0.0f)) + min(max(q.x, q.y), 0.0f) - radius;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 color = SAMPLE_TEXTURE2D_LOD(_MainTex, g_LinearClampSampler, input.uv, 0) * input.color;
	if (any(input.corner_radius > 0.0f))
	{
		float dist = RoundedRectSDF(input.uv * input.rect_size, input.rect_size, input.corner_radius);
		float aa = max(fwidth(dist), 0.001f);
		color.a *= 1.0f - smoothstep(-aa, aa, dist);
	}
	if (any(input.border_thickness > 0.0f))
	{
		float2 p = input.uv * input.rect_size;
		float2 inner_pos = p - input.border_thickness.xy;
		float2 inner_size = max(input.rect_size - input.border_thickness.xy - input.border_thickness.zw, 1.0f);
		float4 inner_radius = max(input.corner_radius - float4(
			max(input.border_thickness.x, input.border_thickness.y),
			max(input.border_thickness.y, input.border_thickness.z),
			max(input.border_thickness.z, input.border_thickness.w),
			max(input.border_thickness.w, input.border_thickness.x)), 0.0f);
		float outer_dist = RoundedRectSDF(p, input.rect_size, input.corner_radius);
		float inner_dist = RoundedRectSDF(inner_pos, inner_size, inner_radius);
		float aa = max(fwidth(outer_dist), 0.001f);
		float outer_alpha = 1.0f - smoothstep(-aa, aa, outer_dist);
		float inner_alpha = 1.0f - smoothstep(-aa, aa, inner_dist);
		color.a *= outer_alpha * (1.0f - inner_alpha);
	}
	return color;
}
