//info bein
//name: DepthOnly
//vert: VSMain
//pixel: PSMain
//Cull: Front
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

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformToLightSpace(0,v.position);
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(1.0, 0.0, 1.0, 1.0);
}
