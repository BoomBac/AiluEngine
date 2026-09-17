#pragma kernel ProbeRelocation

#include "ddgi_common.hlsli"

StructuredBuffer<DDGIProbeRayData> _probe_ray_data;
RWStructuredBuffer<DDGIProbeData> _probe_data;

CBUFFER_START(ComputeCB)
    float4x4 _volume_world_to_local;
    float4x4 _volume_local_to_world;
    float4 _probe_spacing;
    uint _probe_count_x;
    uint _probe_count_y;
    uint _probe_count_z;
    float _max_ray_distance;
    float _backface_threshold;
    float _min_frontface_distance_ratio;
    uint2 _ddgi_padding;
CBUFFER_END

float3 DDGIWorldDisplacementToLocal(float3 world_direction, float move_distance)
{
    const float3 world_displacement = world_direction * move_distance;
    return mul((float3x3)_volume_world_to_local, world_displacement);
}

float3 DDGIGetRelocationRayDirection(uint ray_index)
{
    const float3x3 identity = float3x3(
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f);
    return DDGIGetProbeRayDirection(ray_index, identity, 1u);
}

float3 DDGIClampRelocationOffset(float3 current_offset, float3 candidate_offset, float3 probe_spacing)
{
    const float3 normalized_offset = candidate_offset / max(abs(probe_spacing), 1e-4f.xxx);
    if (dot(normalized_offset, normalized_offset) < kMaxRelocationOffsetFraction *
                                                     kMaxRelocationOffsetFraction)
    {
        return candidate_offset;
    }
    return current_offset;
}

static const float kRelocationHistoryResetRatio = 0.02f;

[numthreads(32, 1, 1)]
void ProbeRelocation(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    const uint3 probe_count = uint3(_probe_count_x, _probe_count_y, _probe_count_z);
    const uint probe_count_total = probe_count.x * probe_count.y * probe_count.z;
    const uint probe_index = dispatch_thread_id.x;
    if (probe_index >= probe_count_total)
        return;

    const float max_ray_distance = max(_max_ray_distance, 0.001f);
    const float min_frontface_distance = min(abs(_probe_spacing.x),
        min(abs(_probe_spacing.y), abs(_probe_spacing.z))) * saturate(_min_frontface_distance_ratio);
    const float3 current_offset = _probe_data[probe_index]._relocation_offset;

    uint backface_count = 0u;
    bool has_backface = false;
    uint closest_backface_index = 0u;
    float closest_backface_distance = max_ray_distance;
    bool has_frontface = false;
    uint closest_frontface_index = 0u;
    float closest_frontface_distance = max_ray_distance;
    bool has_farthest_frontface = false;
    uint farthest_frontface_index = 0u;
    float farthest_frontface_distance = 0.0f;

    [unroll]
    for (uint ray_index = 0u; ray_index < kRelocationRaysPerProbe; ++ray_index)
    {
        const DDGIProbeRayData ray_data = _probe_ray_data[DDGIRayDataIndex(probe_index, ray_index)];
        const float ray_distance = abs(ray_data._distance);
        const bool is_backface = DDGIIsBackFaceHit(ray_data._distance, max_ray_distance);
        const bool is_frontface = DDGIIsFrontFaceHit(ray_data._distance, max_ray_distance);
        const bool is_miss = DDGIIsMiss(ray_data._distance, max_ray_distance);

        if (is_backface)
        {
            ++backface_count;
            if (!has_backface || ray_distance < closest_backface_distance)
            {
                has_backface = true;
                closest_backface_index = ray_index;
                closest_backface_distance = ray_distance;
            }
        }

        if (is_frontface && (!has_frontface || ray_distance < closest_frontface_distance))
        {
            has_frontface = true;
            closest_frontface_index = ray_index;
            closest_frontface_distance = ray_distance;
        }

        // Misses represent the most open candidate direction, but never the closest surface.
        if ((is_frontface || is_miss) &&
            (!has_farthest_frontface || ray_distance > farthest_frontface_distance))
        {
            has_farthest_frontface = true;
            farthest_frontface_index = ray_index;
            farthest_frontface_distance = ray_distance;
        }
    }

    const float backface_ratio = (float)backface_count / (float)kRelocationRaysPerProbe;
    float3 candidate_offset = current_offset;
    if (has_backface && backface_ratio > saturate(_backface_threshold))
    {
        const float3 world_direction = DDGIGetRelocationRayDirection(closest_backface_index);
        candidate_offset += DDGIWorldDisplacementToLocal(
            world_direction, closest_backface_distance + min_frontface_distance * 0.5f);
    }
    else if (has_frontface && closest_frontface_distance < min_frontface_distance && has_farthest_frontface)
    {
        const float3 closest_direction = DDGIGetRelocationRayDirection(closest_frontface_index);
        const float3 farthest_direction = DDGIGetRelocationRayDirection(farthest_frontface_index);
        if (dot(closest_direction, farthest_direction) <= 0.0f)
        {
            candidate_offset += DDGIWorldDisplacementToLocal(
                farthest_direction, min_frontface_distance - closest_frontface_distance);
        }
    }
    else if (closest_frontface_distance > min_frontface_distance && length(current_offset) > 1e-5f)
    {
        const float move_back_margin = min(closest_frontface_distance - min_frontface_distance,
                                           length(current_offset));
        const float3 local_direction = normalize(-current_offset);
        const float3 world_direction = normalize(mul((float3x3)_volume_local_to_world, local_direction));
        candidate_offset += DDGIWorldDisplacementToLocal(world_direction, move_back_margin);
    }

    DDGIProbeData output = _probe_data[probe_index];
    const float3 next_offset = DDGIClampRelocationOffset(current_offset, candidate_offset, _probe_spacing.xyz);
    const float min_probe_spacing = min(abs(_probe_spacing.x), min(abs(_probe_spacing.y), abs(_probe_spacing.z)));
    const float history_reset_threshold = max(min_probe_spacing * kRelocationHistoryResetRatio, 1e-4f);
    const bool relocation_changed = length(next_offset - current_offset) > history_reset_threshold;
    output._relocation_offset = next_offset;
    if (relocation_changed)
    {
        output._state = kDDGIProbeStateRelocated + kDDGIProbeStateHistoryReset;
    }
    _probe_data[probe_index] = output;
}
