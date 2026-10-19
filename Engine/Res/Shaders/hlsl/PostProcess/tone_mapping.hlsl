//info begin
//pass begin::
//name: ToneMapping
//vert: FullscreenVSMain
//pixel: ToneMappingPS
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//pass end::
//info end

Texture2D _SourceTex : register(t0);
Texture2D<float> _AutoExposureEVTex : register(t1);

#include "../common.hlsli"
#include "../fullscreen_quad.hlsli"
#include "../color_space_utils.hlsli"

FullScreenPSInput FullscreenVSMain(FullScreenVSInput i);

PerMaterialCBufferBegin
    float _Exposure;
    int _ToneMappingMode;
    float _Gamma;
    int _UseAutoExposureEV;
PerMaterialCBufferEnd

// Rec.709 Minimal AgX: scene-linear HDR input, display-linear SDR output.
float3 AgxContrast(float3 color)
{
    const float3 color2 = color * color;
    const float3 color4 = color2 * color2;
    return 15.5 * color4 * color2 - 40.14 * color4 * color + 31.96 * color4 - 6.868 * color2 * color
        + 0.4298 * color2 + 0.1191 * color - 0.00232;
}

float3 AgxToneMap(float3 scene_linear_color)
{
    static const float3x3 kAgxInset = float3x3(
        0.84247906, 0.07843360, 0.07922375,
        0.04232824, 0.87846864, 0.07916613,
        0.04237565, 0.07843360, 0.87914297);
    static const float3x3 kAgxOutset = float3x3(
         1.19687901, -0.09802088, -0.09902974,
        -0.05289685,  1.15190313, -0.09896118,
        -0.05297164, -0.09804345,  1.15107367);
    static const float kAgxMinEv = -12.47393;
    static const float kAgxMaxEv = 4.026069;

    float3 color = mul(kAgxInset, max(scene_linear_color, 0.0));
    color = saturate((log2(max(color, 1e-10)) - kAgxMinEv) / (kAgxMaxEv - kAgxMinEv));
    color = AgxContrast(color);
    color = mul(kAgxOutset, color);
    // Linearize the fitted display response; this is not an output sRGB/PQ encode or an artist gamma control.
    return saturate(pow(max(color, 0.0), 2.2));
}

float4 ToneMappingPS(FullScreenPSInput input) : SV_TARGET
{
    float exposure = _UseAutoExposureEV != 0 ? exp2(_AutoExposureEVTex.Load(int3(0, 0, 0))) : _Exposure;
    float3 color = _SourceTex.Sample(g_LinearClampSampler, input.uv).rgb * (exposure / GetPreExposure());
    if (_ToneMappingMode == 1)
        color = ACESFilm(color);
    else if (_ToneMappingMode == 2)
        color = AgxToneMap(color);
    else if (_ToneMappingMode == 3)
        color = saturate(color);
    // Blender applies artist gamma after the display transform. Return to display-linear for the output contract.
    if (_Gamma > 0.0 && abs(_Gamma - 1.0) > 1e-6)
    {
        float3 display_color = ApplySRGBCurve(max(color, 0.0));
        color = RemoveSRGBCurve(pow(max(display_color, 0.0), rcp(_Gamma)));
    }
    return float4(color, 1.0f);
}
