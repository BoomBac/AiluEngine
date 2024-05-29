#ifndef __COMMON_H__
#define __COMMON_H__
#include "cbuffer.hlsli"
#include "constants.hlsli"

SamplerState g_LinearWrapSampler : register(s0);
SamplerState g_LinearClampSampler : register(s1);
SamplerState g_LinearBorderSampler : register(s2);
SamplerComparisonState g_ShadowSampler : register(s3);


float Pow2(float x)
{
	return x*x;
}
float Pow4(float x)
{
	return x*x*x*x;
}

float4 TransformToClipSpace(float3 object_pos)
{
	float4x4 mvp = mul(_MatrixVP, _MatrixWorld);
	return mul(mvp, float4(object_pos, 1.0f));
}
float4 TransformFromWorldToClipSpace(float3 world_pos)
{
	return mul(_MatrixVP, float4(world_pos, 1.0f));
}

float4 TransformToWorldSpace(float3 object_pos)
{
	return mul(_MatrixWorld, float4(object_pos, 1.0f));
}

float3 TransformNormal(float3 object_normal)
{
	return normalize(mul(_MatrixWorld, float4(object_normal, 0.0f)).xyz);
}

float4 TransformToLightSpace(uint shadow_index, float3 object_pos)
{
	
	float4x4 mvp = mul(_ShadowMatrix[shadow_index], _MatrixWorld);
	return mul(mvp, float4(object_pos, 1.0f));
}

float4 TransformFromWorldToLightSpace(uint shadow_index, float3 world_pos)
{
	return mul(_ShadowMatrix[shadow_index], float4(world_pos, 1.0f));
}

	inline void GammaCorrect

	(inout
	float3 color, float gamma)
{
		color = pow(color, F3_WHITE / gamma);
	}

//https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
float2 OctWrap(float2 v)
{
	return (1.0 - abs(v.yx)) * (v.xy >= 0.0 ? 1.0 : -1.0);
}
 
float2 PackNormal(float3 n)
{
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
	n.xy = n.xy * 0.5 + 0.5;
	return n.xy;
}

float3 UnpackNormal(float2 f)
{
	f = f * 2.0 - 1.0;
// https://twitter.com/Stubbesaurus/status/937994790553227264
	float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
	float t = saturate(-n.z);
	n.xy += n.xy >= 0.0 ? -t : t;
	return normalize(n);
}

//reconstruct world pos
float3 Unproject(float2 screen_pos,float depth)
{
	float4 clipPos = float4(2.0f * screen_pos - 1.0f, depth, 1.0);
	float4 world_pos = mul(_MatrixIVP, clipPos);
	world_pos /= world_pos.w;
	return world_pos.xyz;
}

#endif // !__COMMON_H__ 

