#pragma kernel ClearHistogram
#pragma kernel BuildHistogram
#pragma kernel ReduceAndAdaptExposure

#include "cs_common.hlsli"

static const uint kHistogramBinCount = 64u;
static const uint kSampleStep = 4u;

CBUFFER_START(AutoExposureCB)
    float4 _SourceSize;
    float4 _LogRange;
    float4 _Percentiles;
    float4 _Adaptation;
    float4 _CameraParams;
    float4 _ControlLimits;
    float4 _ExposureLimits;
    float4 _PhotometricCalibration;
    int _ExposureMode;
    int _HistoryValid;
    int _MeteringMode;
CBUFFER_END

RWStructuredBuffer<uint> _Histogram;
Texture2D<float4> _SourceTex;
Texture2D<float> _PreviousExposureEV;
RWTexture2D<float> _OutputExposureEV;

groupshared uint s_Histogram[kHistogramBinCount];

[numthreads(64, 1, 1)]
void ClearHistogram(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x < kHistogramBinCount)
        _Histogram[dispatch_thread_id.x] = 0u;
}

[numthreads(16, 16, 1)]
void BuildHistogram(uint3 dispatch_thread_id : SV_DispatchThreadID,
                    uint group_index : SV_GroupIndex)
{
    if (group_index < kHistogramBinCount)
        s_Histogram[group_index] = 0u;
    GroupMemoryBarrierWithGroupSync();

    uint2 pixel = dispatch_thread_id.xy * kSampleStep;
    uint2 source_size = uint2(_SourceSize.xy);
    if (all(pixel < source_size))
    {
        float3 color = _SourceTex.Load(int3(pixel, 0)).rgb;
        float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
        if (all(isfinite(color)) && isfinite(luminance) && luminance > 0.0)
        {
            float pre_exposure_ev = _HistoryValid != 0 ? _PreviousExposureEV.Load(int3(0, 0, 0)) : 0.0;
            float log_luminance = log2(luminance) - (isfinite(pre_exposure_ev) ? pre_exposure_ev : 0.0);
            float normalized_log_luminance = saturate((log_luminance - _LogRange.x) /
                                                       max(_LogRange.y - _LogRange.x, 1e-5));
            uint bin = min((uint)(normalized_log_luminance * kHistogramBinCount),
                           kHistogramBinCount - 1u);
            uint weight = 1u;
            if (_MeteringMode == 1)
            {
                float2 uv = (float2(pixel) + 0.5) / float2(source_size);
                float radius = length((uv - 0.5) * 2.0);
                weight = radius < 0.35 ? 4u : (radius < 0.7 ? 2u : 1u);
            }
            InterlockedAdd(s_Histogram[bin], weight);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (group_index < kHistogramBinCount)
        InterlockedAdd(_Histogram[group_index], s_Histogram[group_index]);
}

[numthreads(1, 1, 1)]
void ReduceAndAdaptExposure(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint total_count = 0u;
    [unroll]
    for (uint bin = 0u; bin < kHistogramBinCount; ++bin)
        total_count += _Histogram[bin];

    float previous_ev = _HistoryValid != 0 ? _PreviousExposureEV.Load(int3(0, 0, 0)) : 0.0;
    if (!isfinite(previous_ev))
        previous_ev = 0.0;

    if (total_count == 0u)
    {
        _OutputExposureEV[uint2(0u, 0u)] = previous_ev;
        return;
    }

    float low_count = saturate(_Percentiles.x) * total_count;
    float high_count = max(low_count + 1e-3, saturate(_Percentiles.y) * total_count);
    float cumulative_count = 0.0;
    float weighted_log_luminance = 0.0;
    float clipped_count = 0.0;
    [unroll]
    for (uint bin = 0u; bin < kHistogramBinCount; ++bin)
    {
        float bin_count = (float)_Histogram[bin];
        float overlap = max(0.0, min(cumulative_count + bin_count, high_count) -
                                  max(cumulative_count, low_count));
        float bin_center = _LogRange.x + ((float)bin + 0.5) / kHistogramBinCount *
                                         (_LogRange.y - _LogRange.x);
        weighted_log_luminance += bin_center * overlap;
        clipped_count += overlap;
        cumulative_count += bin_count;
    }
    float average_log_luminance = clipped_count > 0.0
        ? weighted_log_luminance / clipped_count
        : _LogRange.x;

    // The uncalibrated HDR protocol meters 18% gray. Physical priority modes solve EV100 with K=12.5.
    float average_log_luminance_cd_m2 = average_log_luminance +
        log2(max(_PhotometricCalibration.x, 1e-6));
    float scene_scale_ev = log2(max(_PhotometricCalibration.x, 1e-6));
    float target_ev = log2(0.18) - average_log_luminance_cd_m2 + _CameraParams.w + scene_scale_ev;
    if (_ExposureMode >= 3)
    {
        float meter_ev100 = 3.0 + average_log_luminance_cd_m2;
        float aperture = max(_CameraParams.x, 1e-4);
        float shutter = max(_CameraParams.y, 1e-4);
        float iso = max(_CameraParams.z, 1.0);
        if (_ExposureMode == 3) // Aperture priority: solve shutter.
        {
            shutter = clamp(aperture * aperture * 100.0 / (iso * exp2(meter_ev100)),
                            _ControlLimits.x, _ControlLimits.y);
        }
        else if (_ExposureMode == 4) // Shutter priority: solve aperture.
        {
            aperture = sqrt(max(shutter * iso * exp2(meter_ev100) / 100.0, 0.0));
            aperture = clamp(aperture, _ControlLimits.z, _ControlLimits.w);
        }
        else if (_ExposureMode == 5) // Auto ISO: solve sensor gain.
        {
            iso = aperture * aperture * 100.0 / (shutter * exp2(meter_ev100));
            iso = clamp(iso, 1.0, 1000000.0);
        }
        float actual_ev100 = log2((aperture * aperture / shutter) * (100.0 / iso));
        target_ev = log2(1.44) - actual_ev100 + _CameraParams.w + scene_scale_ev;
    }

    if (!isfinite(target_ev))
        target_ev = previous_ev;
    target_ev = clamp(target_ev, _ExposureLimits.x, _ExposureLimits.y);
    float adaptation_speed = target_ev > previous_ev ? _Adaptation.y : _Adaptation.z;
    float adaptation_alpha = 1.0 - exp(-max(adaptation_speed, 0.0) * max(_Adaptation.x, 0.0));
    float adapted_ev = _HistoryValid != 0 ? lerp(previous_ev, target_ev, saturate(adaptation_alpha)) : target_ev;
    _OutputExposureEV[uint2(0u, 0u)] = isfinite(adapted_ev) ? adapted_ev : previous_ev;
}
