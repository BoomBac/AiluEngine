#ifndef __COMMON_H__
#define __COMMON_H__
#include "cbuffer.hlsli"


float4 TransformToClipSpace(float3 object_pos)
{
	float4x4 mvp = mul(_MatrixVP, _MatrixWorld);
	return mul(mvp, float4(object_pos, 1.0f));
}
float4 TransformFromWorldToClipSpace(float3 world_pos)
{
	return mul(_MatrixVP, float4(world_pos, 1.0f));
}

	float4 TransformToWorldSpace
	(
	float3 object_pos)
{
	return mul(_MatrixWorld, float4(object_pos, 1.0f));
	}

float3 TransformNormal(float3 object_normal)
{
	return normalize(mul(_MatrixWorld, float4(object_normal, 0.0f)).xyz);
}

	inline void GammaCorrect

	(inout
	float3 color, float gamma)
{
		color = pow(color, F3_WHITE / gamma);
	}

#endif // !__COMMON_H__ 

