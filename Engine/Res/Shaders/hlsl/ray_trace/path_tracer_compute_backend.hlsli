#ifndef PATH_TRACER_COMPUTE_BACKEND_HLSLI
#define PATH_TRACER_COMPUTE_BACKEND_HLSLI

#include "path_tracer_common.hlsli"
#include "path_tracer_material.hlsli"

PathHit MakeComputePathMiss()
{
    PathHit hit = (PathHit)0;
    hit._type = kPathHitMiss;
    hit._hit_t = kPathMaxRayT;
    return hit;
}

PathHit MakeComputePathLightHit(float3 ray_origin, float3 ray_direction, float light_t, LightSample light_sample)
{
    PathHit hit = (PathHit)0;
    hit._type = kPathHitLight;
    hit._hit_t = light_t;
    hit._light_pdf = light_sample._pdf;
    hit._world_position = ray_origin + light_t * ray_direction;
    hit._geometric_normal = light_sample._normal;
    hit._shading_normal = light_sample._normal;
    hit._front_face = dot(ray_direction, light_sample._normal) < 0.0f ? 1u : 0u;
    hit._light_radiance = light_sample._radiance;
    return hit;
}

PathHit MakeComputePathSurfaceHit(TraceSurface surface, float hit_t, float3 ray_direction)
{
    PathHit hit = (PathHit)0;
    hit._type = kPathHitSurface;
    hit._hit_t = hit_t;
    hit._world_position = surface._world_position;
    hit._geometric_normal = surface._geometric_normal;
    hit._shading_normal = surface._shading_normal;
    hit._front_face = dot(ray_direction, surface._geometric_normal) < 0.0f ? 1u : 0u;
    hit._front_face = surface._front_face;
    hit._material = CreatePathMaterial(g_material_buffer[surface._material_index], surface._uv);
    return hit;
}

PathHit TracePathRay(float3 ray_origin, float3 ray_direction)
{
    float geometry_t = kPathMaxRayT;
    TraceRayDesc trace_ray = (TraceRayDesc)0;
    trace_ray._origin = ray_origin;
    trace_ray._direction = ray_direction;
    trace_ray._t_min = 0.001f;
    trace_ray._t_max = geometry_t;
    trace_ray._mask = 0xffu;

    TraceHit trace_hit;
    bool hit_geometry = TraceClosest(trace_ray, trace_hit);
    TraceSurface trace_surface;
    hit_geometry = hit_geometry && LoadTraceSurface(trace_hit, trace_surface);
    if (hit_geometry)
        geometry_t = trace_hit._t;

    uint light_index = 0u;
    float light_t = geometry_t;
    LightSample light_sample = (LightSample)0;
    bool hit_light = FindClosestUnifiedLightHit(_light_count, ray_origin, ray_direction, geometry_t,
                                                light_index, light_t, light_sample);
    if (hit_light && light_t < geometry_t)
        return MakeComputePathLightHit(ray_origin, ray_direction, light_t, light_sample);
    if (!hit_geometry)
        return MakeComputePathMiss();
    return MakeComputePathSurfaceHit(trace_surface, trace_hit._t, ray_direction);
}

bool IsPathOccluded(float3 ray_origin, float3 ray_direction, float max_t)
{
    float remaining_t = max_t;
    float3 current_origin = ray_origin;

    [loop]
    for (uint step = 0u; step < 8u; ++step)
    {
        TraceRayDesc trace_ray = (TraceRayDesc)0;
        trace_ray._origin = current_origin;
        trace_ray._direction = ray_direction;
        trace_ray._t_min = 0.001f;
        trace_ray._t_max = remaining_t;
        trace_ray._mask = 0xffu;

        TraceHit trace_hit;
        if (!TraceClosest(trace_ray, trace_hit))
            return false;

        TraceSurface trace_surface;
        if (!LoadTraceSurface(trace_hit, trace_surface))
            return false;
        Material material = CreatePathMaterial(g_material_buffer[trace_surface._material_index], trace_surface._uv);
        if (!material._is_glass)
            return true;

        float adaptive_offset = max(0.001f, trace_hit._t * 1e-4f);
        current_origin = OffsetPathRayOrigin(trace_surface._world_position, trace_surface._geometric_normal,
                                             ray_direction, trace_hit._t);
        remaining_t -= trace_hit._t + adaptive_offset;
        if (remaining_t <= 0.0f)
            return false;
    }

    return false;
}

#endif // PATH_TRACER_COMPUTE_BACKEND_HLSLI
