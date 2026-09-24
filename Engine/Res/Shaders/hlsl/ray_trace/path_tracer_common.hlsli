#ifndef PATH_TRACER_COMMON_HLSLI
#define PATH_TRACER_COMMON_HLSLI

#include "rt_common.hlsli"

static const uint kPathHitMiss = 0u;
static const uint kPathHitSurface = 1u;
static const uint kPathHitLight = 2u;
static const float kPathMaxRayT = 10000.0f;

static const uint kMaxPathDepth = 3u;

struct PathHit
{
    uint _type;
    uint _front_face;
    uint _primitive_index;
    uint _triangle_index;
    uint _material_index;
    uint _light_index;
    float2 _barycentrics;
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
    bool _is_delta;
    // 这一步走的是折射（穿透表面）还是反射。只有折射会改变所在介质，
    // 体积吸收据此判断光线是进入玻璃还是回到空气。
    bool _is_transmission;
    float _pdf;
    float3 _ray_origin;
    float3 _ray_direction;
    float3 _throughput_weight;
};

#endif // PATH_TRACER_COMMON_HLSLI
