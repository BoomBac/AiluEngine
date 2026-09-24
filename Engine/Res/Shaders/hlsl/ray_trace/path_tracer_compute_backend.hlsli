#ifndef PATH_TRACER_COMPUTE_BACKEND_HLSLI
#define PATH_TRACER_COMPUTE_BACKEND_HLSLI

#include "path_tracer_common.hlsli"
#include "path_tracer_material.hlsli"
#include "path_tracer_manifold.hlsli"

PathHit MakeComputePathMiss()
{
    PathHit hit = (PathHit)0;
    hit._type = kPathHitMiss;
    hit._hit_t = kPathMaxRayT;
    return hit;
}

PathHit MakeComputePathLightHit(float3 ray_origin, float3 ray_direction, float light_t, uint light_index,
                                LightSample light_sample)
{
    PathHit hit = (PathHit)0;
    hit._type = kPathHitLight;
    hit._light_index = light_index;
    hit._hit_t = light_t;
    hit._light_pdf = light_sample._pdf;
    hit._world_position = ray_origin + light_t * ray_direction;
    hit._geometric_normal = light_sample._normal;
    hit._shading_normal = light_sample._normal;
    hit._front_face = dot(ray_direction, light_sample._normal) < 0.0f ? 1u : 0u;
    hit._light_radiance = light_sample._radiance;
    return hit;
}

PathHit MakeComputePathSurfaceHit(TraceSurface surface, TraceHit trace_hit, float3 ray_direction)
{
    PathHit hit = (PathHit)0;
    hit._type = kPathHitSurface;
    hit._hit_t = trace_hit._t;
    hit._primitive_index = trace_hit._primitive_index;
    hit._triangle_index = trace_hit._triangle_index;
    hit._barycentrics = trace_hit._barycentrics;
    hit._material_index = surface._material_index;
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
        return MakeComputePathLightHit(ray_origin, ray_direction, light_t, light_index, light_sample);
    if (!hit_geometry)
        return MakeComputePathMiss();
    return MakeComputePathSurfaceHit(trace_surface, trace_hit, ray_direction);
}

bool LoadPathTriangle(uint primitive_index, uint triangle_index, out ManifoldTriangle triangle_data)
{
    triangle_data = (ManifoldTriangle)0;
    PrimitiveData primitive = g_primitive_data[primitive_index];
    TriangleData source_triangle;
    if (!LoadTraceTriangle(primitive, triangle_index, source_triangle))
        return false;
    triangle_data._v0 = mul(primitive._local_to_world, float4(source_triangle.v0, 1.0f)).xyz;
    triangle_data._v1 = mul(primitive._local_to_world, float4(source_triangle.v1, 1.0f)).xyz;
    triangle_data._v2 = mul(primitive._local_to_world, float4(source_triangle.v2, 1.0f)).xyz;
    return true;
}

PathHit TracePathSegment(float3 ray_origin, float3 ray_direction, float max_t)
{
    PathHit miss = MakeComputePathMiss();
    TraceRayDesc trace_ray = (TraceRayDesc)0;
    trace_ray._origin = ray_origin;
    trace_ray._direction = ray_direction;
    trace_ray._t_min = 0.001f;
    trace_ray._t_max = max_t;
    trace_ray._mask = 0xffu;
    TraceHit trace_hit;
    if (!TraceClosest(trace_ray, trace_hit))
        return miss;
    const MaterialData material_data = g_material_buffer[g_primitive_data[trace_hit._primitive_index]._material_id];
    if (!IsPathMaterialTransmissive(material_data))
    {
        miss._type = kPathHitSurface;
        return miss;
    }
    TraceSurface trace_surface;
    if (!LoadTraceSurface(trace_hit, trace_surface))
    {
        miss._type = kPathHitSurface;
        return miss;
    }
    return MakeComputePathSurfaceHit(trace_surface, trace_hit, ray_direction);
}

bool IsPathOccluded(float3 ray_origin, float3 ray_direction, float max_t)
{
    TraceRayDesc trace_ray = (TraceRayDesc)0;
    trace_ray._origin = ray_origin;
    trace_ray._direction = ray_direction;
    trace_ray._t_min = 0.001f;
    trace_ray._t_max = max_t;
    trace_ray._mask = 0xffu;
    TraceHit trace_hit;
    return TraceClosest(trace_ray, trace_hit);
}

#endif // PATH_TRACER_COMPUTE_BACKEND_HLSLI
