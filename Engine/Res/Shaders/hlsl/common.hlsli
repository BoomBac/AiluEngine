#ifndef __COMMON_H__
#define __COMMON_H__
#include "cbuffer.hlsli"
#include "constants.hlsli"
#include "sampler.hlsli"

//directx
#define UNITY_UV_STARTS_AT_TOP 1



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
/*
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
*/

float LinearEyeDepth(float depth, float near, float far)
{
#if defined(_REVERSED_Z)
    return (near * far) / (near - depth * (near - far));
#else
    return (near * far) / (far - depth * (far - near));
#endif
}
float Linear01Depth(float depth,float near, float far)
{
    float viewZ = LinearEyeDepth(depth, near, far);
#if defined(_REVERSED_Z)
    return saturate((viewZ - far) / (near - far));
#else
    return saturate((viewZ - near) / (far - near));
#endif
}


float3 TransformToViewSpace(float3 obj_pos)
{
    return mul(_MatrixV,float4(obj_pos,1.0f)).xyz;
}

#if !defined(AL_SCENE_PRIMITIVE)
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

float3 TransformObjectToWorld(float3 object_pos)
{
	return mul(_MatrixWorld, float4(object_pos, 1.0f)).xyz;
}
float3 TransformPreviousObjectToWorld(float3 object_pos)
{
	return mul(_MatrixWorld_Pre, float4(object_pos, 1.0f)).xyz;
}

float3 TransformNormal(float3 object_normal)
{
    float3x3 normal_matrix = transpose((float3x3)_MatrixInvWorld);
    return normalize(mul(normal_matrix, object_normal));
}
#endif

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
    float2 sign;// = (v.xy >= 0.0) ? float2(1.0, 1.0) : float2(-1.0, -1.0);
    sign.x = v.x >= 0.0 ? 1.0 : -1.0;
    sign.y = v.y >= 0.0 ? 1.0 : -1.0;
    return (1.0 - abs(v.yx)) * sign;
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
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
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

float3 Unproject(float2 screen_pos,float depth,float4x4 inv_vp)
{
	screen_pos.y = 1.0 - screen_pos.y;
	float4 clipPos = float4(2.0f * screen_pos - 1.0f, depth, 1.0);
	float4 world_pos = mul(inv_vp, clipPos);
	world_pos /= world_pos.w;
	return world_pos.xyz;
}

//reconstruct world pos from screen pos and depth
float3 Unproject(float2 screen_pos,float depth)
{
	return Unproject(screen_pos, depth, _MatrixIVP);
}


//reconstruct world pos from camera corners
// float3 UnprojectByCameraRay(float2 screen_pos,float depth)
// {
// 	float linear_z = Linear01Depth(depth);
//     float3 top = lerp(_LT, _RT, screen_pos.x);
//     float3 bottom = lerp(_LB, _RB, screen_pos.x);
// 	float3 dir = lerp(top, bottom, screen_pos.y);
// 	return dir * linear_z;
// }

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

