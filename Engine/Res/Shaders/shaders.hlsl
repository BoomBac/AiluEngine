#include "cbuffer.hlsl"

struct VSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float2 uv0 : TEXCOORD;
	float4 tangent : TANGENT;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float3 normal : NORMAL;
	float2 uv0 : TEXCOORD0;
	float4 tangent : TANGENT;
};

Texture2D albedo : register(t0);
SamplerState g_LinearSampler : register(s0);

PSInput VSMain(VSInput v)
{
	PSInput result;
	float4x4 mvp = mul(_MatrixWorld, _MatrixVP);
	result.position = mul(float4(v.position,1.0f), mvp);
	result.normal = v.normal;
	result.uv0 = v.uv0;
	result.tangent = v.tangent;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	//return albedo.Sample(g_LinearSampler, input.uv0);
	return float4(input.normal.xyz * 0.5 + 0.5,1.0);
}
