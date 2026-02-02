#ifndef _FROXEL_COMMON_H_
#define _FROXEL_COMMON_H_
#include "common.hlsli"

// float3 VolumetricVoxelUVWToWorld(float3 uvw,float4x4 invView,float4x4 invProj,float nearZ,float farZ)
// {
//     // log depth
//     float viewZ = nearZ * pow(farZ / nearZ, uvw.z);
//     // ndc
//     float2 ndc = uvw.xy * 2.0 - 1.0;
//     // view ray
//     float4 clip = float4(ndc, 1, 1);
//     float4 view = mul(invProj, clip);
//     float3 dir = normalize(view.xyz / view.w);

//     float3 viewPos = dir * (viewZ / dir.z);
//     return mul(invView, float4(viewPos, 1)).xyz;
// }

// float3 WorldToVolumetricVoxelUVW(float3 worldPos,float4x4 view,float4x4 proj,float nearZ,float farZ)
// {
//     float3 viewPos = mul(view, float4(worldPos, 1)).xyz;
//     float4 clip = mul(proj, float4(viewPos, 1));
//     float3 ndc = clip.xyz / clip.w;
//     float2 uv = ndc.xy * 0.5 + 0.5;
//     float z = -viewPos.z;
//     float t = log(z / nearZ) / log(farZ / nearZ);
//     return float3(uv, t);
// }
float3 VolumetricVoxelUVWToWorld(float3 uvw,float4x4 matrix_iv,float4x4 matrix_ip, float _cam_near, float _cam_far)
{
    float viewZ = _cam_near * pow(_cam_far / _cam_near, uvw.z);
    float2 ndc = uvw.xy * 2.0 - 1.0;
    float4 ray_clip = float4(ndc, 1.0, 1.0);
    float4 ray_view = mul(matrix_ip, ray_clip);
    ray_view /= ray_view.w;
    float3 view_ray_dir = normalize(ray_view.xyz);
    float3 view_pos = view_ray_dir * (viewZ / abs(view_ray_dir.z));
    return mul(matrix_iv, float4(view_pos, 1)).xyz;
}

float3 WorldToVolumetricVoxelUVW(float3 world_pos,float4x4 matrix_v,float4x4 matrix_p, float _cam_near, float _cam_far)
{
    // world → view
    float3 viewPos = mul(matrix_v, float4(world_pos, 1)).xyz;
    // view → clip
    float4 clip = mul(matrix_p, float4(viewPos, 1.0));
    float3 ndc = clip.xyz / clip.w;
    // ndc → uv
    float2 uv = ndc.xy * 0.5 + 0.5;
    // viewZ → slice
    float z = abs(viewPos.z);
    float t = log(z / _cam_near) / log(_cam_far / _cam_near);
    //float t = (z - _cam_near) / (_cam_far - _cam_near);
    return float3(uv, t);
}

#define VOXEL_SLICE_COUNT 96

float SliceDistance(int z,float n, float f)
{
    return n * pow(f / n, (float(z)) / float(VOXEL_SLICE_COUNT));
}

float SliceThickness(int z,float n, float f)
{
    return abs(SliceDistance(z + 1, n, f) - SliceDistance(z, n, f));
}

#endif//_FROXEL_COMMON_H_