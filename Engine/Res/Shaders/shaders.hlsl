#include "input.hlsl"
#include "cbuffer.hlsl"
#include "common.hlsl"
#include "lighting.hlsl"



Texture2D TexAlbedo : register(t0);
Texture2D TexNormal : register(t1);
Texture2D TexEmssive : register(t2);
Texture2D TexRoughness : register(t3);

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
	result.normal = N;
	result.world_pos = TransformToWorldSpace(v.position);
	return result;
}

void InitSurfaceData(PSInput input,out float3 wnormal,out float4 albedo,out float roughness,out float metallic,out float3 emssive,out float specular)
{
	if(H_Uint_SamplerMask & 1) albedo = TexAlbedo.Sample(g_LinearSampler,input.uv0);
	else albedo = Color4_BaseColor;
	if(H_Uint_SamplerMask & 2) 
	{
		wnormal = TexNormal.Sample(g_LinearSampler,input.uv0).xyz;
		wnormal = normalize(mul(wnormal* 2.0 - 1.0,input.btn));
	}
	else wnormal = input.normal;
	if(H_Uint_SamplerMask & 4)
	{
		emssive = TexEmssive.Sample(g_LinearSampler,input.uv0).rgb;
	}
	else emssive = Color_EmssiveColor;
	float3 roughness_and_metallic = TexRoughness.Sample(g_LinearSampler,input.uv0).xyz;
	if(H_Uint_SamplerMask & 8) roughness = roughness_and_metallic.x;
	else roughness = Float_Roughness;
	if(H_Uint_SamplerMask & 16) metallic = roughness_and_metallic.y;
	else metallic = Float_Metallic;
	if (H_Uint_SamplerMask & 32) specular = roughness_and_metallic.z;
	else specular = Float_Specular;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	SurfaceData surface_data;
	InitSurfaceData(input,surface_data.wnormal,surface_data.albedo,surface_data.roughness,surface_data.metallic,surface_data.emssive,surface_data.specular);
	//float3 light = CalculateLight(input.world_pos, surface_data.wnormal);
	float3 light = max(0.0,CalculateLightPBR(surface_data,input.world_pos));
	light.r += 0.000001 * surface_data.metallic;
	light.g += 0.000001 * surface_data.roughness;
	light.b += 0.000001 * surface_data.specular;
	light += surface_data.emssive * 10;
	return float4(float4(light, 1.0f));
	//return float4(float4(surface_data.wnormal, 1.0f));
}
