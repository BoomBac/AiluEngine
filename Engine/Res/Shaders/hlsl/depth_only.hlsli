#ifndef __DEPTH_ONLY__
#define __DEPTH_ONLY__

#define AL_SCENE_PRIMITIVE 1
#include "standard_lit_common.hlsli"
#include "common.hlsli"
#include "primitive.hlsli"

struct DepthVSInput
{
	float3 position : POSITION;
	float2 uv : TEXCOORD;
	uint instance_id : SV_INSTANCEID;
};

struct DepthPSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD;
	float3 world_pos : TEXCOORD1;
};

DepthPSInput DepthOnlyVSMain(DepthVSInput v)
{
	DepthPSInput result;
	const PrimitiveData primitive = LoadPrimitive(v.instance_id);
	result.world_pos = TransformPrimitiveToWorld(primitive, v.position).xyz;
	result.position = TransformPrimitiveToClipSpace(primitive, v.position);
	result.uv = v.uv;
	return result;
}

float DepthOnlyPSMain(DepthPSInput input) : SV_Depth
{
#ifdef ALPHA_TEST
	float4 c = SAMPLE_TEXTURE2D(_AlbedoTex,g_LinearWrapSampler,input.uv);
	clip(c.a - _AlphaCulloff);
#endif
    return input.position.z;
}

#endif//---------------------------------------------depth only pass---------------------------------------------
