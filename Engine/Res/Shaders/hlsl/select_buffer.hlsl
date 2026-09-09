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

#define AL_SCENE_PRIMITIVE 1
#include "common.hlsli"
#include "primitive.hlsli"

struct VSInput
{
	float3 position : POSITION;
	uint instance_id : SV_INSTANCEID;
};

struct PSInput
{
	float4 position : SV_POSITION;
};

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformPrimitiveToClipSpaceNoJitter(LoadPrimitive(v.instance_id), v.position);
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
