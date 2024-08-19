#ifndef __STANDARD_LIT_COMMON__
#define __STANDARD_LIT_COMMON__

#include "common.hlsli"
#include "input.hlsli"

Texture2D _AlbedoTex : register(t0);
Texture2D _NormalTex : register(t1);
Texture2D _RoughnessMetallicTex : register(t2);
Texture2D _SpecularTex : register(t3);
Texture2D _EmissionTex : register(t4);

PerMaterialCBufferBegin
	float4 _AlbedoValue; //0
	float4 _EmissionValue; //16
	float4 _SpecularValue; //32
	float  _RoughnessValue;
	float  _MetallicValue; //40
	uint   _SamplerMask; //44
	uint   _MaterialID;
	float  _AlphaCulloff;
PerMaterialCBufferEnd


void InitSurfaceData(StandardPSInput input,
out float3 wnormal,out float4 albedo,out float roughness,out float metallic,out float3 emssive,out float3 specular)
{
	if (_SamplerMask & 1)
	{
		albedo = _AlbedoTex.Sample(g_LinearWrapSampler, input.uv0);
	}
	else
		albedo = _AlbedoValue;
	if(_SamplerMask & 2)
	{
		wnormal = _NormalTex.Sample(g_LinearWrapSampler, input.uv0).xyz * 2.0 - 1.0;
		wnormal = normalize(mul(wnormal,input.btn));
	}
	else
		wnormal = normalize(input.normal);
	if(_SamplerMask & 4)
	{
		emssive = _EmissionTex.Sample(g_LinearWrapSampler, input.uv0).rgb;
	}
	else 
		emssive = _EmissionValue;
	if(_SamplerMask & 8)
	{
		float2 roughness_and_metallic = _RoughnessMetallicTex.Sample(g_LinearWrapSampler, input.uv0).rg;
		roughness = roughness_and_metallic.r;
		metallic = roughness_and_metallic.g;
	}
	else
	{
		roughness = _RoughnessValue;
		metallic = _MetallicValue;
	}
	if (_SamplerMask & 16) 
		specular = _SpecularTex.Sample(g_LinearWrapSampler, input.uv0).rgb;
	else 
		specular = _SpecularValue;
	albedo.rgb += emssive.rgb * 2.0f;
}
#endif