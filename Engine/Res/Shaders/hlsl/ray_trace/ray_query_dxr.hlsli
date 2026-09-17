#ifndef AIL_RAY_QUERY_DXR_HLSLI
#define AIL_RAY_QUERY_DXR_HLSLI

#include "gpu_scene.hlsli"

RaytracingAccelerationStructure g_ray_tracing_scene : register(t0);

bool TraceClosest(TraceRayDesc ray, out TraceHit hit)
{
    hit = (TraceHit)0;
    if (ray._t_max <= ray._t_min || dot(ray._direction, ray._direction) <= 0.0)
        return false;

    RayDesc ray_desc;
    ray_desc.Origin = ray._origin;
    ray_desc.Direction = ray._direction;
    ray_desc.TMin = ray._t_min;
    ray_desc.TMax = ray._t_max;

    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(g_ray_tracing_scene, RAY_FLAG_NONE, ray._mask, ray_desc);
    while (query.Proceed())
    {
    }

    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return false;

    hit._hit = 1u;
    hit._primitive_index = query.CommittedInstanceID();
    hit._triangle_index = query.CommittedPrimitiveIndex();
    hit._front_face = query.CommittedTriangleFrontFace() ? 1u : 0u;
    hit._t = query.CommittedRayT();
    hit._barycentrics = query.CommittedTriangleBarycentrics();
    return true;
}

bool TraceAny(TraceRayDesc ray)
{
    if (ray._t_max <= ray._t_min || dot(ray._direction, ray._direction) <= 0.0)
        return false;

    RayDesc ray_desc;
    ray_desc.Origin = ray._origin;
    ray_desc.Direction = ray._direction;
    ray_desc.TMin = ray._t_min;
    ray_desc.TMax = ray._t_max;

    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
             RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(g_ray_tracing_scene, RAY_FLAG_NONE, ray._mask, ray_desc);
    while (query.Proceed())
    {
    }
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

#endif // AIL_RAY_QUERY_DXR_HLSLI
