#pragma kernel ProbeDistance

#include "ddgi_common.hlsli"

StructuredBuffer<DDGIProbeRayData> _probe_ray_data;
RWStructuredBuffer<DDGIProbeData> _probe_data;
Texture2D<float2> _distance_history_texture;
RWTexture2D<float2> _distance_texture;

CBUFFER_START(ComputeCB)
    uint _probe_count_x;
    uint _probe_count_y;
    uint _probe_count_z;
    uint _frame_index;
    uint _freeze_ray_rotation;
    uint _use_temporal;
    uint _history_valid;
    float _hysteresis;
    float _history_step_ratio;
    uint _use_probe_relocation;
    float _max_ray_distance;
    uint2 _ddgi_padding;
CBUFFER_END

[numthreads(14, 14, 1)]
void ProbeDistance(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID)
{
    const uint3 probe_count = uint3(_probe_count_x, _probe_count_y, _probe_count_z);
    const uint probe_count_total = probe_count.x * probe_count.y * probe_count.z;
    const uint probe_index = group_id.x;
    if (probe_index >= probe_count_total)
        return;

    const float2 oct_uv = ((float2)group_thread_id.xy + 0.5f) / (float)kDistanceInteriorResolution;
    const float3 direction = DDGIOctDecode(oct_uv);
    const float3x3 ray_rotation = DDGIGetRayRotation(_frame_index, _freeze_ray_rotation, probe_index);
    const DDGIProbeData probe_data = _probe_data[probe_index];
    const bool reset_history = DDGIProbeNeedsHistoryReset(probe_data._state);

    float distance_sum = 0.0f;
    float distance_squared_sum = 0.0f;
    float weight_sum = 0.0f;
    [unroll]
    for (uint ray_index = 0u; ray_index < kRaysPerProbe; ++ray_index)
    {
        if (DDGIIsFixedRelocationRay(ray_index, _use_probe_relocation))
            continue;
        const DDGIProbeRayData ray_data = _probe_ray_data[DDGIRayDataIndex(probe_index, ray_index)];
        float ray_distance = ray_data._distance;
        // Backface hits still describe a blocker.  Keep them in the moments, but shorten their
        // effective distance so a probe on the opposite side of a thin surface is rejected.
        if (DDGIIsBackFaceHit(ray_distance, _max_ray_distance))
            ray_distance = abs(ray_distance) * kBackfaceDistanceScale;

        const float ray_weight = pow(saturate(dot(direction,
            DDGIGetProbeRayDirection(ray_index, ray_rotation, _use_probe_relocation))), 8.0f);
        distance_sum += ray_distance * ray_weight;
        distance_squared_sum += ray_distance * ray_distance * ray_weight;
        weight_sum += ray_weight;
    }

    const float inv_weight = rcp(max(weight_sum, 1e-5f));
    const float mean_distance = distance_sum * inv_weight;
    const float mean_squared_distance = distance_squared_sum * inv_weight;
    const uint3 probe_coord = DDGIProbeCoord(probe_index, probe_count);
    const uint2 tile_origin = uint2(probe_coord.x, probe_coord.y + probe_coord.z * probe_count.y) *
                              kDistancePhysicalResolution;
    const uint2 atlas_pixel = tile_origin + kDistanceBorderSize + group_thread_id.xy;
    float2 moments = float2(mean_distance, mean_squared_distance);
    if (_use_temporal != 0u && _history_valid != 0u && !reset_history)
    {
        const float2 history = max(_distance_history_texture.Load(int3(atlas_pixel, 0)), 0.0f.xx);
        float2 delta = moments - history;

        // Same slew limit as the radiance update: a ray that hits a small, very bright emitter also skews
        // that texel's distance moments, which feed the visibility weights and make the probe blend
        // flicker.  _history_step_ratio == 0 disables the limit.
        if (_history_step_ratio > 0.0f)
        {
            const float step_limit = _history_step_ratio * max(history.x, 0.0f);
            if (step_limit > 0.0f && abs(delta.x) > step_limit)
                delta *= step_limit / abs(delta.x);
        }

        moments = history + (1.0f - saturate(_hysteresis)) * delta;
    }

    _distance_texture[atlas_pixel] = moments;
    if (group_thread_id.x == 0u && group_thread_id.y == 0u && reset_history)
    {
        DDGIProbeData output = probe_data;
        output._state = DDGIProbeStateWithoutHistoryReset(output._state);
        _probe_data[probe_index] = output;
    }
}
