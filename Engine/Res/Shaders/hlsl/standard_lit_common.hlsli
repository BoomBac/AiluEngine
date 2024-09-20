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
	float  _Anisotropy;
	uint   _SamplerMask; //44
	uint   _MaterialID;
	float  _AlphaCulloff;
	//for shadow
	uint 	shadow_index;
	float3 _point_light_wpos;
	float   padding;
	float4 _shadow_params;
PerMaterialCBufferEnd


void InitSurfaceData(StandardPSInput input,out SurfaceData surface)
{
	if (_SamplerMask & 1)
	{
		surface.albedo = _AlbedoTex.Sample(g_LinearWrapSampler, input.uv0);
	}
	else
		surface.albedo = _AlbedoValue;
	if(_SamplerMask & 2)
	{
		surface.wnormal = _NormalTex.Sample(g_LinearWrapSampler, input.uv0).xyz * 2.0 - 1.0;
		surface.wnormal = normalize(mul(surface.wnormal,input.btn));
	}
	else
		surface.wnormal = normalize(input.normal);
	if(_SamplerMask & 4)
	{
		surface.emssive = _EmissionTex.Sample(g_LinearWrapSampler, input.uv0).rgb;
	}
	else 
		surface.emssive = _EmissionValue;
	if(_SamplerMask & 8)
	{
		float2 roughness_and_metallic = _RoughnessMetallicTex.Sample(g_LinearWrapSampler, input.uv0).rg;
		surface.roughness = roughness_and_metallic.r;
		surface.metallic = roughness_and_metallic.g;
	}
	else
	{
		surface.roughness = _RoughnessValue;
		surface.metallic = _MetallicValue;
	}
	if (_SamplerMask & 16)
		surface.specular = _SpecularTex.Sample(g_LinearWrapSampler, input.uv0).rgb;
	else
	surface.specular = _SpecularValue;
	surface.anisotropy = _Anisotropy; 
	surface.tangent =   normalize(input.btn[2]);
	surface.bitangent = normalize(input.btn[1]);
}
void InitSurfaceDataCheckboard(StandardPSInput input,out SurfaceData surface)
{
	float2 uv10 = input.uv0 * 10;
	float2 uv100 = uv10 * 10;
	float3 c0 = lerp(0.4,1,SAMPLE_TEXTURE2D(_AlbedoTex,g_LinearWrapSampler,uv10).b).rrr;
	float4 t1 = SAMPLE_TEXTURE2D(_AlbedoTex,g_LinearWrapSampler,uv100);
	float camera_fadeout =  1 - saturate(distance(_CameraPos.xyz,input.world_pos) / 10);
	float c1 = 1 - (lerp(0.4,1,t1.g) * t1.r * camera_fadeout);
	c0 *= c1.xxx;
	surface.albedo = c0.xxxx;
	surface.wnormal = _NormalTex.Sample(g_LinearWrapSampler, uv100 * 2).xyz * 2.0 - 1.0;
	surface.wnormal *= float3(0.3,0.3,1);
	surface.wnormal = lerp(float3(0,0,1),surface.wnormal,camera_fadeout);
	surface.wnormal = normalize(mul(surface.wnormal,input.btn));
	surface.emssive = 0.0.rrr;
	surface.roughness = lerp(0,1,c0);
	surface.metallic = 0;
	surface.specular = 0.0.xxx;
	surface.anisotropy = 0;
	surface.tangent = normalize(input.btn[2]);
	surface.bitangent = normalize(input.btn[1]);
}
#endif