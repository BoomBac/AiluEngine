/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef TRIANGLE_LIGHT_HLSLI
#define TRIANGLE_LIGHT_HLSLI

//#include "HelperFunctions.hlsli"
//#include <donut/shaders/packing.hlsli>

#include "RAB_Struct.hlsli"

// For converting an area measure pdf to solid angle measure pdf
float pdfAtoW(float pdfA, float distance_, float cosTheta)
{
    return pdfA * distance_*distance_ / cosTheta;
}

// Helper function to reflect the folds of the lower hemisphere
// over the diagonals in the octahedral map
float2 octWrap(float2 v)
{
#if __HLSL_VERSION >= 2021 || __SLANG__
    return (1.f - abs(v.yx)) * select(v.xy >= 0.f, 1.f, -1.f);
#else
    return (1.f - abs(v.yx)) * (v.xy >= 0.f ? 1.f : -1.f);
#endif
}

/**********************/
// Signed encodings
// Converts a normalized direction to the octahedral map (non-equal area, signed)
// n - normalized direction
// Returns a signed position in octahedral map [-1, 1] for each component
float2 ndirToOctSigned(float3 n)
{
    // Project the sphere onto the octahedron (|x|+|y|+|z| = 1) and then onto the xy-plane
    float2 p = n.xy * (1.f / (abs(n.x) + abs(n.y) + abs(n.z)));
    return (n.z < 0.f) ? octWrap(p) : p;
}

// Converts a point in the octahedral map to a normalized direction (non-equal area, signed)
// p - signed position in octahedral map [-1, 1] for each component 
// Returns normalized direction
float3 octToNdirSigned(float2 p)
{
    // https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));
    float t = max(0, -n.z);
#if __HLSL_VERSION >= 2021 || __SLANG__
    n.xy += select(n.xy >= 0.0, -t, t);
#else
    n.xy += n.xy >= 0.0 ? -t : t;
#endif
    return normalize(n);
}

/**********************/
// Unorm 32 bit encodings
// Converts a normalized direction to the octahedral map (non-equal area, unsigned normalized)
// n - normalized direction
// Returns a packed 32 bit unsigned normalized position in octahedral map
// The two components of the result are stored in UNORM16 format, [0..1]
uint ndirToOctUnorm32(float3 n)
{
    float2 p = ndirToOctSigned(n);
    p = saturate(p.xy * 0.5 + 0.5);
    return uint(p.x * 0xfffe) | (uint(p.y * 0xfffe) << 16);
}

// Converts a point in the octahedral map (non-equal area, unsigned normalized) to normalized direction
// pNorm - a packed 32 bit unsigned normalized position in octahedral map
// Returns normalized direction
float3 octToNdirUnorm32(uint pUnorm)
{
    float2 p;
    p.x = saturate(float(pUnorm & 0xffff) / 0xfffe);
    p.y = saturate(float(pUnorm >> 16) / 0xfffe);
    p = p * 2.0 - 1.0;
    return octToNdirSigned(p);
}

float2 Unpack_R16G16_FLOAT(uint rg)
{
    uint2 d = uint2(rg, rg >> 16);
    return f16tof32(d);
}

float4 Unpack_R16G16B16A16_FLOAT(uint2 rgba)
{
    return float4(Unpack_R16G16_FLOAT(rgba.x), Unpack_R16G16_FLOAT(rgba.y));
}

struct TriangleLight
{
    float3 base;
    float3 edge1;
    float3 edge2;
    float3 radiance;
    float3 normal;
    float surfaceArea;

    // Interface methods

    float calcSolidAnglePdf(in const float3 viewerPosition,
                            in const float3 lightSamplePosition,
                            in const float3 lightSampleNormal)
    {
        float3 L = lightSamplePosition - viewerPosition;
        float Ldist = length(L);
        L /= Ldist;

        const float areaPdf = 1.0 / surfaceArea;
        const float sampleCosTheta = saturate(dot(L, -lightSampleNormal));

        return pdfAtoW(areaPdf, Ldist, sampleCosTheta);
    }

    // Helper methods

    static TriangleLight Create(in const RAB_LightInfo lightInfo)
    {
        TriangleLight triLight;

        triLight.edge1 = octToNdirUnorm32(lightInfo.direction1) * f16tof32(lightInfo.scalars);
        triLight.edge2 = octToNdirUnorm32(lightInfo.direction2) * f16tof32(lightInfo.scalars >> 16);
        triLight.base = lightInfo.center - (triLight.edge1 + triLight.edge2) / 3.0;
        triLight.radiance = Unpack_R16G16B16A16_FLOAT(lightInfo.radiance).rgb;

        float3 lightNormal = cross(triLight.edge1, triLight.edge2);
        float lightNormalLength = length(lightNormal);

        if(lightNormalLength > 0.0)
        {
            triLight.surfaceArea = 0.5 * lightNormalLength;
            triLight.normal = lightNormal / lightNormalLength;
        }
        else
        {
           triLight.surfaceArea = 0.0;
           triLight.normal = 0.0; 
        }

        return triLight;
    }

};

#endif // TRIANGLE_LIGHT_HLSLI
