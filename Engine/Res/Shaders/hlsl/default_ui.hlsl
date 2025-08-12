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

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformToClipSpace(v.position);
    result.uv = v.uv;
	result.color = v.color * _Color;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return SAMPLE_TEXTURE2D_LOD(_MainTex, g_LinearClampSampler, input.uv, 0) * input.color;
}