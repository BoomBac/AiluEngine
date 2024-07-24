#ifndef __COMMON_H__
#define __COMMON_H__
#include "cbuffer.hlsli"
#include "constants.hlsli"

SamplerState g_LinearWrapSampler : register(s0);
SamplerState g_LinearClampSampler : register(s1);
SamplerState g_LinearBorderSampler : register(s2);
SamplerComparisonState g_ShadowSampler : register(s3);
SamplerState g_AnisotropicClampSampler : register(s4);

#define TEXTURE2D(name,slot) Texture2D name : register(t##slot);

//Ref:https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.core/ShaderLibrary/SpaceTransforms.hlsl
#define SAMPLE_TEXTURE2D(textureName, samplerName, coord2)                               textureName.Sample(samplerName, coord2)
#define SAMPLE_TEXTURE2D_LOD(textureName, samplerName, coord2, lod)                      textureName.SampleLevel(samplerName, coord2, lod)
#define SAMPLE_TEXTURE2D_BIAS(textureName, samplerName, coord2, bias)                    textureName.SampleBias(samplerName, coord2, bias)
#define SAMPLE_TEXTURE2D_GRAD(textureName, samplerName, coord2, dpdx, dpdy)              textureName.SampleGrad(samplerName, coord2, dpdx, dpdy)
#define SAMPLE_TEXTURE2D_ARRAY(textureName, samplerName, coord2, index)                  textureName.Sample(samplerName, float3(coord2, index))
#define SAMPLE_TEXTURE2D_ARRAY_LOD(textureName, samplerName, coord2, index, lod)         textureName.SampleLevel(samplerName, float3(coord2, index), lod)
#define SAMPLE_TEXTURE2D_ARRAY_BIAS(textureName, samplerName, coord2, index, bias)       textureName.SampleBias(samplerName, float3(coord2, index), bias)
#define SAMPLE_TEXTURE2D_ARRAY_GRAD(textureName, samplerName, coord2, index, dpdx, dpdy) textureName.SampleGrad(samplerName, float3(coord2, index), dpdx, dpdy)
#define SAMPLE_TEXTURECUBE(textureName, samplerName, coord3)                             textureName.Sample(samplerName, coord3)
#define SAMPLE_TEXTURECUBE_LOD(textureName, samplerName, coord3, lod)                    textureName.SampleLevel(samplerName, coord3, lod)
#define SAMPLE_TEXTURECUBE_BIAS(textureName, samplerName, coord3, bias)                  textureName.SampleBias(samplerName, coord3, bias)
#define SAMPLE_TEXTURECUBE_ARRAY(textureName, samplerName, coord3, index)                textureName.Sample(samplerName, float4(coord3, index))
#define SAMPLE_TEXTURECUBE_ARRAY_LOD(textureName, samplerName, coord3, index, lod)       textureName.SampleLevel(samplerName, float4(coord3, index), lod)
#define SAMPLE_TEXTURECUBE_ARRAY_BIAS(textureName, samplerName, coord3, index, bias)     textureName.SampleBias(samplerName, float4(coord3, index), bias)
#define SAMPLE_TEXTURE3D(textureName, samplerName, coord3)                               textureName.Sample(samplerName, coord3)
#define SAMPLE_TEXTURE3D_LOD(textureName, samplerName, coord3, lod)                      textureName.SampleLevel(samplerName, coord3, lod)

#define LOAD_TEXTURE2D(textureName, unCoord2)                                   textureName.Load(int3(unCoord2, 0))
#define LOAD_TEXTURE2D_LOD(textureName, unCoord2, lod)                          textureName.Load(int3(unCoord2, lod))
#define LOAD_TEXTURE2D_MSAA(textureName, unCoord2, sampleIndex)                 textureName.Load(unCoord2, sampleIndex)
#define LOAD_TEXTURE2D_ARRAY(textureName, unCoord2, index)                      textureName.Load(int4(unCoord2, index, 0))
#define LOAD_TEXTURE2D_ARRAY_MSAA(textureName, unCoord2, index, sampleIndex)    textureName.Load(int3(unCoord2, index), sampleIndex)
#define LOAD_TEXTURE2D_ARRAY_LOD(textureName, unCoord2, index, lod)             textureName.Load(int4(unCoord2, index, lod))
#define LOAD_TEXTURE3D(textureName, unCoord3)                                   textureName.Load(int4(unCoord3, 0))
#define LOAD_TEXTURE3D_LOD(textureName, unCoord3, lod)                          textureName.Load(int4(unCoord3, lod))

float Pow2(float x)
{
	return x*x;
}
float Pow4(float x)
{
	return x*x*x*x;
}
float SqrDistance(float3 p1,float3 p2)
{
	return dot(p1-p2,p1-p2);
}
float4x4 GetWorldToViewMatrix()
{
    return _MatrixV;
}

float3 GetCameraPositionWS()
{
	return _CameraPos.xyz;
}

// float4x4 GetViewToWorldMatrix()
// {
//     return _Matrix_I_V;
// }

float3 TransformToViewSpace(float3 obj_pos)
{
    return mul(_MatrixV,float4(obj_pos,1.0f));
}

float3 GetObjectWorldPos()
{
	return float3(_MatrixWorld[0][3],_MatrixWorld[1][3],_MatrixWorld[2][3]);
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

// Transforms vector from world space to view space
float3 TransformWorldToViewDir(float3 dir_ws, bool doNormalize = false)
{
    float3 dirVS = mul((float3x3)GetWorldToViewMatrix(), dir_ws).xyz;
    if (doNormalize)
        return normalize(dirVS);
    return dirVS;
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

inline void GammaCorrect(inout float3 color, float gamma)
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

