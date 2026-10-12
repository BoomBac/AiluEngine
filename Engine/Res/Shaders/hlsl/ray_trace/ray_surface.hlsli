#ifndef AIL_RAY_SURFACE_HLSLI
#define AIL_RAY_SURFACE_HLSLI

#include "ray_query.hlsli"
#include "../bindless.hlsli"
#include "../geometry.hlsli"
#include "gpu_scene.hlsli"

struct TraceSurface
{
    float3 _world_position;
    float3 _geometric_normal;
    float3 _shading_normal;
    float2 _uv;
    uint _primitive_index;
    uint _material_index;
    uint _front_face;
};

float3 LoadTraceFloat3(ByteAddressBuffer buffer, uint byte_offset)
{
    return asfloat(buffer.Load3(byte_offset));
}

float2 LoadTraceFloat2(ByteAddressBuffer buffer, uint byte_offset)
{
    return asfloat(buffer.Load2(byte_offset));
}

bool LoadTraceTriangle(PrimitiveData primitive, uint triangle_index, out TriangleData tri)
{
    tri = (TriangleData)0;
    if (triangle_index >= primitive._submesh_triangle_count ||
        !valid_bindless_buffer(primitive._position_bindless_idx) ||
        !valid_bindless_buffer(primitive._index_bindless_idx))
    {
        return false;
    }

    ByteAddressBuffer index_buffer = g_bindless_index_buffer[NonUniformResourceIndex(primitive._index_bindless_idx)];
    uint3 indices = index_buffer.Load3((primitive._submesh_triangle_offset + triangle_index) * 12u);
    ByteAddressBuffer position_buffer =
        g_bindless_vertex_buffer[NonUniformResourceIndex(primitive._position_bindless_idx)];
    tri.v0 = LoadTraceFloat3(position_buffer, indices.x * 12u);
    tri.v1 = LoadTraceFloat3(position_buffer, indices.y * 12u);
    tri.v2 = LoadTraceFloat3(position_buffer, indices.z * 12u);

    if (valid_bindless_buffer(primitive._normal_bindless_idx))
    {
        ByteAddressBuffer normal_buffer = g_bindless_vertex_buffer[NonUniformResourceIndex(primitive._normal_bindless_idx)];
        tri.n0 = LoadTraceFloat3(normal_buffer, indices.x * 12u);
        tri.n1 = LoadTraceFloat3(normal_buffer, indices.y * 12u);
        tri.n2 = LoadTraceFloat3(normal_buffer, indices.z * 12u);
    }
    else
    {
        float3 normal = normalize(cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
        tri.n0 = normal;
        tri.n1 = normal;
        tri.n2 = normal;
    }

    if (valid_bindless_buffer(primitive._uv_bindless_idx))
    {
        ByteAddressBuffer uv_buffer = g_bindless_vertex_buffer[NonUniformResourceIndex(primitive._uv_bindless_idx)];
        tri.uv0 = LoadTraceFloat2(uv_buffer, indices.x * 8u);
        tri.uv1 = LoadTraceFloat2(uv_buffer, indices.y * 8u);
        tri.uv2 = LoadTraceFloat2(uv_buffer, indices.z * 8u);
    }
    return true;
}

bool LoadTraceSurface(TraceHit hit, out TraceSurface surface)
{
    surface = (TraceSurface)0;
    if (hit._hit == 0u)
        return false;

    PrimitiveData primitive = g_primitive_data[hit._primitive_index];
    TriangleData tri;
    if (!LoadTraceTriangle(primitive, hit._triangle_index, tri))
        return false;

    float3 bary = float3(1.0 - hit._barycentrics.x - hit._barycentrics.y,
                         hit._barycentrics.x, hit._barycentrics.y);
    float3 local_position = tri.v0 * bary.x + tri.v1 * bary.y + tri.v2 * bary.z;
    float3 local_geometric_normal = normalize(cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
    float3 local_shading_normal = normalize(tri.n0 * bary.x + tri.n1 * bary.y + tri.n2 * bary.z);
    float3x3 normal_matrix = transpose((float3x3)primitive._world_to_local);
    float3 world_geometric_normal = normalize(mul(normal_matrix, local_geometric_normal));
    float3 world_shading_normal = normalize(mul(normal_matrix, local_shading_normal));
    if (hit._front_face == 0u)
    {
        world_geometric_normal = -world_geometric_normal;
        world_shading_normal = -world_shading_normal;
    }

    surface._world_position = mul(primitive._local_to_world, float4(local_position, 1.0)).xyz;
    surface._geometric_normal = world_geometric_normal;
    surface._shading_normal = world_shading_normal;
    surface._uv = tri.uv0 * bary.x + tri.uv1 * bary.y + tri.uv2 * bary.z;
    surface._primitive_index = hit._primitive_index;
    surface._material_index = primitive._material_id;
    surface._front_face = hit._front_face;
    return true;
}

#endif // AIL_RAY_SURFACE_HLSLI
