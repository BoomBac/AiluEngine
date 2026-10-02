#ifndef __VOLUMETRIC_FOG_COMMON_HLSLI__
#define __VOLUMETRIC_FOG_COMMON_HLSLI__

#include "common.hlsli"

TEXTURE3D(_VolumetricFogAccum)

float4 SampleVolumetricFog(float2 screen_uv, float view_depth)
{
    if (_volumetric_fog_enabled < 0.5f || view_depth <= _ProjectionParams.y)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    uint accum_width, accum_height, accum_depth;
    _VolumetricFogAccum.GetDimensions(accum_width, accum_height, accum_depth);
    float near_plane = _ProjectionParams.y;
    float slice_fraction = saturate(log(view_depth / near_plane) / log(_volumetric_fog_far / near_plane));
    float boundary_index = slice_fraction * accum_depth;
    float3 fog_uv = float3(screen_uv.x, 1.0f - screen_uv.y, 0.0f);

    if (boundary_index < 1.0f)
    {
        fog_uv.z = 0.5f / accum_depth;
        float4 first_slice = _VolumetricFogAccum.SampleLevel(g_LinearClampSampler, fog_uv, 0);
        return lerp(float4(0.0f, 0.0f, 0.0f, 1.0f), first_slice, boundary_index);
    }

    fog_uv.z = (boundary_index - 0.5f) / accum_depth;
    return _VolumetricFogAccum.SampleLevel(g_LinearClampSampler, fog_uv, 0);
}

float3 ApplyVolumetricFogStraight(float3 color, float2 screen_uv, float view_depth)
{
    float4 fog = SampleVolumetricFog(screen_uv, view_depth);
    return color * saturate(fog.a) + fog.rgb;
}

float3 ApplyVolumetricFogPremultiplied(float3 color, float opacity, float2 screen_uv, float view_depth)
{
    float4 fog = SampleVolumetricFog(screen_uv, view_depth);
    return color * saturate(fog.a) + saturate(opacity) * fog.rgb;
}

#endif
