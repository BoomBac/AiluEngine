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

#include "../common.hlsli"
#include "../fullscreen_quad.hlsli"
#include "../color_space_utils.hlsli"

FullScreenPSInput FullscreenVSMain(FullScreenVSInput i);

PerMaterialCBufferBegin
    float _Exposure;
    int _ToneMappingMode;
PerMaterialCBufferEnd

float4 ToneMappingPS(FullScreenPSInput input) : SV_TARGET
{
    float3 color = _SourceTex.Sample(g_LinearClampSampler, input.uv).rgb * _Exposure;
    if (_ToneMappingMode == 1)
        color = ACESFilm(color);
    return float4(color, 1.0f);
}
