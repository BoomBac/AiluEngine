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

//Texture2D _SourceTex : register(t0);

TEXTURE2D(_SourceTex,0)

PSInput FullscreenVSMain(VSInput v);

float4 PSMainCopy(PSInput input) : SV_TARGET
{
	return SAMPLE_TEXTURE2D_LOD(_SourceTex, g_LinearClampSampler, input.uv, 0);
}