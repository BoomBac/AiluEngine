#ifndef __STANDARD_LIT_COMMON__
#define __STANDARD_LIT_COMMON__

#include "common.hlsli"
#include "input.hlsli"
#include "material_sampling.hlsli"

Texture2D _AlbedoTex : register(t0);
Texture2D _NormalTex : register(t1);
Texture2D _RoughnessTex : register(t2);
Texture2D _MetallicTex : register(t5);
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
	float _IOR;
	float _Transmission;
	float4 _AttenuationColor;
	float _AttenuationDistance;
    uint _AlbedoTexSamplerIndex;
    uint _NormalTexSamplerIndex;
    uint _RoughnessTexSamplerIndex;
    uint _MetallicTexSamplerIndex;
    uint _SpecularTexSamplerIndex;
    uint _EmissionTexSamplerIndex;
    uint _TextureViewMipBiasMask;
PerMaterialCBufferEnd


float MaterialViewMipBias(uint slot)
{
    return ResolveMaterialMipBias(_TextureViewMipBiasMask, slot, _TextureManualMipBias, _TextureMipBias);
}

void InitSurfaceData(StandardPSInput input,out SurfaceData surface)
{
	surface.albedo = _AlbedoValue;
	if (_SamplerMask & 1)
	{
		surface.albedo *= _AlbedoTex.SampleBias(g_material_samplers[_AlbedoTexSamplerIndex], input.uv0, MaterialViewMipBias(0u));
	}
	if(_SamplerMask & 2)
	{
		surface.wnormal = _NormalTex.SampleBias(g_material_samplers[_NormalTexSamplerIndex], input.uv0, MaterialViewMipBias(1u)).xyz * 2.0 - 1.0;
		surface.wnormal = normalize(mul(surface.wnormal,input.btn));
	}
	else
		surface.wnormal = normalize(input.normal);
	if(_SamplerMask & 4)
	{
		surface.emssive = _EmissionTex.SampleBias(g_material_samplers[_EmissionTexSamplerIndex], input.uv0, MaterialViewMipBias(5u)).rgb;
#ifdef EMISSION_TEXTURE_FACTOR
        surface.emssive *= _EmissionValue.rgb;
#endif
	}
	else 
		surface.emssive = _EmissionValue.rgb;
    surface.roughness = _RoughnessValue;
    surface.metallic = _MetallicValue;
    if (_SamplerMask & 8)
        surface.roughness *= _RoughnessTex.SampleBias(g_material_samplers[_RoughnessTexSamplerIndex], input.uv0, MaterialViewMipBias(2u)).r;
    if (_SamplerMask & 64)
        surface.metallic *= _MetallicTex.SampleBias(g_material_samplers[_MetallicTexSamplerIndex], input.uv0, MaterialViewMipBias(3u)).r;
	if (_SamplerMask & 16)
		surface.specular = _SpecularTex.SampleBias(g_material_samplers[_SpecularTexSamplerIndex], input.uv0, MaterialViewMipBias(4u)).rgb;
	else
	surface.specular = _SpecularValue.rgb;
	surface.anisotropy = _Anisotropy; 
	surface.tangent =   normalize(input.btn[2]);
	surface.bitangent = normalize(input.btn[1]);
}
void InitSurfaceDataCheckboard(StandardPSInput input,out SurfaceData surface)
{
	float2 uv10 = input.uv0 * 10;
	float2 uv100 = uv10 * 10;
	float3 c0 = lerp(0.4,1,_AlbedoTex.SampleBias(g_material_samplers[_AlbedoTexSamplerIndex], uv10, MaterialViewMipBias(0u)).b).rrr;
	float4 t1 = _AlbedoTex.SampleBias(g_material_samplers[_AlbedoTexSamplerIndex], uv100, MaterialViewMipBias(0u));
	float camera_fadeout =  1 - saturate(distance(_CameraPos.xyz,input.world_pos) / 10);
	float c1 = 1 - (lerp(0.4,1,t1.g) * t1.r * camera_fadeout);
	c0 *= c1.xxx;
	surface.albedo = c0.xxxx;
	surface.wnormal = _NormalTex.SampleBias(g_material_samplers[_NormalTexSamplerIndex], uv100 * 2, MaterialViewMipBias(1u)).xyz * 2.0 - 1.0;
	surface.wnormal *= float3(0.3,0.3,1);
	surface.wnormal = lerp(float3(0,0,1),surface.wnormal,camera_fadeout);
	surface.wnormal = normalize(mul(surface.wnormal,input.btn));
	surface.emssive = 0.0.rrr;
	surface.roughness = lerp(0,1,c0.r);
	surface.metallic = 0;
	surface.specular = 0.0.xxx;
	surface.anisotropy = 0;
	surface.tangent = normalize(input.btn[2]);
	surface.bitangent = normalize(input.btn[1]);
}
#endif
