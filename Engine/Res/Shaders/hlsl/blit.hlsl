//info begin
//pass begin::
//name: blit
//vert: FullscreenVSMain
//pixel: PSMainCopy
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Always
//ZWrite: Off
//pass end::
//pass begin::
//name: blit_point
//vert: FullscreenVSMain
//pixel: PSMainCopyPoint
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Always
//ZWrite: Off
//pass end::
//info end
#include "common.hlsli"
#include "fullscreen_quad.hlsli"

TEXTURE2D(_BlitSourceTex)

FullScreenPSInput FullscreenVSMain(FullScreenVSInput v);

float4 PSMainCopy(FullScreenPSInput input) : SV_TARGET
{
	return SAMPLE_TEXTURE2D_LOD(_BlitSourceTex, g_LinearClampSampler, input.uv, 0);
}

float4 PSMainCopyPoint(FullScreenPSInput input) : SV_TARGET
{
	return SAMPLE_TEXTURE2D_LOD(_BlitSourceTex, g_PointClampSampler, input.uv, 0);
}