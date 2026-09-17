//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
//
//*********************************************************

#ifndef RAYTRACING_HLSL
#define RAYTRACING_HLSL

#define AL_SHADER_INTEROP_CBUFFER_AS_STRUCT 1
#define DXR

#if defined(AL_PACKAGE_SHADER_INTEROP)
#include "../../ShaderInterop.h"
#else
#include "../../../../Inc/Render/ShaderInterop.h"
#endif

#include "../ray_trace/rt_common.hlsli"
#include "../ray_trace/sampling.hlsli"
#include "../sampler.hlsli"
#include "../bindless.hlsli"

#ifndef RAY_FLAG_NONE
#define RAY_FLAG_NONE 0x0
#endif

static const uint kMaxPathDepth = 3u;

RaytracingAccelerationStructure Scene : register(t0, space0);
StructuredBuffer<MaterialData> g_material_data : register(t7);

ConstantBuffer<CBufferPerSceneData> g_perSceneData : register(b0);
ConstantBuffer<CBufferPerCameraData> g_perCamData : register(b1);
ConstantBuffer<UnifiedLightBufferConfig> _UnifiedLightConfig : register(b2);

RWTexture2D<float4> RenderTarget : register(u0);

uint GetUnifiedLightCount()
{
    return _UnifiedLightConfig._light_count;
}

float3 RayUnproject(float2 screen_pos, float depth, float4x4 inv_vp)
{
    screen_pos.y = 1.0f - screen_pos.y;
    float4 clip_pos = float4(2.0f * screen_pos - 1.0f, depth, 1.0f);
    float4 world_pos = mul(inv_vp, clip_pos);
    world_pos /= world_pos.w;
    return world_pos.xyz;
}

#include "../ray_trace/path_tracer_material.hlsli"
#include "../ray_trace/path_tracer_environment.hlsli"
#include "../ray_trace/path_tracer_common.hlsli"
#include "../ray_trace/path_tracer_light_common.hlsli"
#include "../ray_trace/path_tracer_dxr_backend.hlsli"
#include "../ray_trace/path_tracer_core.hlsli"

#endif // RAYTRACING_HLSL
