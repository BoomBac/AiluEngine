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
};

struct PSInput
{
	float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};
PerMaterialCBufferBegin
	float4 _Color;
PerMaterialCBufferEnd

TEXTURE2D(_MainTex)

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformToClipSpace(v.position);
	// result.position.x -= 1.0f;
	// result.position.y += 1.0f;
    result.uv = v.uv;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return SAMPLE_TEXTURE2D_LOD(_MainTex,g_LinearClampSampler,input.uv,0) * _Color;
}