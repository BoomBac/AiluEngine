#ifndef PATH_TRACER_DXR_BACKEND_HLSLI
#define PATH_TRACER_DXR_BACKEND_HLSLI

#include "path_tracer_common.hlsli"
#include "path_tracer_material.hlsli"
#include "path_tracer_light_common.hlsli"
#include "path_tracer_manifold.hlsli"

typedef BuiltInTriangleIntersectionAttributes MyAttributes;

struct RayPayload
{
    uint _hit;
    uint _front_face;
    float _hit_t;
    uint _primitive_index;
    uint _triangle_index;
    float2 _barycentrics;
};

struct ShadowPayload
{
    uint _visible;
};

float3 TracePath(uint2 pixel, float3 ray_origin, float3 ray_direction, uint max_depth,
                 inout RandomCtx random_context);

bool LoadDxrPathTriangle(PrimitiveData primitive, uint triangle_index, out TriangleData triangle_data)
{
    triangle_data = (TriangleData)0;
    if (!valid_bindless_buffer(primitive._position_bindless_idx) ||
        !valid_bindless_buffer(primitive._index_bindless_idx))
        return false;

    ByteAddressBuffer index_buffer = g_bindless_index_buffer[primitive._index_bindless_idx];
    uint3 indices = index_buffer.Load3((primitive._submesh_triangle_offset + triangle_index) * 12u);
    ByteAddressBuffer position_buffer = g_bindless_vertex_buffer[primitive._position_bindless_idx];
    triangle_data.v0 = LoadBindlessFloat3(position_buffer, indices.x * 12u);
    triangle_data.v1 = LoadBindlessFloat3(position_buffer, indices.y * 12u);
    triangle_data.v2 = LoadBindlessFloat3(position_buffer, indices.z * 12u);

    if (valid_bindless_buffer(primitive._normal_bindless_idx))
    {
        ByteAddressBuffer normal_buffer = g_bindless_vertex_buffer[primitive._normal_bindless_idx];
        triangle_data.n0 = LoadBindlessFloat3(normal_buffer, indices.x * 12u);
        triangle_data.n1 = LoadBindlessFloat3(normal_buffer, indices.y * 12u);
        triangle_data.n2 = LoadBindlessFloat3(normal_buffer, indices.z * 12u);
    }
    else
    {
        float3 normal = normalize(cross(triangle_data.v1 - triangle_data.v0, triangle_data.v2 - triangle_data.v0));
        triangle_data.n0 = normal;
        triangle_data.n1 = normal;
        triangle_data.n2 = normal;
    }

    if (valid_bindless_buffer(primitive._uv_bindless_idx))
    {
        ByteAddressBuffer uv_buffer = g_bindless_vertex_buffer[primitive._uv_bindless_idx];
        triangle_data.uv0 = LoadBindlessFloat2(uv_buffer, indices.x * 8u);
        triangle_data.uv1 = LoadBindlessFloat2(uv_buffer, indices.y * 8u);
        triangle_data.uv2 = LoadBindlessFloat2(uv_buffer, indices.z * 8u);
    }
    return true;
}

PathHit MakeDxrPathMiss()
{
    PathHit hit = (PathHit)0;
    hit._type = kPathHitMiss;
    hit._hit_t = kPathMaxRayT;
    return hit;
}

PathHit MakeDxrPathLightHit(float3 ray_origin, float3 ray_direction, float light_t, uint light_index,
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

PathHit MakeDxrPathSurfaceHit(RayPayload payload, float3 ray_direction)
{
    PathHit hit = (PathHit)0;
    if (payload._hit == 0u)
        return MakeDxrPathMiss();

    PrimitiveData primitive = g_primitive_data[payload._primitive_index];
    TriangleData triangle_data;
    if (!LoadDxrPathTriangle(primitive, payload._triangle_index, triangle_data))
        return MakeDxrPathMiss();

    float3 barycentrics = float3(1.0f - payload._barycentrics.x - payload._barycentrics.y,
                                 payload._barycentrics.x, payload._barycentrics.y);
    float3 local_position = triangle_data.v0 * barycentrics.x + triangle_data.v1 * barycentrics.y +
                            triangle_data.v2 * barycentrics.z;
    float3 local_geometric_normal = normalize(cross(triangle_data.v1 - triangle_data.v0,
                                                    triangle_data.v2 - triangle_data.v0));
    float3 local_shading_normal = normalize(triangle_data.n0 * barycentrics.x + triangle_data.n1 * barycentrics.y +
                                            triangle_data.n2 * barycentrics.z);
    float3x3 normal_matrix = transpose((float3x3)primitive._world_to_local);
    float3 geometric_normal = normalize(mul(normal_matrix, local_geometric_normal));
    float3 shading_normal = normalize(mul(normal_matrix, local_shading_normal));
    bool front_face = payload._front_face != 0u;
    if (!front_face)
    {
        geometric_normal = -geometric_normal;
        shading_normal = -shading_normal;
    }

    hit._type = kPathHitSurface;
    hit._front_face = front_face ? 1u : 0u;
    hit._primitive_index = payload._primitive_index;
    hit._triangle_index = payload._triangle_index;
    hit._material_index = primitive._material_id;
    hit._barycentrics = payload._barycentrics;
    hit._hit_t = payload._hit_t;
    hit._world_position = mul(primitive._local_to_world, float4(local_position, 1.0f)).xyz;
    hit._geometric_normal = geometric_normal;
    hit._shading_normal = shading_normal;
    float2 uv = triangle_data.uv0 * barycentrics.x + triangle_data.uv1 * barycentrics.y +
                triangle_data.uv2 * barycentrics.z;
    hit._material = CreatePathMaterial(g_material_data[primitive._material_id], uv);
    return hit;
}

bool LoadPathTriangle(uint primitive_index, uint triangle_index, out ManifoldTriangle triangle_data)
{
    triangle_data = (ManifoldTriangle)0;
    TriangleData source_triangle;
    if (!LoadDxrPathTriangle(g_primitive_data[primitive_index], triangle_index, source_triangle))
        return false;
    const PrimitiveData primitive = g_primitive_data[primitive_index];
    triangle_data._v0 = mul(primitive._local_to_world, float4(source_triangle.v0, 1.0f)).xyz;
    triangle_data._v1 = mul(primitive._local_to_world, float4(source_triangle.v1, 1.0f)).xyz;
    triangle_data._v2 = mul(primitive._local_to_world, float4(source_triangle.v2, 1.0f)).xyz;
    return true;
}

PathHit TracePathSegment(float3 ray_origin, float3 ray_direction, float max_t)
{
    RayDesc ray;
    ray.Origin = ray_origin;
    ray.Direction = ray_direction;
    ray.TMin = 0.001f;
    ray.TMax = max_t;
    RayPayload payload = (RayPayload)0;
    TraceRay(Scene, RAY_FLAG_NONE, ~0u, 0, 1, 0, ray, payload);
    if (payload._hit == 0u)
        return MakeDxrPathMiss();
    const PrimitiveData primitive = g_primitive_data[payload._primitive_index];
    if (!IsPathMaterialTransmissive(g_material_data[primitive._material_id]))
    {
        PathHit blocker = (PathHit)0;
        blocker._type = kPathHitSurface;
        return blocker;
    }
    PathHit hit = MakeDxrPathSurfaceHit(payload, ray_direction);
    if (payload._hit != 0u && hit._type == kPathHitMiss)
        hit._type = kPathHitSurface;
    return hit;
}

PathHit TracePathRay(float3 ray_origin, float3 ray_direction)
{
    RayDesc ray;
    ray.Origin = ray_origin;
    ray.Direction = ray_direction;
    ray.TMin = 0.001f;
    ray.TMax = kPathMaxRayT;

    RayPayload payload = (RayPayload)0;
    TraceRay(Scene, RAY_FLAG_NONE, ~0u, 0, 1, 0, ray, payload);

    float max_light_t = payload._hit != 0u ? payload._hit_t : kPathMaxRayT;
    uint light_index = 0u;
    float light_t = max_light_t;
    LightSample light_sample = (LightSample)0;
    bool hit_light = FindClosestUnifiedLightHit(GetUnifiedLightCount(), ray_origin, ray_direction, max_light_t,
                                                light_index, light_t, light_sample);
    if (hit_light && (payload._hit == 0u || light_t < payload._hit_t))
        return MakeDxrPathLightHit(ray_origin, ray_direction, light_t, light_index, light_sample);
    if (payload._hit == 0u)
        return MakeDxrPathMiss();
    return MakeDxrPathSurfaceHit(payload, ray_direction);
}

bool IsPathOccluded(float3 ray_origin, float3 ray_direction, float max_t)
{
    RayDesc ray;
    ray.Origin = ray_origin;
    ray.Direction = ray_direction;
    ray.TMin = 0.001f;
    ray.TMax = max_t;
    RayPayload payload = (RayPayload)0;
    TraceRay(Scene, RAY_FLAG_NONE, ~0u, 0, 1, 0, ray, payload);
    return payload._hit != 0u;
}

#include "path_tracer_core.hlsli"

void WriteDxrPathHitPayload(inout RayPayload payload, in MyAttributes attributes)
{
    PrimitiveData primitive = g_primitive_data[InstanceID()];
    TriangleData triangle_data;
    bool front_face = true;
    if (LoadDxrPathTriangle(primitive, PrimitiveIndex(), triangle_data))
    {
        float3 local_geometric_normal = normalize(cross(triangle_data.v1 - triangle_data.v0,
                                                        triangle_data.v2 - triangle_data.v0));
        float3x3 normal_matrix = transpose((float3x3)primitive._world_to_local);
        float3 geometric_normal = normalize(mul(normal_matrix, local_geometric_normal));
        front_face = dot(WorldRayDirection(), geometric_normal) < 0.0f;
    }
    payload._hit = 1u;
    payload._front_face = front_face ? 1u : 0u;
    payload._hit_t = RayTCurrent();
    payload._primitive_index = InstanceID();
    payload._triangle_index = PrimitiveIndex();
    payload._barycentrics = attributes.barycentrics;
}

[shader("raygeneration")]
void MyRaygenShader()
{
    uint2 pixel = DispatchRaysIndex().xy;
    float2 uv = (float2(pixel) + 0.5f) / (float2)DispatchRaysDimensions();
    float3 ray_origin = g_perCamData._CameraPos.xyz;
    float3 ray_direction = normalize(RayUnproject(uv, 1.0f, g_perCamData._MatrixIVP) - ray_origin);

    RandomCtx random_context;
    InitSeed(random_context, pixel, g_perSceneData._FrameIndex);
    float3 radiance = TracePath(pixel, ray_origin, ray_direction, kMaxPathDepth, random_context);
    RenderTarget[pixel] = float4(radiance, 1.0f);
}

[shader("closesthit")]
void MyClosestHitShader0(inout RayPayload payload, in MyAttributes attributes)
{
    WriteDxrPathHitPayload(payload, attributes);
}

[shader("closesthit")]
void MyClosestHitShader1(inout RayPayload payload, in MyAttributes attributes)
{
    WriteDxrPathHitPayload(payload, attributes);
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    payload._hit = 0u;
}

[shader("miss")]
void MyShadowMissShader(inout ShadowPayload payload)
{
    payload._visible = 1u;
}

#endif // PATH_TRACER_DXR_BACKEND_HLSLI
