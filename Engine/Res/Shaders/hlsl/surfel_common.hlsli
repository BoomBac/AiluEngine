#ifndef AIL_SURFEL_COMMON_HLSLI
#define AIL_SURFEL_COMMON_HLSLI

struct SurfelData
{
    float3 _position;
    float _radius;
    float3 _normal;
    uint _flags;
    uint _cascade_level;
    uint _last_touched_frame;
    uint _object_id;
    float _retire_start_weight;
    uint2 _spawn_pixel;
};

struct SurfelLightingData
{
    float3 _irradiance;
    float _history_weight;
    uint _last_update_frame;
    uint _sample_count;
    uint _flags;
    uint _padding;
    float3 _sampled_irradiance;
    float _sampled_padding;
    float3 _inherited_irradiance;
    uint _inheritance_depth;
};

float4 SurfelComputeGridInfo(float3 camera_position, uint level, uint grid_resolution, float base_cell_size)
{
    const float cell_size = base_cell_size * exp2((float)level);
    const float3 center = floor(camera_position / cell_size) * cell_size;
    const float world_size = cell_size * (float)grid_resolution;
    return float4(center - world_size * 0.5f, cell_size);
}

float SurfelComputeRawDesiredRadius(float3 world_position, float3 camera_position, float tan_half_fov,
                                    float screen_height, float target_pixel_radius)
{
    const float view_distance = max(distance(camera_position, world_position), 0.001f);
    const float world_size_per_pixel = 2.0f * view_distance * tan_half_fov / max(screen_height, 1.0f);
    return world_size_per_pixel * target_pixel_radius;
}

uint SurfelSelectDesiredCascade(float3 world_position, float raw_radius, float3 camera_position,
                                uint cascade_count, uint grid_resolution, float base_cell_size,
                                float max_radius_scale, out bool is_in_grid)
{
    uint cascade_level = 0u;
    [loop]
    while (cascade_level + 1u < cascade_count)
    {
        const float4 grid_info = SurfelComputeGridInfo(camera_position, cascade_level, grid_resolution,
                                                        base_cell_size);
        const int3 cell = (int3)floor((world_position - grid_info.xyz) / grid_info.w);
        if (raw_radius <= grid_info.w * max_radius_scale && all(cell >= 0) && all(cell < (int)grid_resolution))
            break;
        ++cascade_level;
    }

    const float4 grid_info = SurfelComputeGridInfo(camera_position, cascade_level, grid_resolution, base_cell_size);
    const int3 cell = (int3)floor((world_position - grid_info.xyz) / grid_info.w);
    is_in_grid = all(cell >= 0) && all(cell < (int)grid_resolution);
    return cascade_level;
}

static const uint kSurfelValid = 1u;
static const uint kSurfelNeedsOverlapResolve = 1u << 1u;
static const uint kSurfelMigratedFiner = 1u << 2u;
static const uint kSurfelMigratedCoarser = 1u << 3u;
static const uint kSurfelRetiring = 1u << 4u;
static const uint kSurfelTransitionFrames = 32u;

float SurfelContributionWeight(SurfelData surfel, uint frame_index)
{
    const float age = (float)(frame_index - surfel._last_touched_frame);
    const float blend = smoothstep(0.0f, (float)kSurfelTransitionFrames, age);
    return (surfel._flags & kSurfelRetiring) != 0u
        ? surfel._retire_start_weight * (1.0f - blend) : blend;
}

static const uint kSurfelLightingValid = 1u << 0u;
static const uint kSurfelLightingInherited = 1u << 1u;
static const uint kSurfelMatureSampleCount = 64u;
static const uint kSurfelMaxInheritanceDepth = 4u;

float SurfelLightingMaturity(uint sample_count)
{
    return smoothstep(0.0f, (float)kSurfelMatureSampleCount, (float)sample_count);
}
static const uint kSurfelStateHighWatermark = 0u;
static const uint kSurfelStateActiveListCount = 5u;

#endif // AIL_SURFEL_COMMON_HLSLI
