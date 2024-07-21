#ifndef __STANDARD_LIT_COMMON__
#define __STANDARD_LIT_COMMON__

#include "common.hlsli"
#include "lighting.hlsli"

PerMaterialCBufferBegin
	float4 BaseColor; //0
	float4 EmssiveColor; //16
	float4 SpecularColor; //32
	float RoughnessValue; //36
	float MetallicValue; //40
	uint SamplerMask; //44
	uint MaterialID;
PerMaterialCBufferEnd


void InitSurfaceData(StandardPSInput input,
out float3 wnormal,out float4 albedo,out float roughness,out float metallic,out float3 emssive,out float3 specular)
{
	if(SamplerMask & 2)
	{
		wnormal = Normal.Sample(g_LinearWrapSampler, input.uv0).xyz * 2.0 - 1.0;
		wnormal = normalize(mul(wnormal,input.btn));
	}
	else
		wnormal = normalize(input.normal);
	if(SamplerMask & 4)
	{
		emssive = Emssive.Sample(g_LinearWrapSampler, input.uv0).rgb;
	}
	else 
		emssive = EmssiveColor;
	if(SamplerMask & 8) 
		roughness = Roughness.Sample(g_LinearWrapSampler, input.uv0).r;
	else 
		roughness = RoughnessValue;
	if(SamplerMask & 16) 
		metallic = Metallic.Sample(g_LinearWrapSampler, input.uv0).r;
	else 
		metallic = MetallicValue;
	if (SamplerMask & 32) 
		specular = Specular.Sample(g_LinearWrapSampler, input.uv0).rgb;
	else 
		specular = SpecularColor;
	if (SamplerMask & 1)
	{
		albedo = Albedo.Sample(g_LinearWrapSampler, input.uv0);
	}
	else
		albedo = BaseColor;
}
#endif