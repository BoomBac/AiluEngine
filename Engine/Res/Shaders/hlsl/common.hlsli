#ifndef __COMMON_H__
#define __COMMON_H__
#include "cbuffer.hlsli"
#include "constants.hlsli"

//directx
#define UNITY_UV_STARTS_AT_TOP 1

SamplerState g_LinearWrapSampler : register(s0);
SamplerState g_LinearClampSampler : register(s1);
SamplerState g_LinearBorderSampler : register(s2);
SamplerState g_PointWrapSampler : register(s3);
SamplerState g_PointClampSampler : register(s4);
SamplerState g_PointBorderSampler : register(s5);
SamplerComparisonState g_ShadowSampler : register(s6);
SamplerState g_AnisotropicClampSampler : register(s7);

//convert z to 0(near)->1(far)
float DeviceDepthToLogic(float d)
{
#if defined(_REVERSED_Z)
	return 1.0 - d;
#else
	return d;
#endif
}

#define TEXTURE2D(name) Texture2D name;
#define TEXTURE3D(name) Texture3D name;
//#define TEXTURE2D(name,slot) Texture2D name : register(t##slot);
#define TEXTURECUBE(name) TextureCube name;
#define RWTEXTURE2D(name,type) RWTexture2D<type> name;
#define RWTEXTURE3D(name,type) RWTexture3D<type> name;
//#define TEXTURECUBE(name,slot) TextureCube name : register(t##slot);

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
#define SAMPLE_TEXTURE2D_DEPTH(textureName, samplerName, coord2)                         DeviceDepthToLogic(textureName.Sample(samplerName, coord2).x)
#define SAMPLE_TEXTURE2D_DEPTH_LOD(textureName, samplerName, coord2,lod)                 DeviceDepthToLogic(textureName.SampleLevel(samplerName, coord2,lod).x)

#define LOAD_TEXTURE2D(textureName, unCoord2)                                   textureName.Load(int3(unCoord2, 0))
#define LOAD_TEXTURE2D_LOD(textureName, unCoord2, lod)                          textureName.Load(int3(unCoord2, lod))
#define LOAD_TEXTURE2D_MSAA(textureName, unCoord2, sampleIndex)                 textureName.Load(unCoord2, sampleIndex)
#define LOAD_TEXTURE2D_ARRAY(textureName, unCoord2, index)                      textureName.Load(int4(unCoord2, index, 0))
#define LOAD_TEXTURE2D_ARRAY_MSAA(textureName, unCoord2, index, sampleIndex)    textureName.Load(int3(unCoord2, index), sampleIndex)
#define LOAD_TEXTURE2D_ARRAY_LOD(textureName, unCoord2, index, lod)             textureName.Load(int4(unCoord2, index, lod))
#define LOAD_TEXTURE3D(textureName, unCoord3)                                   textureName.Load(int4(unCoord3, 0))
#define LOAD_TEXTURE3D_LOD(textureName, unCoord3, lod)                          textureName.Load(int4(unCoord3, lod))


#define CBUFFER_START(name) cbuffer name : register(b0) {
#define CBUFFER_END }

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
// Z buffer to linear depth.
// Does NOT correctly handle oblique view frustums.
// Does NOT work with orthographic projection.
// zBufferParam = { (f-n)/n, 1, (f-n)/n*f, 1/f }
float LinearEyeDepth(float depth, float4 zBufferParam)
{
    return 1.0 / (zBufferParam.z * depth + zBufferParam.w);
}

// Z buffer to linear 0..1 depth (0 at camera position, 1 at far plane).
// Does NOT work with orthographic projections.
// Does NOT correctly handle oblique view frustums.
// zBufferParam = { (f-n)/n, 1, (f-n)/n*f, 1/f }
float Linear01Depth(float depth, float4 zBufferParam)
{
    return 1.0 / (zBufferParam.x * depth + zBufferParam.y);
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

float4 TransformToClipSpace(float3 obj_pos)
{
	float4x4 mvp = mul(_MatrixVP, _MatrixWorld);
	return mul(mvp, float4(obj_pos, 1.0f));
}
float4 TransformToClipSpaceNoJitter(float3 obj_pos)
{
	float4x4 mvp = mul(_MatrixVP_NoJitter, _MatrixWorld);
	return mul(mvp, float4(obj_pos, 1.0f));
}
float4 TransformWorldToHClip(float3 world_pos)
{
	return mul(_MatrixVP, float4(world_pos, 1.0f));
}
float4 TransformWorldToHClipNoJitter(float3 world_pos)
{
	return mul(_MatrixVP_NoJitter, float4(world_pos, 1.0f));
}
float4 TransformPreviousWorldToHClip(float3 world_pos)
{
	return mul(_MatrixVP_Pre, float4(world_pos, 1.0f));
}

float4 TransformObjectToWorld(float3 object_pos)
{
	return mul(_MatrixWorld, float4(object_pos, 1.0f));
}
float4 TransformPreviousObjectToWorld(float3 object_pos)
{
	return mul(_MatrixWorld_Pre, float4(object_pos, 1.0f));
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

float4 ComputeClipSpacePosition(float2 ndc_pos, float depth)
{
    float4 positionCS = float4(ndc_pos * 2.0 - 1.0, depth, 1.0);

#if UNITY_UV_STARTS_AT_TOP
    positionCS.y = -positionCS.y;
#endif
    return positionCS;
}
float3 ComputeWorldSpacePosition(float2 ndc_pos, float depth, float4x4 inv_vp)
{
    float4 positionCS  = ComputeClipSpacePosition(ndc_pos, depth);
    float4 hpositionWS = mul(inv_vp, positionCS);
    return hpositionWS.xyz / hpositionWS.w;
}

//reconstruct world pos from screen pos and depth
float3 Unproject(float2 screen_pos,float depth)
{
	screen_pos.y = 1.0 - screen_pos.y;
	float4 clipPos = float4(2.0f * screen_pos - 1.0f, depth, 1.0);
	float4 world_pos = mul(_MatrixIVP, clipPos);
	world_pos /= world_pos.w;
	return world_pos.xyz;
}
//reconstruct world pos from camera corners
float3 UnprojectByCameraRay(float2 screen_pos,float depth)
{
	float linear_z = Linear01Depth(depth, _ZBufferParams);
    float3 top = lerp(_LT, _RT, screen_pos.x);
    float3 bottom = lerp(_LB, _RB, screen_pos.x);
	float3 dir = lerp(top, bottom, screen_pos.y);
	return dir * linear_z;
}

//-----------------------------------------------------------------------------
//-- Orthonormal Basis Function -----------------------------------------------
//-- @nimitz's "Cheap orthonormal basis" on Shadertoy
//-- https://www.shadertoy.com/view/4sSSW3
float3x3 OrthonormalBasis(in float3 n, out float3 u, out float3 v)
{
    float3 f,r;
    if(n.z < -0.999999)
    {
        u = float3(0 , -1, 0);
        v = float3(-1, 0, 0);
    }
    else
    {
        float a = 1./(1. + n.z);
        float b = -n.x*n.y*a;
        u = normalize(float3(1. - n.x*n.x*a, b, -n.x));
        v = normalize(float3(b, 1. - n.y*n.y*a , -n.y));
    }
    return ( float3x3(u,v,n) );
}

half MaxCompHalf(half4 v) 
{
    return max(max(v.x, v.y), max(v.z, v.w));
}

half MinCompHalf(half4 v) 
{
    return min(min(v.x, v.y), min(v.z, v.w));
}

float MaxCompFloat(float4 v) 
{
    return max(max(v.x, v.y), max(v.z, v.w));
}

float MinCompFloat(float4 v) 
{
    return min(min(v.x, v.y), min(v.z, v.w));
}

// 打包 float4 (RGBA) 到 uint (32位)
uint PackFloat4(float4 color)
{
    uint4 scaled = uint4(color * 255.0 + 0.5);
    return (scaled.r << 24) | (scaled.g << 16) | (scaled.b << 8) | scaled.a;
}

// 解包 uint 到 float4 (RGBA)
float4 UnpackFloat4(uint packed)
{
    return float4(
        float((packed >> 24) & 0xFF) / 255.0,
        float((packed >> 16) & 0xFF) / 255.0,
        float((packed >> 8) & 0xFF) / 255.0,
        float(packed & 0xFF) / 255.0
    );
}

uint PackHalf2(half2 value)
{
    return f32tof16(value.x) | (f32tof16(value.y) << 16);
}

half2 UnpackHalf2(uint packed)
{
    return half2(f16tof32(packed), f16tof32(packed >> 16));
}

#endif // !__COMMON_H__ 

