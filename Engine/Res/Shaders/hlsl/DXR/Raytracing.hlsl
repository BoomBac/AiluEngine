//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#ifndef RAYTRACING_HLSL
#define RAYTRACING_HLSL

struct Viewport
{
    float left;
    float top;
    float right;
    float bottom;
};

struct RayGenConstantBuffer
{
    Viewport viewport;
    Viewport stencil;
};

#include "../../../../Inc/Render/ShaderInterop.h"


float3 Unproject(float2 screen_pos, float depth, float4x4 inv_vp)
{
    screen_pos.y = 1.0f - screen_pos.y;
    float4 clip_pos = float4(2.0f * screen_pos - 1.0f, depth, 1.0f);
    float4 world_pos = mul(inv_vp, clip_pos);
    world_pos /= world_pos.w;
    return world_pos.xyz;
}

RaytracingAccelerationStructure Scene : register(t0, space0);
StructuredBuffer<float3> VertexBuffer : register(t1);
StructuredBuffer<float3> NormalBuffer : register(t2);
StructuredBuffer<uint3> IndexBuffer : register(t3);
struct InstanceGeometryDesc
{
    uint triangle_offset;
    uint vertex_offset;
};
StructuredBuffer<InstanceGeometryDesc> InstanceGeometryBuffer : register(t4);

ConstantBuffer<RayGenConstantBuffer> g_rayGenCB : register(b0);
ConstantBuffer<CBufferPerSceneData> g_perSceneData : register(b1);
ConstantBuffer<CBufferPerCameraData> g_perCamData : register(b2);

RWTexture2D<float4> RenderTarget : register(u0);

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
    float4 color;
};

struct ShadowPayload
{
    uint visible;
};

bool IsInsideViewport(float2 p, Viewport viewport)
{
    return (p.x >= viewport.left && p.x <= viewport.right)
        && (p.y >= viewport.top && p.y <= viewport.bottom);
}

float Hash11(float value)
{
    return frac(sin(value) * 43758.5453123f);
}

float3 RandomColorFromIndex(uint index)
{
    float seed = (float)index;
    return float3(
        Hash11(seed * 12.9898f + 78.233f),
        Hash11(seed * 39.3468f + 11.135f),
        Hash11(seed * 73.1569f + 53.573f));
}

[shader("raygeneration")]
void MyRaygenShader()
{
    float2 uv = (float2)DispatchRaysIndex() / (float2)DispatchRaysDimensions();
    float3 ray_origin = g_perCamData._CameraPos.xyz;
    float3 ray_dir = normalize(Unproject(uv,1.0f,g_perCamData._MatrixIVP) - ray_origin);

    // Trace the ray.
    // Set the ray's extents.
    RayDesc ray;
    ray.Origin = ray_origin;
    ray.Direction = ray_dir;
    // Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
    // TMin should be kept small to prevent missing geometry at close contact areas.
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { float4(0, 0, 0, 0) };
    TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);
    RenderTarget[DispatchRaysIndex().xy] = payload.color;
    // float2 lerpValues = (float2)DispatchRaysIndex() / (float2)DispatchRaysDimensions();

    // // Orthographic projection since we're raytracing in screen space.
    // float3 rayDir = float3(0, 0, 1);
    // float3 origin = float3(
    //     lerp(g_rayGenCB.viewport.left, g_rayGenCB.viewport.right, lerpValues.x),
    //     lerp(g_rayGenCB.viewport.top, g_rayGenCB.viewport.bottom, lerpValues.y),
    //     0.0f);

    // if (IsInsideViewport(origin.xy, g_rayGenCB.stencil))
    // {
    //     // Trace the ray.
    //     // Set the ray's extents.
    //     RayDesc ray;
    //     ray.Origin = origin;
    //     ray.Direction = rayDir;
    //     // Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
    //     // TMin should be kept small to prevent missing geometry at close contact areas.
    //     ray.TMin = 0.001;
    //     ray.TMax = 10000.0;
    //     RayPayload payload = { float4(0, 0, 0, 0) };
    //     TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

    //     // Write the raytraced color to the output texture.
    //     RenderTarget[DispatchRaysIndex().xy] = payload.color;
    // }
    // else
    // {
    //     // Render interpolated DispatchRaysIndex outside the stencil window
    //     RenderTarget[DispatchRaysIndex().xy] = float4(lerpValues, 0, 1);
    // }
}

float3 EvaluateDirectionalLight(float3 normal, float3 tint)
{
    float lambert = saturate(dot(normal, normalize(float3(0.7, 0.7, 0.7))));
    return tint * lambert;
}

float TraceShadowRay(float3 hit_position, float3 normal)
{
    float3 light_direction = normalize(float3(0.7, 0.7, 0.7));

    RayDesc shadow_ray;
    shadow_ray.Origin = hit_position + normal * 0.01f;
    shadow_ray.Direction = light_direction;
    shadow_ray.TMin = 0.001f;
    shadow_ray.TMax = 10000.0f;

    ShadowPayload shadow_payload;
    shadow_payload.visible = 0u;

    TraceRay(
        Scene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        ~0,
        0,
        1,
        1,
        shadow_ray,
        shadow_payload);

    return shadow_payload.visible ? 1.0f : 0.15f;
}

float3 LoadInstanceNormal(in MyAttributes attr)
{
    InstanceGeometryDesc instance_desc = InstanceGeometryBuffer[InstanceID()];
    uint tri_idx = instance_desc.triangle_offset + PrimitiveIndex();
    float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    uint3 tri_indices = IndexBuffer[tri_idx] + instance_desc.vertex_offset;
    return normalize(
        NormalBuffer[tri_indices.x] * barycentrics.x +
        NormalBuffer[tri_indices.y] * barycentrics.y +
        NormalBuffer[tri_indices.z] * barycentrics.z);
}

[shader("closesthit")]
void MyClosestHitShader0(inout RayPayload payload, in MyAttributes attr)
{
    float3 normal = LoadInstanceNormal(attr);
    float3 hit_position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float shadow = TraceShadowRay(hit_position, normal);
    payload.color = float4(EvaluateDirectionalLight(normal, float3(1.0f, 0.35f, 0.2f)) * shadow, 1.0f);
}

[shader("closesthit")]
void MyClosestHitShader1(inout RayPayload payload, in MyAttributes attr)
{
    float3 normal = LoadInstanceNormal(attr);
    float3 hit_position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float shadow = TraceShadowRay(hit_position, normal);
    payload.color = float4(EvaluateDirectionalLight(normal, float3(0.2f, 0.55f, 1.0f)) * shadow, 1.0f);
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    payload.color = float4(0, 0, 0, 1);
}

[shader("miss")]
void MyShadowMissShader(inout ShadowPayload payload)
{
    payload.visible = 1u;
}

#endif // RAYTRACING_HLSL