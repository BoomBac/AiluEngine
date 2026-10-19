//info begin
//pass begin::
//name: PreExposureDomainTransform
//vert: FullscreenVSMain
//pixel: PreExposureDomainTransformPS
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//pass end::
//info end

Texture2D _SourceTex : register(t0);
Texture2D<float> _PreExposureEVTex : register(t1);

#include "common.hlsli"
#include "fullscreen_quad.hlsli"

FullScreenPSInput FullscreenVSMain(FullScreenVSInput input);

PerMaterialCBufferBegin
    float _PreExposure;
    int _UsePreExposureEV;
    int _EncodeToPreExposed;
PerMaterialCBufferEnd

float4 PreExposureDomainTransformPS(FullScreenPSInput input) : SV_TARGET
{
    float pre_exposure = max(_PreExposure, 1e-6);
    if (_UsePreExposureEV != 0)
    {
        const float pre_exposure_ev = _PreExposureEVTex.Load(int3(0, 0, 0));
        pre_exposure = exp2(clamp(isfinite(pre_exposure_ev) ? pre_exposure_ev : 0.0, -24.0, 24.0));
    }

    float4 color = _SourceTex.Sample(g_LinearClampSampler, input.uv);
    color.rgb *= _EncodeToPreExposed != 0 ? pre_exposure : rcp(pre_exposure);
    return color;
}
