//info bein
//pass begin::
//name: ui_shadow
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
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 rect_size : TEXCOORD2;
    float4 corner_radius : TEXCOORD3;
};

PerMaterialCBufferBegin
    float4 _Color;
    float _ShadowSpread;
PerMaterialCBufferEnd

PSInput VSMain(VSInput v)
{
    PSInput result;
    result.position = TransformToClipSpace(v.position);
    result.uv = v.uv;
    result.color = v.color * _Color;
    result.rect_size = v.rect.zw;
    result.corner_radius = v.corner_radius;
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
    float2 shape_size = max(input.rect_size - _ShadowSpread * 2.0f, 1.0f);
    float2 shape_position = input.uv * input.rect_size - _ShadowSpread;
    float distance = RoundedRectSDF(shape_position, shape_size, input.corner_radius);
    // Keep a narrow inner overlap so the focused border does not become a bright seam.
    float inner_overlap = smoothstep(-2.0f, 0.0f, distance);
    float outside_alpha = 1.0f - smoothstep(0.0f, _ShadowSpread, max(distance, 0.0f));
    float4 color = input.color;
    color.a *= outside_alpha * inner_overlap;
    return color;
}
