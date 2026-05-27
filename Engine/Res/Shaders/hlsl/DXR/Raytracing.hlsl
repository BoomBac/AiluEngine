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

#define AL_SHADER_INTEROP_CBUFFER_AS_STRUCT 1
#define DXR

//#include "RaytracingDef.hlsli"
#if defined(AL_PACKAGE_SHADER_INTEROP)
#include "../../ShaderInterop.h"
#else
#include "../../../../Inc/Render/ShaderInterop.h"
#endif
#include "../ray_trace/rt_common.hlsli"
#include "../ray_trace/sampling.hlsli"
#include "../ray_trace/hit.hlsli"
#include "../sampler.hlsli"
#include "../bindless.hlsli"

#ifndef RAY_FLAG_NONE
#define RAY_FLAG_NONE 0x0
#endif
#ifndef RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
#define RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH 0x4
#endif
#ifndef RAY_FLAG_SKIP_CLOSEST_HIT_SHADER
#define RAY_FLAG_SKIP_CLOSEST_HIT_SHADER 0x8
#endif

static const float kMinRayT = 0.001f;
static const float kMaxRayT = 10000.0f;
static const uint kMaxPathDepth = 1u;


float3 RayUnproject(float2 screen_pos, float depth, float4x4 inv_vp)
{
    screen_pos.y = 1.0f - screen_pos.y;
    float4 clip_pos = float4(2.0f * screen_pos - 1.0f, depth, 1.0f);
    float4 world_pos = mul(inv_vp, clip_pos);
    world_pos /= world_pos.w;
    return world_pos.xyz;
}

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

#include "../ray_trace/path_tracer_light_common.hlsli"

typedef BuiltInTriangleIntersectionAttributes MyAttributes;

struct SurfaceMaterialPayload
{
    float3 albedo;
    float roughness;
    float metallic;
    float anisotropy;
    float ior;
    float transmission;
    float3 emission;
    uint is_glass;
};


struct RayPayload
{
    uint hit;
    uint front_face;
    float hit_t;
    float3 world_pos;
    float3 shading_normal;
    float3 miss_radiance;
    SurfaceMaterialPayload material;
};

struct ShadowPayload
{
    uint visible;
};

bool IsMaterialTransmissive(MaterialData mat_data);
Material UnpackMaterial(SurfaceMaterialPayload payload);
TraceContext CreateTraceContext(Material mat, bool front_face);
float3 EvaluateDirectLighting(TraceContext trace_ctx, float3 world_pos, float3 normal, float3 wo, inout RandomCtx ctx);
bool IsOccluded(float3 origin, float3 dir, float max_t);
bool SampleNextBounce(
    TraceContext trace_ctx,
    float3 world_pos,
    float3 normal,
    float3 wo,
    float hit_t,
    uint depth,
    inout RandomCtx ctx,
    inout float3 throughput,
    out float next_bsdf_pdf,
    out bool carry_bsdf_mis,
    out float3 next_origin,
    out float3 next_dir);


[shader("raygeneration")]
void MyRaygenShader()
{
    float2 uv = (float2)DispatchRaysIndex() / (float2)DispatchRaysDimensions();
    float3 ray_origin = g_perCamData._CameraPos.xyz;
    float3 ray_dir = normalize(RayUnproject(uv, 1.0f, g_perCamData._MatrixIVP) - ray_origin);
    float3 radiance = 0.0.xxx;
    float3 throughput = 1.0.xxx;
    float prev_bsdf_pdf = 0.0f;
    bool has_prev_bsdf_sample = false;

    [loop]
    for (uint depth = 0; depth < kMaxPathDepth; ++depth)
    {
        RandomCtx ctx;
        InitSeed(ctx, DispatchRaysIndex().xy, g_perSceneData._FrameIndex, depth);

        RayDesc ray;
        ray.Origin = ray_origin;
        ray.Direction = ray_dir;
        ray.TMin = kMinRayT;
        ray.TMax = kMaxRayT;

        RayPayload payload = (RayPayload)0;
        TraceRay(Scene, RAY_FLAG_NONE, ~0, 0, 1, 0, ray, payload);

        uint light_index = 0u;
        float light_t = payload.hit != 0u ? payload.hit_t : kMaxRayT;
        LightSample light_sample = (LightSample)0;
        bool hit_sampled_light = FindClosestUnifiedLightHit(GetUnifiedLightCount(), ray_origin, ray_dir, light_t, light_index, light_t, light_sample);

        if (hit_sampled_light && (payload.hit == 0u || light_t < payload.hit_t))
        {
            float mis_weight = has_prev_bsdf_sample ? PowerHeuristic(prev_bsdf_pdf, light_sample._pdf) : 1.0f;
            radiance += throughput * light_sample._radiance * mis_weight;
            break;
        }

        if (payload.hit == 0u)
        {
            radiance += throughput * payload.miss_radiance;
            break;
        }

        Material mat = UnpackMaterial(payload.material);
        bool front_face = payload.front_face != 0u;
        float3 wo = normalize(-ray_dir);
        TraceContext trace_ctx = CreateTraceContext(mat, front_face);

        radiance += throughput * mat._emission;
        radiance += throughput * EvaluateDirectLighting(trace_ctx, payload.world_pos, payload.shading_normal, wo, ctx);

        float3 next_origin;
        float3 next_dir;
        float next_bsdf_pdf = 0.0f;
        bool carry_bsdf_mis = false;
        if (!SampleNextBounce(trace_ctx, payload.world_pos, payload.shading_normal, wo, payload.hit_t, depth, ctx, throughput, next_bsdf_pdf, carry_bsdf_mis, next_origin, next_dir))
            break;

        prev_bsdf_pdf = next_bsdf_pdf;
        has_prev_bsdf_sample = carry_bsdf_mis;

        ray_origin = next_origin;
        ray_dir = next_dir;
    }

    RenderTarget[DispatchRaysIndex().xy] = float4(radiance, 1.0f);
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

uint3 GetTriangleIndices(in MyAttributes attr)
{
    ObjectInstanceData instance_desc = g_instance_data[InstanceID()];
    ByteAddressBuffer index_buffer = g_bindless_index_buffer[instance_desc._index_bindless_idx];
    uint tri_idx = instance_desc._submesh_triangle_offset + PrimitiveIndex();
    return index_buffer.Load3(tri_idx * 12u);
}

float4 LoadBindlessFloat4(ByteAddressBuffer buffer, uint byte_offset)
{
    return asfloat(buffer.Load4(byte_offset));
}

float3 LoadInstancePosition(in MyAttributes attr)
{
    ObjectInstanceData instance_desc = g_instance_data[InstanceID()];
    uint3 tri_indices = GetTriangleIndices(attr);
    ByteAddressBuffer position_buffer = g_bindless_vertex_buffer[instance_desc._position_bindless_idx];
    float3 v0 = LoadBindlessFloat3(position_buffer, tri_indices.x * 12u);
    float3 v1 = LoadBindlessFloat3(position_buffer, tri_indices.y * 12u);
    float3 v2 = LoadBindlessFloat3(position_buffer, tri_indices.z * 12u);
    float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    return v0 * barycentrics.x + v1 * barycentrics.y + v2 * barycentrics.z;
}

float3 LoadInstanceNormal(in MyAttributes attr)
{
    ObjectInstanceData instance_desc = g_instance_data[InstanceID()];
    float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    uint3 tri_indices = GetTriangleIndices(attr);
    ByteAddressBuffer normal_buffer = g_bindless_vertex_buffer[instance_desc._normal_bindless_idx];
    return LoadBindlessFloat3(normal_buffer, tri_indices.x * 12u) * barycentrics.x +
                 LoadBindlessFloat3(normal_buffer, tri_indices.y * 12u) * barycentrics.y +
                 LoadBindlessFloat3(normal_buffer, tri_indices.z * 12u) * barycentrics.z;
}

float2 LoadInstanceUV(in MyAttributes attr)
{
    ObjectInstanceData instance_desc = g_instance_data[InstanceID()];
    float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    uint3 tri_indices = GetTriangleIndices(attr);
    ByteAddressBuffer uv_buffer = g_bindless_vertex_buffer[instance_desc._uv_bindless_idx];
    float2 uv = LoadBindlessFloat2(uv_buffer, tri_indices.x * 8u) * barycentrics.x +
                 LoadBindlessFloat2(uv_buffer, tri_indices.y * 8u) * barycentrics.y +
                 LoadBindlessFloat2(uv_buffer, tri_indices.z * 8u) * barycentrics.z;
    return uv;
}

float3 LoadInstanceTangent(in MyAttributes attr)
{
    ObjectInstanceData instance_desc = g_instance_data[InstanceID()];
    float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    uint3 tri_indices = GetTriangleIndices(attr);
    ByteAddressBuffer tangent_buffer = g_bindless_vertex_buffer[instance_desc._tangent_bindless_idx];
    float4 t1 = LoadBindlessFloat4(tangent_buffer, tri_indices.x * 16u);
    t1.xyz *= t1.w;
    float4 t2 = LoadBindlessFloat4(tangent_buffer, tri_indices.y * 16u);
    t2.xyz *= t2.w;
    float4 t3 = LoadBindlessFloat4(tangent_buffer, tri_indices.z * 16u);
    t3.xyz *= t3.w;
    float3 tangent = t1.xyz * barycentrics.x +
                     t2.xyz * barycentrics.y +
                     t3.xyz * barycentrics.z;
    return tangent;
}

float4 SampleTex2DLinearClamp(uint handle, float2 uv)
{
    if (valid_bindless_handle(handle))
    {
        return SAMPLE_TEXTURE2D_LOD(g_bindless_texture2d[handle], g_LinearClampSampler, uv,0);
    }
    else
    {
        return float4(1, 1, 1, 1);
    }
}

float4 SampleTex2DLinearWrap(uint handle, float2 uv)
{
    if (valid_bindless_handle(handle))
    {
        return SAMPLE_TEXTURE2D_LOD(g_bindless_texture2d[handle], g_LinearWrapSampler, uv,0);
    }
    else
    {
        return float4(1, 1, 1, 1);
    }
}

float3 GetHitPosition()
{
    return WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
}

SurfaceMaterialPayload PackMaterial(Material mat)
{
    SurfaceMaterialPayload payload = (SurfaceMaterialPayload)0;
    payload.albedo = mat._albedo;
    payload.roughness = mat._roughness;
    payload.metallic = mat._metallic;
    payload.anisotropy = mat._anisotropy;
    payload.ior = mat._ior;
    payload.transmission = mat._transmission;
    payload.emission = mat._emission;
    payload.is_glass = mat._is_glass ? 1u : 0u;
    return payload;
}

Material UnpackMaterial(SurfaceMaterialPayload payload)
{
    Material mat = (Material)0;
    mat._albedo = payload.albedo;
    mat._roughness = payload.roughness;
    mat._metallic = payload.metallic;
    mat._anisotropy = payload.anisotropy;
    mat._ior = payload.ior;
    mat._transmission = payload.transmission;
    mat._emission = payload.emission;
    mat._is_glass = payload.is_glass != 0u;
    return mat;
}

TraceContext CreateTraceContext(Material mat, bool front_face)
{
    TraceContext trace_ctx = (TraceContext)0;
    trace_ctx._mat = mat;
    trace_ctx._ior_i = front_face ? 1.0f : mat._ior;
    trace_ctx._ior_t = front_face ? mat._ior : 1.0f;
    trace_ctx._eta = trace_ctx._ior_i / max(trace_ctx._ior_t, 1e-6f);
    return trace_ctx;
}

float3 OffsetRayOrigin(float3 p, float3 n, float3 dir, float hit_t)
{
    float offset = max(1e-4f, hit_t * 1e-4f);
    return p + (dot(dir, n) > 0 ? n : -n) * offset;
}

Material CreateMaterial(MaterialData mat_data, float2 uv)
{
    Material mat = (Material)0;
    mat._albedo = mat_data._base_color.rgb * SampleTex2DLinearClamp(mat_data._base_color_tex, uv).rgb;
    mat._roughness = mat_data._roughness;
    mat._metallic = mat_data._metallic;
    mat._anisotropy = mat_data._anisotropy;
    mat._ior = max(mat_data._ior, 1.0001f);
    mat._transmission = mat_data._transmission;
    mat._emission = mat_data._emission * 10;// * mat_data._emission_strength;
    mat._emission *= SampleTex2DLinearClamp(mat_data._emission_tex, uv).rgb;
    mat._is_glass = IsMaterialTransmissive(mat_data);
    return mat;
}

float3 CreateGrassAlbedo(MaterialData mat_data, float2 uv, float3 world_pos)
{
    float2 uv10 = uv * 10;
    float2 uv100 = uv10 * 10;
    float3 c0 = lerp(0.4, 1, SampleTex2DLinearWrap(mat_data._base_color_tex, uv10).b).rrr;
    float4 t1 = SampleTex2DLinearWrap(mat_data._base_color_tex, uv100);
    float camera_fadeout = saturate(1 - saturate(distance(g_perCamData._CameraPos.xyz, world_pos) / 10));
    float c1 = 1 - (lerp(0.4, 1, t1.g) * t1.r * camera_fadeout);
    return c0 * c1.xxx;
}

Material CreateGrassMaterial(MaterialData mat_data, float2 uv, float3 world_pos)
{
    Material mat = (Material)0;
    float3 albedo = CreateGrassAlbedo(mat_data, uv, world_pos);
    mat._albedo = albedo;
    mat._roughness = lerp(0, 1, albedo.r);
    mat._metallic = 0.0f;
    mat._anisotropy = 0.0f;
    mat._ior = 1.5f;
    mat._transmission = 0.0f;
    mat._emission = 0.0.xxx;
    mat._is_glass = false;
    return mat;
}

bool IsOccluded(float3 origin, float3 dir, float max_t)
{
    float remaining_t = max_t;
    float3 ray_origin = origin;

    [loop]
    for (uint step = 0u; step < 8u; ++step)
    {
        RayDesc ray;
        ray.Origin = ray_origin;
        ray.Direction = dir;
        ray.TMin = kMinRayT;
        ray.TMax = remaining_t;

        RayPayload payload = (RayPayload)0;
        TraceRay(Scene, RAY_FLAG_NONE, ~0, 0, 1, 0, ray, payload);
        if (payload.hit == 0u)
            return false;

        Material mat = UnpackMaterial(payload.material);
        if (!mat._is_glass)
            return true;

        float adaptive_offset = max(0.001f, payload.hit_t * 1e-4f);
        ray_origin = payload.world_pos + dir * adaptive_offset;
        remaining_t -= payload.hit_t + adaptive_offset;
        if (remaining_t <= 0.0f)
            return false;
    }

    return false;
}

float3 EvaluateDirectLighting(TraceContext trace_ctx, float3 world_pos, float3 normal, float3 wo, inout RandomCtx ctx)
{
    uint light_count = GetUnifiedLightCount();
    if (light_count == 0u)
        return 0.0.xxx;

    uint sampled_light_idx = min((uint)(rand(ctx) * light_count), light_count - 1u);
    LightSample light_sample = SampleUnifiedLight(sampled_light_idx, world_pos, normal, ctx);
    light_sample._pdf *= GetUnifiedLightSelectionPMF(GetUnifiedLightCount());

    float cos_theta = DirectLightCosTheta(trace_ctx._mat, normal, light_sample._wi);
    if (light_sample._pdf <= 1e-6f || cos_theta <= 1e-6f || any(light_sample._radiance > 0.0.xxx) == false)
        return 0.0.xxx;

    float3 shadow_origin = OffsetRayOrigin(world_pos, normal, light_sample._wi, 1.0f);
    if (IsOccluded(shadow_origin, light_sample._wi, GetLightShadowRayTMax(light_sample)))
        return 0.0.xxx;

    float pdf_brdf = 0.0f;
    float3 h = normalize(light_sample._wi + wo);
    float3 f = EvalBRDF(trace_ctx, normal, h, wo, light_sample._wi, pdf_brdf);
    float mis_weight = PowerHeuristic(light_sample._pdf, pdf_brdf);
    //return light_sample._radiance * f * cos_theta * (mis_weight / light_sample._pdf);
    return light_sample._radiance * f * cos_theta / (light_sample._pdf);
}

bool SampleNextBounce(
    TraceContext trace_ctx,
    float3 world_pos,
    float3 normal,
    float3 wo,
    float hit_t,
    uint depth,
    inout RandomCtx ctx,
    inout float3 throughput,
    out float next_bsdf_pdf,
    out bool carry_bsdf_mis,
    out float3 next_origin,
    out float3 next_dir)
{
    next_bsdf_pdf = 0.0f;
    carry_bsdf_mis = false;
    next_origin = 0.0.xxx;
    next_dir = 0.0.xxx;

    float3 randv = float3(rand(ctx), rand(ctx), rand(ctx));
    SampleBRDFResult sample_result = SampleBRDF(trace_ctx, normal, wo, randv.x, randv.y, randv.z);
    if (all(sample_result.wi == 0.0.xxx))
        return false;

    float pdf = 0.0f;
    float3 f = EvalBRDF(trace_ctx, normal, sample_result.h, wo, sample_result.wi, pdf);
    float cos_theta = abs(dot(normal, sample_result.wi));
    if (pdf <= 1e-6f || cos_theta <= 1e-6f || all(f == 0.0.xxx))
        return false;

    throughput *= f * (cos_theta / pdf);
    next_bsdf_pdf = pdf;
    carry_bsdf_mis = !trace_ctx._mat._is_glass;

    if (depth > 2u)
    {
        float rr_prob = saturate(max(throughput.r, max(throughput.g, throughput.b)));
        if (rand(ctx) > rr_prob)
            return false;

        throughput /= max(rr_prob, 1e-6f);
    }

    next_dir = sample_result.wi;
    next_origin = OffsetRayOrigin(world_pos, normal, next_dir, hit_t);
    return true;
}

bool IsMaterialTransmissive(MaterialData mat_data)
{
    return mat_data._transmission > 0.0 && mat_data._metallic < 1.0;
}
//返回指向光源的入射方向wi，pdf为该方向的概率密度
float3 SampleAreaLight(ShaderArealLightData light,float3 x,float3 n,RandomCtx ctx,out float pdf)
{
    float2 u = rand2(ctx);
    float3 light_u = light._points[1].xyz - light._points[0].xyz;
    float3 light_v = light._points[3].xyz - light._points[0].xyz;
    float3 light_normal = -normalize(cross(light_u, light_v));
    float3 light_pos = light._points[0].xyz + 0.5 * light_u + 0.5 * light_v; // 面光源中心点
    float3 y =light_pos+ (u.x - 0.5f) * light_u+ (u.y - 0.5f) * light_v;
    float3 wi = y - x;
    float r2 = dot(wi, wi);
    float r = sqrt(r2);
    wi = normalize(wi);
    //wi /= r;
    float cos_theta_l = dot(light_normal, -wi);
    if (light._is_twosided != 0)
        cos_theta_l = abs(cos_theta_l);
    if (cos_theta_l <= 0)
    {
        pdf = 0;
        return 0;
    }
    float area = length(cross(light_u, light_v));
    pdf = r2 / (area * cos_theta_l);
    return wi;
}

void LoadBaseSurfaceData(
    in MyAttributes attr,
    out ObjectInstanceData inst_data,
    out MaterialData mat_data,
    out float2 uv,
    out float3 world_pos,
    out float3 geometric_normal,
    out bool front_face)
{
    inst_data = g_instance_data[InstanceID()];
    float3x3 normal_matrix = transpose((float3x3)inst_data._world_to_local);
    geometric_normal = normalize(mul(normal_matrix, LoadInstanceNormal(attr)));
    uv = LoadInstanceUV(attr);
    world_pos = GetHitPosition();
    mat_data = g_material_data[inst_data._material_id];
    front_face = dot(WorldRayDirection(), geometric_normal) < 0.0f;
}

float3 ComputeGrassNormal(in MyAttributes attr, ObjectInstanceData inst_data, MaterialData mat_data, float2 uv, float3 world_pos)
{
    float3 tangent_local = normalize(LoadInstanceTangent(attr));
    float3 normal_local = normalize(LoadInstanceNormal(attr));
    float3x3 world_matrix = (float3x3)inst_data._local_to_world;
    float3x3 normal_matrix = transpose((float3x3)inst_data._world_to_local);
    float3 N = normalize(mul(normal_matrix, normal_local));
    float3 T = normalize(mul(world_matrix, tangent_local));
    T = normalize(T - N * dot(T, N));
    float3 B = normalize(cross(N, T));

    float2 uv100 = uv * 100;
    float3 tangent_normal = SampleTex2DLinearWrap(mat_data._normal_tex, uv100 * 2).xyz * 2.0 - 1.0;
    tangent_normal *= float3(0.3, 0.3, 1);
    tangent_normal = normalize(tangent_normal);

    float camera_fadeout = saturate(1 - saturate(distance(g_perCamData._CameraPos.xyz, world_pos) / 10));
    float3x3 btn = float3x3(T, B, N);
    float3 world_normal = normalize(mul(tangent_normal, btn));
    return normalize(lerp(N, world_normal, camera_fadeout));
}

void WriteSurfaceHitPayload(inout RayPayload payload, Material mat, float3 world_pos, float3 shading_normal, bool front_face)
{
    payload.hit = 1u;
    payload.front_face = front_face ? 1u : 0u;
    payload.hit_t = RayTCurrent();
    payload.world_pos = world_pos;
    payload.shading_normal = shading_normal;
    payload.material = PackMaterial(mat);
}


[shader("closesthit")]
void MyClosestHitShader0(inout RayPayload payload, in MyAttributes attr)
{
    ObjectInstanceData inst_data;
    MaterialData mat_data;
    float2 uv;
    float3 world_pos;
    float3 geometric_normal;
    bool front_face;
    LoadBaseSurfaceData(attr, inst_data, mat_data, uv, world_pos, geometric_normal, front_face);
    Material mat = CreateMaterial(mat_data, uv);
    float3 shading_normal = front_face ? geometric_normal : -geometric_normal;
    WriteSurfaceHitPayload(payload, mat, world_pos, shading_normal, front_face);
}

[shader("closesthit")]
void MyClosestHitShader1(inout RayPayload payload, in MyAttributes attr)
{
    ObjectInstanceData inst_data;
    MaterialData mat_data;
    float2 uv;
    float3 world_pos;
    float3 geometric_normal;
    bool front_face;
    LoadBaseSurfaceData(attr, inst_data, mat_data, uv, world_pos, geometric_normal, front_face);

    float3 wnormal = ComputeGrassNormal(attr, inst_data, mat_data, uv, world_pos);
    Material mat = CreateGrassMaterial(mat_data, uv, world_pos);
    float3 shading_normal = front_face ? wnormal : -wnormal;
    WriteSurfaceHitPayload(payload, mat, world_pos, shading_normal, front_face);
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    float3 dir    = WorldRayDirection();
    payload.hit = 0u;
    payload.miss_radiance = ProceduralSky(dir);
}

[shader("miss")]
void MyShadowMissShader(inout ShadowPayload payload)
{
    payload.visible = 1u;
}

#endif // RAYTRACING_HLSL