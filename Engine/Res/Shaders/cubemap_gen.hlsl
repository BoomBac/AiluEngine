//info bein
//name: cubemap_gen
//vert: VSMain
//pixel: PSMain
//Cull: Front
//Queue: Opaque
//Properties
//{
//  env("Env",Texture2D) = white
//}
//info end

#include "common.hlsli"
#include "constants.hlsli"

Texture2D env : register(t1);
SamplerState g_LinearSampler : register(s0);

struct VSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float3 wnormal : NORMAL;
};

static const float2 inv_atan = { 0.1591f, 0.3183f };
float2 SampleSphericalMap(float3 v)
{
	float2 uv = float2(atan2(v.x, v.z), asin(v.y));
	uv *= inv_atan;
	uv += 0.5;
	return uv;
}


PSInput VSMain(VSInput v)
{
	PSInput result;
	float4x4 mvp = mul(_PassMatrixVP, _MatrixWorld);
	result.position = mul(mvp, float4(v.position, 1.0f));
	result.wnormal = normalize(v.position);
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float2 uv = SampleSphericalMap(normalize(-input.wnormal));
	float3 sky_color = env.Sample(g_LinearSampler, uv).rgb;
	return float4(sky_color, 1.0);
}