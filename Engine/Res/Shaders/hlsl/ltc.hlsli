#ifndef __LTC_H__
#define __LTC_H__
#include "common.hlsli"

// TEXTURE2D(_LTC_Lut1)
// TEXTURE2D(_LTC_Lut2)

Texture2D _LTC_Lut1 : register(t14);
Texture2D _LTC_Lut2 : register(t15);

static const float LUT_SIZE  = 64.0; // ltc_texture size
static const float LUT_SCALE = (LUT_SIZE - 1.0) / LUT_SIZE;
static const float LUT_BIAS  = 0.5 / LUT_SIZE;


float3x3 Identity()
{
    float3x3 tmp;
    tmp[0] = float3(1, 0, 0);
    tmp[1] = float3(0, 1, 0);
    tmp[2] = float3(0, 0, 1);
    return tmp;
}

float2 LTC_UV(float nv,float rouhhness)
{
    float2 uv = float2(rouhhness,sqrt(1.0 - nv));
    uv = uv*LUT_SCALE + LUT_BIAS;
    return uv;
}
float4 LTC_LUT1Value(float2 uv)
{
    return SAMPLE_TEXTURE2D_LOD(_LTC_Lut1,g_LinearClampSampler,uv,0);
}
float4 LTC_LUT2Value(float2 uv)
{
    return SAMPLE_TEXTURE2D_LOD(_LTC_Lut2,g_LinearClampSampler,uv,0);
}
//------------------------------------------------LTC(Area light)------------------------------------------------//
//Ref: https://github.com/selfshadow/ltc_code
float3 IntegrateEdgeVec(float3 v1, float3 v2)
{
    float x = dot(v1, v2);
    float y = abs(x);

    float a = 0.8543985 + (0.4965155 + 0.0145206*y)*y;
    float b = 3.4175940 + (4.1616724 + y)*y;
    float v = a / b;

    float theta_sintheta = (x > 0.0) ? v : 0.5* rsqrt(max(1.0 - x*x, 1e-7)) - v;

    return cross(v1, v2)*theta_sintheta;
}

float IntegrateEdge(float3 v1, float3 v2)
{
    return IntegrateEdgeVec(v1, v2).z;
}

void ClipQuadToHorizon(inout float3 L[5], out int n)
{
    // detect clipping config
    int config = 0;
    if (L[0].z > 0.0) config += 1;
    if (L[1].z > 0.0) config += 2;
    if (L[2].z > 0.0) config += 4;
    if (L[3].z > 0.0) config += 8;

    // clip
    n = 0;

    if (config == 0)
    {
        // clip all
    }
    else if (config == 1) // V1 clip V2 V3 V4
    {
        n = 3;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[3].z * L[0] + L[0].z * L[3];
    }
    else if (config == 2) // V2 clip V1 V3 V4
    {
        n = 3;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    }
    else if (config == 3) // V1 V2 clip V3 V4
    {
        n = 4;
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
        L[3] = -L[3].z * L[0] + L[0].z * L[3];
    }
    else if (config == 4) // V3 clip V1 V2 V4
    {
        n = 3;
        L[0] = -L[3].z * L[2] + L[2].z * L[3];
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
    }
    else if (config == 5) // V1 V3 clip V2 V4) impossible
    {
        n = 0;
    }
    else if (config == 6) // V2 V3 clip V1 V4
    {
        n = 4;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    }
    else if (config == 7) // V1 V2 V3 clip V4
    {
        n = 5;
        L[4] = -L[3].z * L[0] + L[0].z * L[3];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    }
    else if (config == 8) // V4 clip V1 V2 V3
    {
        n = 3;
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
        L[1] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] =  L[3];
    }
    else if (config == 9) // V1 V4 clip V2 V3
    {
        n = 4;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[2].z * L[3] + L[3].z * L[2];
    }
    else if (config == 10) // V2 V4 clip V1 V3) impossible
    {
        n = 0;
    }
    else if (config == 11) // V1 V2 V4 clip V3
    {
        n = 5;
        L[4] = L[3];
        L[3] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    }
    else if (config == 12) // V3 V4 clip V1 V2
    {
        n = 4;
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
    }
    else if (config == 13) // V1 V3 V4 clip V2
    {
        n = 5;
        L[4] = L[3];
        L[3] = L[2];
        L[2] = -L[1].z * L[2] + L[2].z * L[1];
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
    }
    else if (config == 14) // V2 V3 V4 clip V1
    {
        n = 5;
        L[4] = -L[0].z * L[3] + L[3].z * L[0];
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
    }
    else if (config == 15) // V1 V2 V3 V4
    {
        n = 4;
    }

    if (n == 3)
        L[3] = L[0];
    if (n == 4)
        L[4] = L[0];
}

float3 LTC_Evaluate(float3 N, float3 V, float3 P, float3x3 Minv, float3 points[4], bool twoSided)
{
    // construct orthonormal basis around N
    float3 T1, T2;
    T1 = normalize(V - N*dot(V, N));
    T2 = cross(N, T1);

    // rotate area light in (T1, T2, N) basis
    Minv = mul(Minv, float3x3(T1, T2, N));

    // polygon (allocate 5 vertices for clipping)
    float3 L[5];
    L[0] = mul(Minv, points[0].xyz - P);
    L[1] = mul(Minv, points[1].xyz - P);
    L[2] = mul(Minv, points[2].xyz - P);
    L[3] = mul(Minv, points[3].xyz - P);
	L[4] = L[0];

    int n;
    ClipQuadToHorizon(L, n);
    
	if (n == 0)
		return float3(0, 0, 0);

    // project onto sphere
    L[0] = normalize(L[0]);
    L[1] = normalize(L[1]);
    L[2] = normalize(L[2]);
    L[3] = normalize(L[3]);
    L[4] = normalize(L[4]);

    // integrate
    float sum = 0.0;

    sum += IntegrateEdge(L[0], L[1]);
    sum += IntegrateEdge(L[1], L[2]);
    sum += IntegrateEdge(L[2], L[3]);
    if (n >= 4)
        sum += IntegrateEdge(L[3], L[4]);
    if (n == 5)
        sum += IntegrateEdge(L[4], L[0]);

    sum = twoSided ? abs(sum) : max(0.0, sum);

    float3 Lo_i = float3(sum, sum, sum);

    return Lo_i;
}

#endif //__LTC_H__