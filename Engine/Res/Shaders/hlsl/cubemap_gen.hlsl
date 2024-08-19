//info bein
//pass begin::
//name: cubemap_gen
//vert: VSMain
//pixel: PSMain
//Cull: Off
//Queue: Opaque
//pass end::
//Properties
//{
//  env("Env",Texture2D) = white
//}
//info end

#include "common.hlsli"
#include "constants.hlsli"

Texture2D env : register(t1);

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


float2 SampleSphericalMap(float3 v)
{
	static const float2 inv_atan = { 0.1591f, 0.3183f };
	float2 uv = float2(atan2(v.x, v.z), asin(v.y));
	uv *= inv_atan;
	uv += 0.5;
	return uv;
}


PSInput VSMain(VSInput v)
{
	PSInput result;
	float4x4 mvp = mul(_MatrixVP, _MatrixWorld);
	result.position = mul(mvp, float4(v.position, 1.0f));
	result.wnormal = normalize(v.position);
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float2 uv = SampleSphericalMap(normalize(-input.wnormal));
	float3 sky_color = env.Sample(g_LinearClampSampler, uv).rgb;
	return float4(sky_color, 1.0);
}
