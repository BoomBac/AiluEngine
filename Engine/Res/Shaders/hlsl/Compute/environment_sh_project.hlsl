#include "../common.hlsli"
#include "../spherical_harmonics.hlsli"

#pragma kernel SHProject
#pragma kernel SHReduce

TextureCube<float4> _Cubemap;
RWStructuredBuffer<float4> _Partial;
RWStructuredBuffer<float4> _Output;

uint _ProjectionSize;
uint _PartialCount;

groupshared float3 g_sh[SH_COEFFICIENT_COUNT][256];

float3 FaceDirection(uint face, float2 uv)
{
    if (face == 0u) return float3(1.0f, -uv.y, -uv.x);
    if (face == 1u) return float3(-1.0f, -uv.y, uv.x);
    if (face == 2u) return float3(uv.x, 1.0f, uv.y);
    if (face == 3u) return float3(uv.x, -1.0f, -uv.y);
    if (face == 4u) return float3(uv.x, -uv.y, 1.0f);
    return float3(-uv.x, -uv.y, -1.0f);
}

[numthreads(16,16,1)]
void SHProject(uint3 dispatch_id : SV_DispatchThreadID, uint3 group_id : SV_GroupID, uint group_index : SV_GroupIndex)
{
    const float2 uv = ((float2(dispatch_id.xy) + 0.5f) / float(_ProjectionSize)) * 2.0f - 1.0f;
    const float3 direction = normalize(FaceDirection(group_id.z, uv));
    const float3 radiance = _Cubemap.SampleLevel(g_LinearClampSampler, direction, 0.0f).rgb;
    const float inv_length = rsqrt(1.0f + dot(uv, uv));
    const float texel_solid_angle = 4.0f * inv_length * inv_length * inv_length /
                                    float(_ProjectionSize * _ProjectionSize);
    float basis[SH_COEFFICIENT_COUNT];
    EvaluateSHBasis9(direction, basis);

    [unroll]
    for (uint coefficient = 0u; coefficient < SH_COEFFICIENT_COUNT; ++coefficient)
        g_sh[coefficient][group_index] = radiance * basis[coefficient] * texel_solid_angle;
    GroupMemoryBarrierWithGroupSync();

    if (group_index == 0u)
    {
        const uint groups_per_axis = (_ProjectionSize + 15u) / 16u;
        const uint partial_group = (group_id.z * groups_per_axis + group_id.y) * groups_per_axis + group_id.x;
        [unroll]
        for (uint coefficient = 0u; coefficient < SH_COEFFICIENT_COUNT; ++coefficient)
        {
            float3 value = 0.0f.xxx;
            [unroll]
            for (uint sample = 0u; sample < 256u; ++sample)
                value += g_sh[coefficient][sample];
            _Partial[partial_group * SH_COEFFICIENT_COUNT + coefficient] = float4(value, 0.0f);
        }
    }
}

[numthreads(64,1,1)]
void SHReduce(uint3 dispatch_id : SV_DispatchThreadID)
{
    if (dispatch_id.x >= SH_COEFFICIENT_COUNT)
        return;
    float3 value = 0.0f.xxx;
    for (uint group = 0u; group < _PartialCount; ++group)
        value += _Partial[group * SH_COEFFICIENT_COUNT + dispatch_id.x].rgb;
    const float band_scale = dispatch_id.x < 1u ? PI : (dispatch_id.x < 4u ? 2.0f * PI / 3.0f : PI / 4.0f);
    _Output[dispatch_id.x] = float4(value * band_scale, 0.0f);
}
