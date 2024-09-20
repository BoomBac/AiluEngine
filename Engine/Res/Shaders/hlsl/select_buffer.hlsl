//info bein
//pass begin::
//name: forward
//vert: VSMain
//pixel: PSMainFront
//ZTest: Always
//ZWrite: Off
//Cull: Off
//Queue: Opaque
//pass end::
//pass begin::
//name: forward
//vert: VSMain
//pixel: PSMainBack
//ZTest: Greater
//ZWrite: Off
//Cull: Off
//ColorMask: GBA
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
	result.position = TransformToClipSpace(v.position);
	return result;
}

float4 PSMainFront(PSInput input) : SV_TARGET
{
	return float4(0.5, 1.0, 1.0, 1.0);
}
float4 PSMainBack(PSInput input) : SV_TARGET
{
	return float4(0.0, 0.0, 1.0, 1.0);
}