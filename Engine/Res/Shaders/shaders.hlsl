#include "cbuffer.hlsl"
#include "common.hlsl"
#include "lighting.hlsl"

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
	float3 world_pos : POSITION;
	float3 normal : NORMAL;
	float2 uv0 : TEXCOORD0;
	float3x3 btn : BTN;
};

Texture2D TexAlbedo : register(t0);
Texture2D TexNormal : register(t1);
SamplerState g_LinearSampler : register(s0);

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformToClipSpace(v.position);
	result.normal = v.normal;
	result.uv0 = v.uv0;
	float3 B = normalize(mul(float4(cross(v.tangent.xyz, v.normal), 0.0f), _MatrixWorld).xyz);
	float3 T = normalize(mul(float4(v.tangent.xyz,0.0f),_MatrixWorld).xyz);
	float3 N = normalize(mul(float4(v.normal,0.0f),_MatrixWorld).xyz);
	result.btn = float3x3(T,B,N);
	result.normal = mul(v.normal, (float3x3) _MatrixWorld);
	result.world_pos = TransformToWorldSpace(v.position);
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 n = TexNormal.Sample(g_LinearSampler, input.uv0);
	n = normalize(mul(n * 2.0 - 1.0,input.btn));
	float3 base_color = TexAlbedo.Sample(g_LinearSampler, input.uv0);
	float3 light = CalculateLight(input.world_pos, input.normal);
	//return float4(float4(light, 1.0f) + _Color * 0.00001);
	//return float4(float4(light, 1.0f) + _Color * 0.00001);
	return float4(float4(light, 1.0f) + _Color * 0.00001);
	//return float4(float4(light, 1.0f) + _Color * 0.00001);
	//return float4(1.0, 1.0, 1.0, 1.0) * (length(input.world_pos - float3(0.0, 0.0, 0.0)) / 1000.0);
}
