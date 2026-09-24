//info begin
//pass begin::
//name: SurfelCoverageDebug
//vert: FullscreenVSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
//ZTest: Always
//ZWrite: Off
//pass end::
//info end

#include "common.hlsli"
#include "fullscreen_quad.hlsli"

TEXTURE2D(_BlitSourceTex)
static const float kInvalidCoverage = 1.0e20f;

float4 PSMain(FullScreenPSInput input) : SV_TARGET
{
    float coverage = SAMPLE_TEXTURE2D_LOD(_BlitSourceTex, g_LinearClampSampler, input.uv, 0).r;
    coverage = coverage >= kInvalidCoverage * 0.1f ? 0.0f : saturate(coverage);
    return float4(coverage.xxx, 1.0f);
}
