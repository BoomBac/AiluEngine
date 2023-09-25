#include "cbuffer.hlsl"
#include "common.hlsl"

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
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 n = TexNormal.Sample(g_LinearSampler, input.uv0);
	n = normalize(mul(n * 2.0 - 1.0,input.btn));
	float3 light_pos = float3(100, 100, 100);
	float3 light_color = float3(1, 1, 1);
	float3 base_color = TexAlbedo.Sample(g_LinearSampler, input.uv0);
	//return float4(dot(input.normal, normalize(light_pos)) * base_color, 1.0f);
	return float4(dot(n, normalize(light_pos)) * base_color, 1.0f) + _Color * 0.00001;
	//return normal.Sample(g_LinearSampler, input.uv0) + albedo.Sample(g_LinearSampler, input.uv0) * 0.0001;
	//return float4(input.normal.xyz * 0.5 + 0.5,1.0);
}
