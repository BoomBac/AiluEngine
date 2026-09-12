//info bein
//pass begin::
//name: wireframe
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
//Blend: Src,OneMinusSrc
//ZTest: LEqual
//Fill: Wireframe
//multi_compile _ PER_OBJECT_CB
//pass end::
//info end
//PER_OBJECT_CB：资产预览等非场景绘制路径没有 scene primitive 缓冲，切回 per-object cbuffer
#if defined(PER_OBJECT_CB)
#include "common.hlsli"
#else
#define AL_SCENE_PRIMITIVE 1
#include "common.hlsli"
#include "primitive.hlsli"
#endif

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
#if defined(PER_OBJECT_CB)
	result.position = TransformToClipSpace(v.position);
#else
	result.position = TransformPrimitiveToClipSpace(LoadPrimitive(v.instance_id), v.position);
#endif
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	//return float4(0.7,0.7,0.0,1.0);
	return float4(1.0,1.0,1.0,1.0);
}
