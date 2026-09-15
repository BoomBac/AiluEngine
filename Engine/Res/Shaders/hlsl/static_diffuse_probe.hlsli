#ifndef __STATIC_DIFFUSE_PROBE_HLSLI__
#define __STATIC_DIFFUSE_PROBE_HLSLI__

StructuredBuffer<StaticDiffuseProbeGroupData> g_static_diffuse_probe_groups : register(t20);
StructuredBuffer<StaticDiffuseProbeSHData> g_static_diffuse_probe_data : register(t21);

uint FlattenStaticDiffuseProbeIndex(StaticDiffuseProbeGroupData group, uint3 coordinate)
{
    return group._probe_offset + coordinate.x + group._probe_count_x *
           (coordinate.y + group._probe_count_y * coordinate.z);
}

float3 SampleStaticDiffuseProbeSH(StaticDiffuseProbeGroupData group, uint3 coordinate, float weight, float3 normal_ws)
{
    // 探针组的 probe_offset 由 CPU 侧预算裁剪，这里再兜一层边界，
    // 避免结构化缓冲越界读触发 device removal。
    uint probe_count, probe_stride;
    g_static_diffuse_probe_data.GetDimensions(probe_count, probe_stride);
    const uint probe_index = FlattenStaticDiffuseProbeIndex(group, coordinate);
    if (probe_index >= probe_count)
        return 0.0.xxx;
    return EvaluateSH9(g_static_diffuse_probe_data[probe_index]._sh, normal_ws) * weight;
}

bool SampleStaticDiffuseProbeIrradiance(float3 world_position, float3 normal_ws, out float3 irradiance)
{
    irradiance = 0.0.xxx;
    uint group_count, group_stride;
    g_static_diffuse_probe_groups.GetDimensions(group_count, group_stride);
    uint selected_group_index = UINT32_MAX;
    float selected_volume = 3.402823466e+38f;
    for (uint group_index = 0u; group_index < group_count; ++group_index)
    {
        const StaticDiffuseProbeGroupData group = g_static_diffuse_probe_groups[group_index];
        if (group._probe_count_x == 0u || group._probe_count_y == 0u || group._probe_count_z == 0u)
            continue;
        const float3 local_position = world_position - group._position;
        const float3 half_size = group._size * 0.5f;
        if (any(abs(local_position) > half_size))
            continue;
        const float volume = group._size.x * group._size.y * group._size.z;
        if (volume < selected_volume)
        {
            selected_group_index = group_index;
            selected_volume = volume;
        }
    }
    if (selected_group_index == UINT32_MAX)
        return false;

    const StaticDiffuseProbeGroupData group = g_static_diffuse_probe_groups[selected_group_index];
    const uint3 probe_count = uint3(group._probe_count_x, group._probe_count_y, group._probe_count_z);
    float3 grid_coordinate = 0.0.xxx;
    if (abs(group._spacing.x) > 1e-6f)
        grid_coordinate.x = (world_position.x - group._position.x - group._origin.x) / group._spacing.x;
    if (abs(group._spacing.y) > 1e-6f)
        grid_coordinate.y = (world_position.y - group._position.y - group._origin.y) / group._spacing.y;
    if (abs(group._spacing.z) > 1e-6f)
        grid_coordinate.z = (world_position.z - group._position.z - group._origin.z) / group._spacing.z;
    const float3 grid_position = clamp(grid_coordinate, 0.0.xxx, float3(probe_count - 1u));
    const uint3 base = uint3(floor(grid_position));
    const float3 fraction = grid_position - base;
    [unroll]
    for (uint z = 0u; z <= 1u; ++z)
        [unroll]
        for (uint y = 0u; y <= 1u; ++y)
            [unroll]
            for (uint x = 0u; x <= 1u; ++x)
            {
                const uint3 coordinate = min(base + uint3(x, y, z), probe_count - 1u);
                const float weight = (x != 0u ? fraction.x : 1.0f - fraction.x) *
                                     (y != 0u ? fraction.y : 1.0f - fraction.y) *
                                     (z != 0u ? fraction.z : 1.0f - fraction.z);
                irradiance += SampleStaticDiffuseProbeSH(group, coordinate, weight, normal_ws);
            }
    return true;
}

#endif // __STATIC_DIFFUSE_PROBE_HLSLI__
