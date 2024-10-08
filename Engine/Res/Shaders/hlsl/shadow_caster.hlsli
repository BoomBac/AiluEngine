#ifndef __SHADOW_CASTER_H__
#define __SHADOW_CASTER_H__
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

// Values used to linearize the Z buffer (http://www.humus.name/temp/Linearize%20depth.txt)
// x = 1-far/near
// y = far/near
// z = x/far
// w = y/far
// or in case of a reversed depth buffer (UNITY_REVERSED_Z is 1)
// x = -1+far/near
// y = 1
// z = x/far
// w = 1/far
//float4 _ZBufferParams;

// PerMaterialCBufferBegin
// 	uint 	shadow_index;
// 	float3 _point_light_wpos;
// 	float   padding;
// 	float4 _shadow_params;
// PerMaterialCBufferEnd

//TEXTURE2D(_AlbedoTex);


PSInput VSMain(VSInput v)
{
	PSInput result;
	result.world_pos = TransformToWorldSpace(v.position).xyz;
	result.position = TransformToClipSpace(v.position);
	result.uv = v.uv;
	return result;
}

float PSMain(PSInput input) : SV_Depth
{
	// float4 c = SAMPLE_TEXTURE2D(_AlbedoTex,g_LinearWrapSampler,input.uv);
	// clip(c.a - 0.5);
#ifdef CAST_POINT_SHADOW
	float dis = distance(input.world_pos,_CameraPos.xyz);
	float linear_z = saturate((dis - _ZBufferParams.x) / (_ZBufferParams.y - _ZBufferParams.x));
	return linear_z;
#else
	return input.position.z;
#endif
}

#define ZBufferNear _shadow_params.x
#define ZBufferFar  _shadow_params.y

#endif//