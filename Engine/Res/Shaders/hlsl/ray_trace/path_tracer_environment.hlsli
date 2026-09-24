#ifndef PATH_TRACER_ENVIRONMENT_HLSLI
#define PATH_TRACER_ENVIRONMENT_HLSLI

#include "rt_common.hlsli"

TextureCube _EnvironmentSky : register(t3, space0);

float3 RotatePathEnvironmentDirection(float3 direction)
{
#if defined(DXR)
    const float rotation = g_perSceneData.g_EnvironmentRotation;
#else
    const float rotation = g_EnvironmentRotation;
#endif
    float sine = sin(rotation);
    float cosine = cos(rotation);
    return float3(cosine * direction.x - sine * direction.z,
                  direction.y,
                  sine * direction.x + cosine * direction.z);
}

float GetPathEnvironmentIntensity()
{
#if defined(DXR)
    return g_perSceneData.g_EnvironmentIntensity;
#else
    return g_EnvironmentIntensity;
#endif
}

float3 EvaluateEnvironmentRadiance(float3 direction)
{
    direction = RotatePathEnvironmentDirection(normalize(direction));
    return _EnvironmentSky.SampleLevel(g_LinearClampSampler, direction, 0.0f).rgb *
           GetPathEnvironmentIntensity();
}

#endif // PATH_TRACER_ENVIRONMENT_HLSLI
