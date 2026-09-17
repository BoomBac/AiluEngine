#ifndef PATH_TRACER_COMMON_HLSLI
#define PATH_TRACER_COMMON_HLSLI

#include "rt_common.hlsli"

static const uint kPathHitMiss = 0u;
static const uint kPathHitSurface = 1u;
static const uint kPathHitLight = 2u;
static const float kPathMaxRayT = 10000.0f;

struct PathHit
{
    uint _type;
    uint _front_face;
    float _hit_t;
    float _light_pdf;
    float3 _world_position;
    float3 _shading_normal;
    float3 _geometric_normal;
    Material _material;
    float3 _light_radiance;
};

struct PathBounceSample
{
    bool _valid;
    bool _carry_mis;
    float _pdf;
    float3 _ray_origin;
    float3 _ray_direction;
    float3 _throughput_weight;
};

#endif // PATH_TRACER_COMMON_HLSLI
