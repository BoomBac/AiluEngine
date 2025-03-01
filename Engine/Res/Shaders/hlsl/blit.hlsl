//info bein
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
//info end
#include "common.hlsli"
#include "fullscreen_quad.hlsli"

TEXTURE2D(_SourceTex)

FullScreenPSInput FullscreenVSMain(FullScreenVSInput v);

float4 PSMainCopy(FullScreenPSInput input) : SV_TARGET
{
	return SAMPLE_TEXTURE2D_LOD(_SourceTex, g_LinearClampSampler, input.uv, 0);
}