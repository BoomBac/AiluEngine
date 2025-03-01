#ifndef __SHADOW_CASTER_H__
#define __SHADOW_CASTER_H__
#include "standard_lit_common.hlsli"
#include "common.hlsli"

struct VSInput
{
	float3 position : POSITION;
	float2 uv : TEXCOORD;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD;
	float3 world_pos : TEXCOORD1;
};

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.world_pos = TransformObjectToWorld(v.position).xyz;
	result.position = TransformToClipSpace(v.position);
	result.uv = v.uv;
	return result;
}

float PSMain(PSInput input) : SV_Depth
{
#ifdef ALPHA_TEST
	float4 c = SAMPLE_TEXTURE2D(_AlbedoTex,g_LinearWrapSampler,input.uv);
	clip(c.a - _AlphaCulloff);
#endif

#ifdef CAST_POINT_SHADOW
	float dis = distance(input.world_pos,_CameraPos.xyz);
	float linear_z = saturate((dis - _ZBufferParams.x) / (_ZBufferParams.y - _ZBufferParams.x));
	#if defined(_REVERSED_Z)
		linear_z = 1 - linear_z;
	#endif
	return linear_z;
#else
	return input.position.z;
#endif
}

#define ZBufferNear _shadow_params.x
#define ZBufferFar  _shadow_params.y

#endif//