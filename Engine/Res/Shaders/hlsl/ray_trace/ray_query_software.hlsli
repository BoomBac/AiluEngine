#ifndef AIL_RAY_QUERY_SOFTWARE_HLSLI
#define AIL_RAY_QUERY_SOFTWARE_HLSLI

#ifndef RAYTRACE_GI_HIT_USE_BINDLESS_MESH_BUFFERS
#define RAYTRACE_GI_HIT_USE_BINDLESS_MESH_BUFFERS 1
#endif

#include "hit.hlsli"

bool TraceClosest(TraceRayDesc ray, out TraceHit hit)
{
    hit = (TraceHit)0;
    if (ray._t_max <= ray._t_min || dot(ray._direction, ray._direction) <= 0.0)
        return false;

    // The software walker currently accepts an origin-shifted ray. This preserves world-space t
    // while keeping the existing BVH traversal unchanged until its explicit t-min support lands.
    float3 shifted_origin = ray._origin + ray._direction * ray._t_min;
    float shifted_t_max = ray._t_max - ray._t_min;
    HitRecord record = (HitRecord)0;
    record.t = shifted_t_max;
    TriangleHitCandidate candidate;
    uint primitive_index;
    PrimitiveData primitive;
    if (!TraverseSceneClosest(shifted_origin, ray._direction, record, candidate, primitive_index, primitive))
        return false;

    hit._hit = 1u;
    hit._primitive_index = primitive_index;
    hit._triangle_index = (uint)candidate.tri_idx;
    hit._t = candidate.world_t + ray._t_min;
    hit._barycentrics = candidate.bary;

    TriangleData tri;
    if (LoadTraversalTriangleFromOriginalMesh(primitive, candidate.tri_idx, tri))
    {
        float3 p0 = mul(primitive._local_to_world, float4(tri.v0, 1.0)).xyz;
        float3 p1 = mul(primitive._local_to_world, float4(tri.v1, 1.0)).xyz;
        float3 p2 = mul(primitive._local_to_world, float4(tri.v2, 1.0)).xyz;
        hit._front_face = dot(ray._direction, normalize(cross(p1 - p0, p2 - p0))) < 0.0 ? 1u : 0u;
    }
    return true;
}

bool TraceAny(TraceRayDesc ray)
{
    if (ray._t_max <= ray._t_min || dot(ray._direction, ray._direction) <= 0.0)
        return false;

    // See TraceClosest for why the software backend shifts the origin for t-min.
    return TraverseSceneAny(ray._origin + ray._direction * ray._t_min, ray._direction,
                            ray._t_max - ray._t_min);
}

#endif // AIL_RAY_QUERY_SOFTWARE_HLSLI
