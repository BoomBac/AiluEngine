//info begin
//pass begin::
//name: ui_cache_opaque
//vert: VSMain
//pixel: PSMain
//Cull: Off
//Queue: Transparent
//Blend: One,Zero
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
};

PerMaterialCBufferBegin
    float4 _Color;
PerMaterialCBufferEnd

TEXTURE2D(_MainTex)

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = TransformToClipSpace(input.position);
    output.uv = input.uv;
    output.color = input.color * _Color;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 rgb = SAMPLE_TEXTURE2D_LOD(_MainTex, g_LinearClampSampler, input.uv, 0).rgb * input.color.rgb;
    return float4(rgb, 1.0f);
}