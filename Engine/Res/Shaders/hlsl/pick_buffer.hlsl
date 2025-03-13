//info bein
//pass begin::
//name: pick_buffer
//vert: VSMain
//pixel: PSMain
//Cull: Off
//Queue: Opaque
//pass end::
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
	result.position = TransformToClipSpaceNoJitter(v.position);
	return result;
}

uint PSMain(PSInput input) : SV_TARGET
{
	return uint(_ObjectID);
}