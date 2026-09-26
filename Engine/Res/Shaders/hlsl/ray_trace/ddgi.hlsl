#pragma kernel ProbeTrace
#pragma multi_compile ProbeTrace AL_RAY_QUERY_DXR_INLINE

#include "ddgi_common.hlsli"
#include "ray_surface.hlsli"
#include "gi_ray_lighting.hlsli"

StructuredBuffer<MaterialData> g_material_buffer;
StructuredBuffer<DDGIProbeData> _probe_data;
RWStructuredBuffer<DDGIProbeRayData> _probe_ray_data;

CBUFFER_START(ComputeCB)
    float4x4 _volume_local_to_world;
    float4 _probe_spacing_and_distance;
    uint _probe_count_x;
    uint _probe_count_y;
    uint _probe_count_z;
    uint _rays_per_probe;
    uint _frame_index;
    float4 _environment_radiance_intensity;
    uint _freeze_ray_rotation;
    uint _unified_light_count;
    uint _use_probe_relocation;
    uint _ddgi_padding;
CBUFFER_END

[numthreads(128, 1, 1)]
void ProbeTrace(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID)
{
    const uint3 probe_count = uint3(_probe_count_x, _probe_count_y, _probe_count_z);
    const uint probe_index = group_id.x;
    const uint ray_index = group_thread_id.x;
    const uint probe_count_total = probe_count.x * probe_count.y * probe_count.z;
    if (probe_index >= probe_count_total || ray_index >= kRaysPerProbe)
    {
        return;
    }

    const float max_ray_distance = max(_probe_spacing_and_distance.w, 0.001f);
    const float3 base_local_position = DDGIProbeLocalPosition(probe_index, probe_count,
                                                                _probe_spacing_and_distance.xyz);
    const float3 relocation_offset = _use_probe_relocation != 0u
        ? _probe_data[probe_index]._relocation_offset : 0.0f.xxx;
    const float3 local_position = base_local_position + relocation_offset;
    const float3 probe_position = mul(_volume_local_to_world, float4(local_position, 1.0f)).xyz;
    const float3x3 ray_rotation = DDGIGetRayRotation(_frame_index, _freeze_ray_rotation, probe_index);
    const bool fixed_relocation_ray = DDGIIsFixedRelocationRay(ray_index, _use_probe_relocation);
    const float3 ray_direction = DDGIGetProbeRayDirection(ray_index, ray_rotation, _use_probe_relocation);

    TraceRayDesc ray;
    ray._origin = probe_position;
    ray._direction = ray_direction;
    ray._t_min = 0.001f;
    ray._t_max = max_ray_distance;
    ray._mask = 0xffu;

    DDGIProbeRayData ray_data = (DDGIProbeRayData)0;
    ray_data._radiance = fixed_relocation_ray
        ? 0.0f.xxx : _environment_radiance_intensity.xyz * _environment_radiance_intensity.w;
    ray_data._distance = max_ray_distance;

    TraceHit hit;
    if (TraceClosest(ray, hit))
    {
        const float hit_distance = min(max(hit._t, 0.0f), max_ray_distance);
        if (hit._front_face == 0u)
        {
            ray_data._radiance = 0.0f.xxx;
            ray_data._distance = -hit_distance;
        }
        else
        {
            if (!fixed_relocation_ray)
            {
                TraceSurface surface;
                if (LoadTraceSurface(hit, surface))
                {
                    const MaterialData material = g_material_buffer[surface._material_index];
                    ray_data._radiance = GIEvaluateHitRadiance(surface, material, probe_index, ray_index,
                                                               _frame_index, _unified_light_count, hit_distance);
                }
            }
            ray_data._distance = hit_distance;
        }
    }

    _probe_ray_data[DDGIRayDataIndex(probe_index, ray_index)] = ray_data;
}
