//info begin
//pass begin::
//name: overlay_depth
//vert: FullscreenVSMain
//pixel: PSMainCopyDepth
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Always
//ZWrite: On
//pass end::
//info end
#include "common.hlsli"
#include "fullscreen_quad.hlsli"

TEXTURE2D(_BlitSourceTex)

float PSMainCopyDepth(FullScreenPSInput input) : SV_Depth
{
    return SAMPLE_TEXTURE2D_LOD(_BlitSourceTex, g_PointClampSampler, input.uv, 0).r;
}
