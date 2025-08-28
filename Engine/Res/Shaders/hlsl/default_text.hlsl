//info bein
//pass begin::
//name: default_font
//vert: VSMain
//pixel: PSMain
//Cull: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
//ZTest: Always
//ColorMask: RGB
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

TEXTURE2D(_MainTex)
PSInput VSMain(VSInput v,uint vert_id : SV_VertexID)
{
	PSInput result;
	result.position = TransformToClipSpace(v.position);
    result.color = v.color;
    result.uv = v.uv;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 c = SAMPLE_TEXTURE2D_LOD(_MainTex,g_LinearClampSampler,input.uv,0) * input.color;
    return c;
}