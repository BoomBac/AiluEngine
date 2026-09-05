#pragma kernel Skinning

#include "cs_common.hlsli"
#include "../bindless.hlsli"

struct SkinningJobData
{
    uint _position_srv;
    uint _normal_srv;
    uint _tangent_srv;
    uint _bone_index_srv;
    uint _bone_weight_srv;
    uint _position_uav;
    uint _normal_uav;
    uint _tangent_uav;
    uint _palette_offset;
    uint _vertex_count;
    uint _vertex_offset;
};

StructuredBuffer<SkinningJobData> _skinning_jobs;
StructuredBuffer<float4x4> _bone_palette;

float3 SkinPosition(float3 position, uint4 indices, float4 weights, uint palette_offset)
{
    float3 result = 0.0;
    float valid_weight = 0.0;
    for (uint influence = 0u; influence < 4u; ++influence)
    {
        if (weights[influence] <= 0.0 || indices[influence] >= 65536u)
            continue;
        result += weights[influence] * mul(_bone_palette[palette_offset + indices[influence]], float4(position, 1.0)).xyz;
        valid_weight += weights[influence];
    }
    return valid_weight > 0.00001 ? result : position;
}

float3 SkinDirection(float3 direction, uint4 indices, float4 weights, uint palette_offset)
{
    float3 result = 0.0;
    float valid_weight = 0.0;
    for (uint influence = 0u; influence < 4u; ++influence)
    {
        if (weights[influence] <= 0.0 || indices[influence] >= 65536u)
            continue;
        result += weights[influence] * mul(_bone_palette[palette_offset + indices[influence]], float4(direction, 0.0)).xyz;
        valid_weight += weights[influence];
    }
    return valid_weight > 0.00001 && dot(result, result) > 0.0000001 ? normalize(result) : direction;
}

[numthreads(64, 1, 1)]
void Skinning(CSInput input)
{
    const uint job_index = input.DispatchThreadID.y;
    const uint vertex_index = input.DispatchThreadID.x;
    const SkinningJobData job = _skinning_jobs[job_index];
    if (vertex_index >= job._vertex_count)
        return;

    const uint position_offset = (job._vertex_offset + vertex_index) * 12u;
    const uint bone_index_offset = vertex_index * 16u;
    const uint bone_weight_offset = vertex_index * 16u;
    const float3 position = BINDLESS_BUFFER_LOAD(float3, job._position_srv, position_offset);
    const uint4 bone_indices = BINDLESS_BUFFER_LOAD(uint4, job._bone_index_srv, bone_index_offset);
    const float4 bone_weights = BINDLESS_BUFFER_LOAD(float4, job._bone_weight_srv, bone_weight_offset);
    const float3 skinned_position = SkinPosition(position, bone_indices, bone_weights, job._palette_offset);
    BINDLESS_RWBUFFER_STORE_OFFSET(float3, job._position_uav, vertex_index * 12u, skinned_position);

    if (valid_bindless_buffer(job._normal_srv) && valid_bindless_buffer(job._normal_uav))
    {
        const float3 normal = BINDLESS_BUFFER_LOAD(float3, job._normal_srv, position_offset);
        const float3 skinned_normal = SkinDirection(normal, bone_indices, bone_weights, job._palette_offset);
        BINDLESS_RWBUFFER_STORE_OFFSET(float3, job._normal_uav, vertex_index * 12u, skinned_normal);
    }
    if (valid_bindless_buffer(job._tangent_srv) && valid_bindless_buffer(job._tangent_uav))
    {
        const float4 tangent = BINDLESS_BUFFER_LOAD(float4, job._tangent_srv, vertex_index * 16u);
        const float3 skinned_tangent = SkinDirection(tangent.xyz, bone_indices, bone_weights, job._palette_offset);
        BINDLESS_RWBUFFER_STORE_OFFSET(float4, job._tangent_uav, vertex_index * 16u,
                                       float4(skinned_tangent, tangent.w));
    }
}
