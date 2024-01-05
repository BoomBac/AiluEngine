//info bein
//name: blit
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Off
//ZWrite: Off
//info end
#include "common.hlsl"

Texture2D _SourceTex : register(t0);
SamplerState g_LinearSampler : register(s0);

struct VSInput
{
	float3 position : POSITION;
	float2 uv : TEXCOORD;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = float4(v.position.xy,0.0, 1.0);
	result.uv = v.uv;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 color = _SourceTex.Sample(g_LinearSampler, input.uv).rgb;
	//GammaCorrect(color,2.0f);
	return float4(color,1.0);
}
