//info bein
//name: wireframe
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
//Fill: Wireframe
//info end
#include "common.hlsl"

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
	return float4(0.7,0.7,0.0,1.0);
}
