//info bein
//pass begin::
//name: color_picker_sv
//vert: VSMain
//pixel: PSMain
//Cull: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
//ZWrite: Off
//ZTest: LEqual
//pass end::
//info end

#include "common.hlsli"
#include "color_space_utils.hlsli"

struct VSInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
    float4 rect : TEXCOORD1;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

PerMaterialCBufferBegin
    float _Hue;
PerMaterialCBufferEnd

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = TransformToClipSpace(input.position);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float3 HsvToRgb(float3 hsv)
{
    float h = frac(hsv.x) * 6.0f;
    float sector = floor(h);
    float f = h - sector;
    float p = hsv.z * (1.0f - hsv.y);
    float q = hsv.z * (1.0f - f * hsv.y);
    float t = hsv.z * (1.0f - (1.0f - f) * hsv.y);
    switch (int(sector))
    {
        case 0: return float3(hsv.z, t, p);
        case 1: return float3(q, hsv.z, p);
        case 2: return float3(p, hsv.z, t);
        case 3: return float3(p, q, hsv.z);
        case 4: return float3(t, p, hsv.z);
        default: return float3(hsv.z, p, q);
    }
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 hsv = float3(_Hue, saturate(input.uv.x), saturate(1.0f - input.uv.y));
    float3 srgb = HsvToRgb(hsv);
    return float4(RemoveSRGBCurve(srgb), 1.0f) * input.color;
}
