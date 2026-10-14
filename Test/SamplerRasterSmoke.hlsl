//info begin
//pass begin::
//name: SamplerRasterSmoke
//vert: FullscreenVSMain
//pixel: PSMain
//Cull: Back
//ZTest: Always
//ZWrite: Off
//pass end::
//info end
#include "../Engine/Res/Shaders/hlsl/common.hlsli"
#include "../Engine/Res/Shaders/hlsl/material_sampling.hlsli"
Texture2D _AlbedoTex;
PerMaterialCBufferBegin
    float _TestManualBias;
    float _TestEffectiveBias;
    uint _TextureViewMipBiasMask;
    uint _AlbedoTexSamplerIndex;
PerMaterialCBufferEnd
#include "../Engine/Res/Shaders/hlsl/fullscreen_quad.hlsli"
float4 PSMain(FullScreenPSInput input) : SV_TARGET
{
    return _AlbedoTex.SampleBias(g_material_samplers[_AlbedoTexSamplerIndex], input.uv, ResolveMaterialMipBias(_TextureViewMipBiasMask, 0u, _TestManualBias, _TestEffectiveBias));
}
