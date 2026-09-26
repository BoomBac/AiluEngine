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
