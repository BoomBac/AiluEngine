#ifndef AIL_RAY_QUERY_HLSLI
#define AIL_RAY_QUERY_HLSLI

struct TraceRayDesc
{
    float3 _origin;
    float _t_min;
    float3 _direction;
    float _t_max;
    uint _mask;
};

struct TraceHit
{
    uint _hit;
    uint _primitive_index;
    uint _triangle_index;
    uint _front_face;
    float _t;
    float2 _barycentrics;
};

float3 OffsetRayOrigin(float3 world_position, float3 normal, float3 direction, float hit_t)
{
    float offset = max(1e-4, hit_t * 1e-4);
    return world_position + (dot(direction, normal) > 0.0 ? normal : -normal) * offset;
}

#if defined(AL_RAY_QUERY_DXR_INLINE)
#include "ray_query_dxr.hlsli"
#else
#include "ray_query_software.hlsli"
#endif

#endif // AIL_RAY_QUERY_HLSLI
