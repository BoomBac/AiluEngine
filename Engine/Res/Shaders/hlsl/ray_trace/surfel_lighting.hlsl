#pragma kernel SurfelLightingUpdateCS
#pragma kernel SurfelLightingResetCS
#pragma multi_compile SurfelLightingUpdateCS AL_RAY_QUERY_DXR_INLINE

#include "../common.hlsli"
#include "../surfel_common.hlsli"
#include "ray_surface.hlsli"
#include "gi_ray_lighting.hlsli"
#include "sampling.hlsli"
#include "path_tracer_environment.hlsli"

StructuredBuffer<SurfelData> _SurfelPool;
StructuredBuffer<uint> _SurfelStateRead;
StructuredBuffer<uint> _ActiveSurfelIndices;
RWStructuredBuffer<SurfelLightingData> _SurfelLighting;
RWStructuredBuffer<uint> _SurfelFrameState;
StructuredBuffer<MaterialData> g_material_buffer;

CBUFFER_START(SurfelLightingCB)
    uint _SurfelFrameIndex;
    uint _LightingUpdateCursor;
    uint _LightingUpdateBudget;
    uint _RaysPerSurfel;
    uint _LightingMaxSurfelCount;
    float _LightingMaxRayDistance;
    uint _UnifiedLightCount;
    uint _LightingPadding;
CBUFFER_END

groupshared float3 s_irradiance[8];

float3 EvaluateSurfelSkyReflection(TraceSurface surface, MaterialData material, uint surfel_index, uint ray_index,
                                  float hit_distance)
{
    if (g_EnvironmentEnabled <= 0.0f || g_EnvironmentIntensity <= 0.0f)
        return 0.0f.xxx;

    const float3 diffuse_albedo = GIReadMaterialAlbedo(material, surface._uv) *
                                 (1.0f - GIReadMaterialMetallic(material, surface._uv));
    if (!any(diffuse_albedo > 0.0f.xxx))
        return 0.0f.xxx;

    RandomCtx random_ctx = (RandomCtx)0;
    InitSeed(random_ctx, uint2(surfel_index ^ 0x68bc21ebu, ray_index), _SurfelFrameIndex);
    const float3 direction = sample_cosine_hemisphere(surface._shading_normal, rand(random_ctx), rand(random_ctx));
    TraceRayDesc ray = (TraceRayDesc)0;
    ray._origin = OffsetRayOrigin(surface._world_position, surface._geometric_normal, direction, hit_distance);
    ray._direction = direction;
    ray._t_min = 0.001f;
    ray._t_max = max(_LightingMaxRayDistance, 0.001f);
    ray._mask = 0xffu;
    // Cosine sampling cancels the Lambert cosine / PI factor. This is reflected sky, not direct IBL.
    return TraceAny(ray) ? 0.0f.xxx : diffuse_albedo * EvaluateEnvironmentRadiance(direction);
}

float HashToUnit(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return (float)(value & 0x00ffffffu) / 16777216.0f;
}

[numthreads(256, 1, 1)]
void SurfelLightingResetCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x < _LightingMaxSurfelCount)
        _SurfelLighting[thread_id.x] = (SurfelLightingData)0;
}

[numthreads(8, 1, 1)]
void SurfelLightingUpdateCS(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID)
{
    const uint ray_index = group_thread_id.x;
    const uint local_index = group_id.x;
    const uint active_count = _SurfelStateRead[kSurfelStateActiveListCount];
    if (local_index == 0u && ray_index == 0u)
    {
        _SurfelFrameState[8u] = min(active_count, _LightingUpdateBudget);
        _SurfelFrameState[9u] = active_count;
        _SurfelFrameState[10u] = _SurfelStateRead[0u];
        _SurfelFrameState[11u] = _SurfelStateRead[1u];
        _SurfelFrameState[12u] = _SurfelStateRead[2u];
        _SurfelFrameState[13u] = _SurfelStateRead[3u];
    }
    if (active_count == 0u || local_index >= _LightingUpdateBudget || local_index >= active_count)
        return;

    const uint surfel_index = _ActiveSurfelIndices[(_LightingUpdateCursor + local_index) % active_count];
    const SurfelData surfel = _SurfelPool[surfel_index];
    SurfelLightingData old_data = _SurfelLighting[surfel_index];
    if ((surfel._flags & kSurfelValid) == 0u || surfel._radius <= 0.0f)
    {
        if (ray_index == 0u)
            InterlockedAdd(_SurfelFrameState[7u], 1u);
        return;
    }

    if ((surfel._flags & kSurfelRetiring) != 0u)
        return;

    const float normal_length_sq = dot(surfel._normal, surfel._normal);
    if (normal_length_sq <= 1e-8f)
    {
        if (ray_index == 0u)
            InterlockedAdd(_SurfelFrameState[7u], 1u);
        return;
    }

    const uint rays_per_surfel = clamp(_RaysPerSurfel, 1u, 8u);
    float3 irradiance_sample = 0.0f.xxx;
    if (ray_index < rays_per_surfel)
    {
        const uint history_samples = (old_data._flags & kSurfelLightingValid) != 0u
            ? old_data._sample_count : 0u;
        const float2 scramble = float2(HashToUnit(surfel_index),
                                       HashToUnit(surfel_index ^ 0x9e3779b9u));
        // R2-style progressive sequence: samples stay ordered across lighting updates.
        const float2 sequence = float2(0.7548776662f, 0.569840296f) *
                                (float)(history_samples + ray_index);
        const float2 xi = frac(sequence + scramble);
        const float3 normal = normalize(surfel._normal);
        const float3 ray_direction = sample_cosine_hemisphere(normal, xi.x, xi.y);
        const float origin_bias = max(0.001f, surfel._radius * 0.02f);

        TraceRayDesc ray;
        ray._origin = surfel._position + normal * origin_bias;
        ray._direction = ray_direction;
        ray._t_min = 0.001f;
        ray._t_max = max(_LightingMaxRayDistance, 0.001f);
        ray._mask = 0xffu;

        // Direct environment illumination is owned by IBL. Surfel stores only light arriving from surfaces.
        float3 incoming_radiance = 0.0f.xxx;
        TraceHit hit;
        if (TraceClosest(ray, hit))
        {
            const float hit_distance = min(max(hit._t, 0.0f), ray._t_max);
            if (hit._front_face == 0u)
            {
                incoming_radiance = 0.0f.xxx;
            }
            else
            {
                TraceSurface surface;
                if (LoadTraceSurface(hit, surface))
                {
                    const MaterialData material = g_material_buffer[surface._material_index];
                    incoming_radiance = GIEvaluateHitRadiance(
                        surface, material, surfel_index, ray_index, _SurfelFrameIndex,
                        _UnifiedLightCount, hit_distance);
                    incoming_radiance += EvaluateSurfelSkyReflection(surface, material, surfel_index, ray_index,
                                                                    hit_distance);
                }
            }
        }
        irradiance_sample = incoming_radiance * PI;
    }

    s_irradiance[ray_index] = irradiance_sample;
    GroupMemoryBarrierWithGroupSync();

    if (ray_index == 0u)
    {
        float3 irradiance_sum = 0.0f.xxx;
        [unroll]
        for (uint i = 0u; i < 8u; ++i)
            irradiance_sum += s_irradiance[i];

        const float3 new_irradiance = irradiance_sum / (float)rays_per_surfel;
        // An inherited estimate is not a traced sample and must not bias the running mean.
        const bool history_valid = old_data._sample_count > 0u;
        const uint history_samples = history_valid ? old_data._sample_count : 0u;
        const uint sample_count = min(history_samples, 0xffffffffu - rays_per_surfel) + rays_per_surfel;
        const float sample_weight = (float)rays_per_surfel / (float)sample_count;
        const float3 sampled_irradiance = history_valid
            ? old_data._sampled_irradiance + (new_irradiance - old_data._sampled_irradiance) * sample_weight
            : new_irradiance;

        old_data._sampled_irradiance = max(sampled_irradiance, 0.0f.xxx);
        const float3 irradiance = (old_data._flags & kSurfelLightingInherited) != 0u
            ? lerp(old_data._inherited_irradiance, old_data._sampled_irradiance,
                   SurfelLightingMaturity(sample_count))
            : old_data._sampled_irradiance;
        old_data._irradiance = max(irradiance, 0.0f.xxx);
        old_data._history_weight = 1.0f - sample_weight;
        old_data._last_update_frame = _SurfelFrameIndex;
        old_data._sample_count = sample_count;
        old_data._flags |= kSurfelLightingValid;
        _SurfelLighting[surfel_index] = old_data;
        InterlockedAdd(_SurfelFrameState[6u], 1u);
    }
}
