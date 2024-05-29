//info bein
//pass begin::
//name: unlit
//vert: VSMain
//pixel: PSMain
//Cull: None
//Queue: Opaque
//pass end::
//info end

#include "common.hlsli"
CBufBegin
	float4 base_color;
CBufEnd

struct VSInput
{
	float3 position : POSITION;
};

struct PSInput
{
	float4 position : SV_POSITION;
};

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformToClipSpace(v.position);
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(base_color);
}
