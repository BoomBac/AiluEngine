#ifndef __COMMON_H__
#define __COMMON_H__
#include "cbuffer.hlsl"

float4 TransformToClipSpace(float3 object_pos)
{
	float4x4 mvp = mul(_MatrixWorld, _MatrixVP);
	return mul(float4(object_pos, 1.0f), mvp);
}

#endif // !__COMMON_H__

