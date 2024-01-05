//info bein
//name: shaders
//vert: VSMain
//pixel: PSMain
//Cull: Front
//Queue: Opaque
//Blend: Src,OneMinusSrc
//Properties
//{
//	Albedo("Albedo",Texture2D) = "white"
//	Normal("Normal",Texture2D) = "white"
//	Emssive("Emssive",Texture2D) = "white"
//	Roughness("Roughness",Texture2D) = "white"
//	Metallic("Metallic",Texture2D) = "white"
//	Specular("Specular",Texture2D) = "white"
//	BaseColor("BaseColor",Color) = (1,1,1,0)
//	EmssiveColor("Emssive",Color) = (0,0,0,0)
//	SpecularColor("Specular",Color) = (0,0,0,0)
//	RoughnessValue("Roughness",Range(0,1)) = 0
//	MetallicValue("Metallic",Range(0,1)) = 0
//}
//info end

#include "input.hlsl"
#include "cbuffer.hlsl"
#include "common.hlsl"
#include "lighting.hlsl"



Texture2D Albedo : register(t0);
Texture2D Normal : register(t1);
Texture2D Emssive : register(t2);
Texture2D Roughness : register(t3);
Texture2D Metallic : register(t4);
Texture2D Specular : register(t5);

SamplerState g_LinearSampler : register(s0);

cbuffer SceneMaterialBuffer : register(b1)
{
	float4 		BaseColor; //0
	float4 		EmssiveColor;//16
	float4		SpecularColor;//32
	float	 	RoughnessValue;//36
	float	 	MetallicValue;//40
	uint 		SamplerMask;//44
};



#pragma vs
PSInput VSMain(VSInput v);


void InitSurfaceData(PSInput input,out float3 wnormal,out float4 albedo,out float roughness,out float metallic,out float3 emssive,out float3 specular)
{
	if(SamplerMask & 2)
	{
		wnormal = Normal.Sample(g_LinearSampler, input.uv0).xyz;
		wnormal = normalize(mul(wnormal* 2.0 - 1.0,input.btn));
	}
	else wnormal = input.normal;
	if(SamplerMask & 4)
	{
		emssive = Emssive.Sample(g_LinearSampler, input.uv0).rgb;
	}
	else emssive = EmssiveColor;
	if(SamplerMask & 8) roughness = Roughness.Sample(g_LinearSampler, input.uv0).r;
	else roughness = RoughnessValue;
	if(SamplerMask & 16) metallic = Metallic.Sample(g_LinearSampler, input.uv0).r;
	else metallic = MetallicValue;
	if (SamplerMask & 32) specular = Specular.Sample(g_LinearSampler, input.uv0).rgb;
	else specular = SpecularColor;
	if (SamplerMask & 1)
		albedo = Albedo.SampleLevel(g_LinearSampler, input.uv0, lerp(0.0, 7.0, roughness));
	else
		albedo = BaseColor;
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
	light += surface_data.emssive; 
	GammaCorrect(light,2.2f);
#ifdef TEST
	return float4(light, surface_data.albedo.a);
#else
	return float4(0.0,1.0,0.0,1.0);
#endif
}

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
