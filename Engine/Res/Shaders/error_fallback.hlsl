//info bein
//name: error
//vert: VSMain
//pixel: PSMain
//Cull: None
//Queue: Opaque
//info end

#include "common.hlsli"

struct VSInput
{
	float3 position : POSITION;
};

struct PSInput
{
	float4 position : SV_POSITION;
};

CBufBegin
	float4 color;
CBufEnd

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformToClipSpace(v.position);
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(color);
}
