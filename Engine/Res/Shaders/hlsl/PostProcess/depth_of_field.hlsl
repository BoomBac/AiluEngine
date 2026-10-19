//info begin
//pass begin::
//name: DepthOfFieldPrefilter
//vert: FullscreenVSMain
//pixel: PrefilterPS
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//pass end::
//pass begin::
//name: DepthOfFieldDilateX
//vert: FullscreenVSMain
//pixel: DilateXPS
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//pass end::
//pass begin::
//name: DepthOfFieldDilateY
//vert: FullscreenVSMain
//pixel: DilateYPS
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//pass end::
//pass begin::
//name: DepthOfFieldFar
//vert: FullscreenVSMain
//pixel: FarPS
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//pass end::
//pass begin::
//name: DepthOfFieldNear
//vert: FullscreenVSMain
//pixel: NearPS
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//pass end::
//pass begin::
//name: DepthOfField
//vert: FullscreenVSMain
//pixel: DepthOfFieldPS
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//pass end::
//info end

Texture2D _SourceTex : register(t0);
Texture2D<float> _CameraDepthTexture : register(t1);
Texture2D<float4> _PreparedTex : register(t2);
Texture2D<float> _RadiusTex : register(t3);
Texture2D<float4> _FarTex : register(t4);
Texture2D<float4> _NearTex : register(t5);

#include "../common.hlsli"
#include "../fullscreen_quad.hlsli"

FullScreenPSInput FullscreenVSMain(FullScreenVSInput i);

PerMaterialCBufferBegin
    float4 _SourceSize;
    float4 _WorkSize;
    float4 _LensParams;
    float4 _CameraParams;
    float _MaximumCoCDiameterPx;
    int _ApertureBladeCount;
PerMaterialCBufferEnd

static const int kSampleCount = 256;
static const float kGoldenAngle = 2.39996323;
static const float kTwoPi = 6.28318530718;

// Negative radius is in front of focus. All radii remain in output pixels, including half-resolution passes.
float SignedRadius(float2 uv)
{
    float device_depth = _CameraDepthTexture.SampleLevel(g_PointClampSampler, uv, 0);
    if (!isfinite(device_depth))
        return 0.0;
    float depth_m = max(LinearEyeDepth(device_depth, _CameraParams.x, _CameraParams.y), 1e-5);
    float focal_length = _LensParams.x;
    float denominator = max(_LensParams.y * depth_m * (_LensParams.z - focal_length), 1e-6);
    float diameter = focal_length * focal_length * (depth_m - _LensParams.z) / denominator;
    float radius = 0.5 * diameter / _LensParams.w * _SourceSize.y;
    return clamp(radius, -0.5 * _MaximumCoCDiameterPx, 0.5 * _MaximumCoCDiameterPx);
}

float ApertureBoundary(float angle)
{
    float sector = kTwoPi / max(_ApertureBladeCount, 3);
    float local_angle = frac((angle + 3.14159265359) / sector) * sector - 0.5 * sector;
    return cos(0.5 * sector) / max(cos(local_angle), 1e-4);
}

// Area-uniform triangle sampling of the regular polygon avoids disc rejection and sparse polygon corners.
float2 ApertureSample(int index)
{
    float blade_count = float(max(_ApertureBladeCount, 3));
    float blade = float(index % max(_ApertureBladeCount, 3));
    float angle_a = blade * kTwoPi / blade_count - 3.14159265359;
    float angle_b = angle_a + kTwoPi / blade_count;
    float2 a = float2(cos(angle_a), sin(angle_a));
    float2 b = float2(cos(angle_b), sin(angle_b));
    float radial = sqrt((float(index) + 0.5) / float(kSampleCount));
    return lerp(a, b, frac(float(index) * kGoldenAngle)) * radial;
}

float4 PrefilterPS(FullScreenPSInput input) : SV_TARGET
{
    // Four source texels per work texel. Keep the nearest CoC at mixed near/far boundaries and exclude far color.
    float4 taps[4];
    float selected_radius = 0.0;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float2 offset = float2(i & 1, i >> 1) - 0.5;
        float2 uv = input.uv + offset * _SourceSize.zw;
        float radius = SignedRadius(uv);
        taps[i] = float4(_SourceTex.SampleLevel(g_LinearClampSampler, uv, 0).rgb, radius);
        selected_radius = radius < 0.0 ? min(selected_radius, radius) :
            (selected_radius < 0.0 ? selected_radius : max(selected_radius, radius));
    }
    float3 color = 0.0;
    float weights = 0.0;
    [unroll]
    for (int j = 0; j < 4; ++j)
    {
        float weight = selected_radius < -0.5 ? (taps[j].a < -0.5 ? 1.0 : 0.0) : 1.0;
        color += taps[j].rgb * weight;
        weights += weight;
    }
    return float4(color / max(weights, 1.0), selected_radius);
}

float4 DilateXPS(FullScreenPSInput input) : SV_TARGET
{
    float radius = 0.0;
    int extent = min(16, int(ceil(0.5 * _MaximumCoCDiameterPx * _WorkSize.x / _SourceSize.x)));
    [loop]
    for (int x = -extent; x <= extent; ++x)
        radius = max(radius, -_PreparedTex.SampleLevel(g_PointClampSampler,
            input.uv + float2(x, 0) * _WorkSize.zw, 0).a);
    return float4(radius, 0.0, 0.0, 1.0);
}

float4 DilateYPS(FullScreenPSInput input) : SV_TARGET
{
    float radius = 0.0;
    int extent = min(16, int(ceil(0.5 * _MaximumCoCDiameterPx * _WorkSize.y / _SourceSize.y)));
    [loop]
    for (int y = -extent; y <= extent; ++y)
        radius = max(radius, _RadiusTex.SampleLevel(g_PointClampSampler,
            input.uv + float2(0, y) * _WorkSize.zw, 0));
    return float4(radius, 0.0, 0.0, 1.0);
}

float3 LayerColor(float2 uv, bool near_layer)
{
    float2 pixel = uv * _WorkSize.xy - 0.5;
    int2 origin = int2(floor(pixel));
    float2 fraction = frac(pixel);
    float3 color = 0.0;
    float weights = 0.0;
    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            int2 coord = clamp(origin + int2(x, y), int2(0, 0), int2(_WorkSize.xy) - 1);
            float4 sample_value = _PreparedTex.Load(int3(coord, 0));
            float weight = (x == 0 ? 1.0 - fraction.x : fraction.x) *
                (y == 0 ? 1.0 - fraction.y : fraction.y);
            weight *= near_layer ? (sample_value.a < -0.5 ? 1.0 : 0.0) :
                (sample_value.a >= -0.5 ? 1.0 : 0.0);
            color += sample_value.rgb * weight;
            weights += weight;
        }
    }
    return weights > 1e-5 ? color / weights : _PreparedTex.SampleLevel(g_PointClampSampler, uv, 0).rgb;
}

float4 FarPS(FullScreenPSInput input) : SV_TARGET
{
    float4 center = _PreparedTex.SampleLevel(g_PointClampSampler, input.uv, 0);
    bool fill_background = center.a < -0.5;
    float radius = fill_background ? _RadiusTex.SampleLevel(g_PointClampSampler, input.uv, 0) : max(center.a, 0.0);
    if (radius < 0.5)
        return float4(center.rgb, 0.0);
    float3 color = 0.0;
    float weights = 0.0;
    [loop]
    for (int i = 0; i < kSampleCount; ++i)
    {
        float2 offset = ApertureSample(i) * radius;
        float2 uv = input.uv + offset * _SourceSize.zw;
        float sample_radius = _PreparedTex.SampleLevel(g_PointClampSampler, uv, 0).a;
        float distance_to_center = length(offset) / ApertureBoundary(atan2(offset.y, offset.x));
        // A neighbor contributes only if its own far bokeh covers this destination. Never average near color in.
        float weight = fill_background ? (sample_radius >= -0.5 ? 1.0 : 0.0) :
            saturate(min(radius, sample_radius) - distance_to_center + 1.0) * smoothstep(0.0, 1.0, sample_radius);
        color += LayerColor(uv, false) * weight;
        weights += weight;
    }
    // Approximate the hidden background at near edges from visible non-near neighbors. Interior holes have no data.
    return float4(weights > 1e-5 ? color / weights : center.rgb, weights > 1e-5 ? radius : 0.0);
}

float4 NearPS(FullScreenPSInput input) : SV_TARGET
{
    // Dilated support makes near bokeh reach sharp pixels outside the foreground silhouette.
    float radius = _RadiusTex.SampleLevel(g_PointClampSampler, input.uv, 0);
    if (radius < 0.5)
        return 0.0;
    float3 color = 0.0;
    float coverage = 0.0;
    [loop]
    for (int i = 0; i < kSampleCount; ++i)
    {
        float2 offset = ApertureSample(i) * radius;
        float2 uv = input.uv + offset * _SourceSize.zw;
        float sample_radius = max(-_PreparedTex.SampleLevel(g_PointClampSampler, uv, 0).a, 0.0);
        float distance_to_center = length(offset) / ApertureBoundary(atan2(offset.y, offset.x));
        float weight = saturate(sample_radius - distance_to_center + 1.0) * smoothstep(0.5, 1.5, sample_radius);
        // Normalize by the source footprint area, not by the number of foreground samples, to retain coverage.
        weight *= radius * radius / max(sample_radius * sample_radius, 1.0) / float(kSampleCount);
        color += LayerColor(uv, true) * weight;
        coverage += weight;
    }
    float alpha = saturate(coverage);
    return float4(coverage > 1e-5 ? color / coverage * alpha : 0.0, alpha);
}

float4 DepthOfFieldPS(FullScreenPSInput input) : SV_TARGET
{
    float3 source = _SourceTex.SampleLevel(g_LinearClampSampler, input.uv, 0).rgb;
    float center_radius = SignedRadius(input.uv);
    float3 far_color = 0.0;
    float far_weight = 0.0;
    float4 near_layer = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 uv = input.uv + float2(x, y) * _WorkSize.zw;
            float weight = float((2 - abs(x)) * (2 - abs(y))) / 16.0;
            float4 far_sample = _FarTex.SampleLevel(g_LinearClampSampler, uv, 0);
            // Bilateral reconstruction keeps focused/foreground colors out of the far layer.
            float far_mask = smoothstep(0.0, 1.0, far_sample.a);
            far_color += far_sample.rgb * weight * far_mask;
            far_weight += weight * far_mask;
            near_layer += _NearTex.SampleLevel(g_LinearClampSampler, uv, 0) * weight;
        }
    }
    float far_blend = center_radius < -0.5 ? (far_weight > 1e-5 ? 1.0 : 0.0) : smoothstep(0.5, 1.5, center_radius);
    float3 background = lerp(source, far_weight > 1e-5 ? far_color / far_weight : source, far_blend);
    return float4(background * (1.0 - near_layer.a) + near_layer.rgb, 1.0);
}
