#ifndef __SPHERICAL_HARMONICS_HLSLI__
#define __SPHERICAL_HARMONICS_HLSLI__

#define SH_COEFFICIENT_COUNT 9

void EvaluateSHBasis9(float3 dir, out float basis[SH_COEFFICIENT_COUNT])
{
    dir = normalize(dir);
    basis[0] = 0.2820947918f;
    basis[1] = 0.4886025119f * dir.y;
    basis[2] = 0.4886025119f * dir.z;
    basis[3] = 0.4886025119f * dir.x;
    basis[4] = 1.0925484306f * dir.x * dir.y;
    basis[5] = 1.0925484306f * dir.y * dir.z;
    basis[6] = 0.3153915653f * (3.0f * dir.z * dir.z - 1.0f);
    basis[7] = 1.0925484306f * dir.x * dir.z;
    basis[8] = 0.5462742153f * (dir.x * dir.x - dir.y * dir.y);
}

float3 EvaluateSH9(StructuredBuffer<float4> coeffs, float3 dir)
{
    float basis[SH_COEFFICIENT_COUNT];
    EvaluateSHBasis9(dir, basis);
    float3 result = 0.0f.xxx;
    [unroll]
    for (uint index = 0u; index < SH_COEFFICIENT_COUNT; ++index)
        result += coeffs[index].rgb * basis[index];
    return result;
}

float3 EvaluateSH9(float4 coeffs[SH_COEFFICIENT_COUNT], float3 dir)
{
    float basis[SH_COEFFICIENT_COUNT];
    EvaluateSHBasis9(dir, basis);
    float3 result = 0.0f.xxx;
    [unroll]
    for (uint index = 0u; index < SH_COEFFICIENT_COUNT; ++index)
        result += coeffs[index].rgb * basis[index];
    return result;
}

#endif // __SPHERICAL_HARMONICS_HLSLI__
