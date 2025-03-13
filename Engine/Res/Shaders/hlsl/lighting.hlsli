#ifndef __LIGHTING_H__
#define __LIGHTING_H__


//#include "common.hlsli"
#include "cbuffer.hlsli"
#include "brdf.hlsli"
#include "constants.hlsli"
#include "shadow.hlsli"
#include "ltc.hlsli"
#include "voxel_gi.hlsli"


// TEXTURE2D(Albedo)
// TEXTURE2D(Normal)
// TEXTURE2D(Emssive)
// TEXTURE2D(Roughness)
// TEXTURE2D(Metallic)
// TEXTURE2D(Specular)
// TEXTURECUBE(RadianceTex)
// TEXTURECUBE(PrefilterEnvTex)
// TEXTURE2D(IBLLut)
// TEXTURE2D(_OcclusionTex)

Texture2D Albedo : register(t0);
Texture2D Normal : register(t1);
Texture2D Emssive : register(t2);
Texture2D Roughness : register(t3);
Texture2D Metallic : register(t4);
Texture2D Specular : register(t5);
TextureCube RadianceTex : register(t6);
TextureCube PrefilterEnvTex : register(t7);
Texture2D IBLLut : register(t8);
Texture2D _OcclusionTex : register(t9);
//TEXTURE2D(_OcclusionTex)


//#define _MULTI_SCATTER 1
#define PREFILTER_CUBEMAP_NUM 7



float GetDistanceAtt(float distance,float atten_radius)
{
	float atten = 1.0f;
	float inv_sqr_atten = Pow2(atten_radius) / (Pow2(distance) + 1);
	float window_func = Pow2(saturate(1-Pow4(distance / atten_radius)));
	return inv_sqr_atten * window_func;
}
// On the CPU
// float lightAngleScale = 1.0 f / max (0.001f, ( cosInner - cosOuter ));
// float lightAngleOffset = -cosOuter * angleScale ;
// float normalizedLightVector: pos - light_pos
float GetAngleAtt(float3 normalizedLightVector ,float3 lightDir,float lightAngleScale ,float lightAngleOffset)
{
	float cd = dot (lightDir,normalizedLightVector );
	float attenuation = saturate ( cd * lightAngleScale + lightAngleOffset ) ;
	// smooth the transition
	attenuation *= attenuation ;
	return attenuation;
}

float3 GetPointLightIrridance(uint index,float3 world_pos)
{
	float dis = distance(_PointLights[index]._LightPosOrDir,world_pos);
	float atten = 1;
	atten *= GetDistanceAtt(dis,_PointLights[index]._LightParam0.x);
	return _PointLights[index]._LightColor * atten;
}

float3 GetSpotLightIrridance(uint index,float3 world_pos)
{
	float dis = distance(_SpotLights[index]._LightPos,world_pos);
	float3 l = normalize(world_pos - _SpotLights[index]._LightPos);
	float light_angle_scale = _SpotLights[index]._LightAngleScale;
	float light_angle_offset = _SpotLights[index]._LightAngleOffset;
	float atten = 1;
	atten *= GetAngleAtt(l,_SpotLights[index]._LightDir,light_angle_scale,light_angle_offset);
	atten *= GetDistanceAtt(dis,_SpotLights[index]._Rdius);
	return _SpotLights[index]._LightColor * atten;
}

float3 AmbientLighting(SurfaceData surface,ShadingData data,float3 irradiance, float3 prefiltered_color, float2 lut,float ao)
{
#ifdef _MULTI_SCATTER
	//https://bruop.github.io/ibl/
	float3 f0 = lerp(F0, surface.albedo.rgb, surface.metallic);
	// Roughness dependent fresnel, from Fdez-Aguera
    float3 Fr = max((1.0 - surface.roughness).xxx, f0) - f0;
    float3 k_S = f0 + Fr * pow(1.0 - data.nv, 5.0);
    float3 FssEss = k_S * lut.x + lut.y;
	// Multiple scattering, from Fdez-Aguera
    float Ems = (1.0 - (lut.x + lut.y));
    float3 F_avg = f0 + (1.0 - f0) / 21.0;
    float3 FmsEms = Ems * FssEss * F_avg / (1.0 - F_avg * Ems);
    float3 k_D = surface.albedo.rgb * (1.0 - FssEss - FmsEms);
	return (FssEss * prefiltered_color + (FmsEms + k_D) * irradiance) * ao;
#else
	float3 diffuse = (1.0 - surface.metallic) * surface.albedo.rgb;
	diffuse *= irradiance;
	float3 specular = prefiltered_color * EnvBRDF(surface.metallic,surface.albedo.rgb,lut);
	return (diffuse + specular) * ao;
#endif//_MULTI_SCATTER
}
uint GetGlobalDirectionalLightCount()
{
	return min((uint)(_ActiveLightCount.x),MAX_DIRECTIONAL_LIGHT);
}
uint GetGlobalPointLightCount()
{
	return min((uint)(_ActiveLightCount.y),MAX_POINT_LIGHT);
}
uint GetGlobalSpotLightCount()
{
	return min((uint)(_ActiveLightCount.z),MAX_SPOT_LIGHT);
}
uint GetGlobalAreaLightCount()
{
	return min((uint)(_ActiveLightCount.w),MAX_AREA_LIGHT);
}
LightData GetDirectionalLight(uint index)
{
	LightData light;
	light.light_pos = -_DirectionalLights[index]._LightPosOrDir;
	light.light_color = _DirectionalLights[index]._LightColor;
	light.light_dir = light.light_pos;
	light.shadow_atten = 1.0;
	return light;
}
LightData GetPointLight(uint index,float3 world_pos)
{
	LightData light = (LightData)0;
	light.light_pos = _PointLights[index]._LightPosOrDir;
	light.light_dir = -normalize(world_pos - _PointLights[index]._LightPosOrDir);
	light.light_color = _PointLights[index]._LightColor * GetPointLightIrridance(index, world_pos);
	light.shadow_atten = 1.0;
	return light;
}
LightData GetSpotLight(uint index,float3 world_pos)
{
	LightData light = (LightData)0;
	light.light_pos = _SpotLights[index]._LightPos;
	light.light_dir = -normalize(_SpotLights[index]._LightDir);
	light.light_color = _SpotLights[index]._LightColor * GetSpotLightIrridance(index, world_pos);
	light.shadow_atten = 1.0;
	return light;
}

void InitShadingData(LightData light,SurfaceData surface,inout ShadingData data)
{
	float3 hv = normalize(data.view_dir + light.light_dir);
	data.nl = max(saturate(dot(light.light_dir, surface.wnormal)), 0.000001);
	data.vh = max(saturate(dot(data.view_dir, hv)), 0.000001);
	data.lh = max(saturate(dot(light.light_dir, hv)), 0.000001);
	data.nh = max(saturate(dot(surface.wnormal, hv)), 0.000001);
	data.th = dot(surface.tangent, hv);
	data.bh = dot(surface.bitangent, hv);
}


float3 CalculateLightPBR(SurfaceData surface,float3 world_pos,float2 screen_uv)
{
#if defined(DEBUG_GI)
	return VoxelGI(world_pos,surface.wnormal);
#endif//DEBUG_GI
	float3 light = float3(0.0, 0.0, 0.0);
	ShadingData shading_data;
	shading_data.view_dir = normalize(_CameraPos.xyz - world_pos);
	shading_data.nv = saturate(dot(shading_data.view_dir,surface.wnormal));
	LightData light_data;
	light_data.shadow_atten = 1.0;
	float nl = saturate(dot(_DirectionalLights[0]._LightPosOrDir, surface.wnormal));
	float shadow_factor = 1.0;
	light_data.shadow_atten = shadow_factor;
	for (uint i = 0; i < GetGlobalDirectionalLightCount(); i++)
	{
		light_data = GetDirectionalLight(i);
		InitShadingData(light_data,surface,shading_data);
		if(_DirectionalLights[i]._shadowmap_index != -1)
		{
			shadow_factor = ApplyCascadeShadow(nl, world_pos.xyz,_DirectionalLights[i]._ShadowDistance);
		}
		light_data.shadow_atten = shadow_factor;
		light += light_data.shadow_atten * CookTorranceBRDF(surface, shading_data) * shading_data.nl * _DirectionalLights[i]._LightColor;
	}

	shadow_factor = 1.0;
	for (uint j = 0; j < GetGlobalPointLightCount(); j++)
	{
		light_data = GetPointLight(j,world_pos);
		InitShadingData(light_data,surface,shading_data);
		if(_PointLights[j]._shadowmap_index != -1)
		{
			shadow_factor = ApplyShadowPointLight(0.01,_PointLights[j]._LightParam0 * 1.5f,shading_data.nl,
				world_pos,light_data.light_pos,j);
		}
		light_data.shadow_atten = shadow_factor;
		light += light_data.shadow_atten * CookTorranceBRDF(surface, shading_data) * shading_data.nl * light_data.light_color;
	}
	shadow_factor = 1.0;
	for (uint k = 0; k < GetGlobalSpotLightCount(); k++)
	{
		light_data = GetSpotLight(k,world_pos);
		InitShadingData(light_data,surface,shading_data);
		if(_SpotLights[k]._shadowmap_index != -1)
		{ 
			float4 shadow_pos = mul(_SpotLights[k]._shadow_matrix,float4(world_pos,1.0));
			shadow_factor = ApplyShadowAddLight(shadow_pos, nl, world_pos,_SpotLights[k]._shadowmap_index,
				_SpotLights[k]._constant_bias,_SpotLights[k]._slope_bias);
		}
		light_data.shadow_atten = shadow_factor;
		light += light_data.shadow_atten * CookTorranceBRDF(surface, shading_data) * shading_data.nl * light_data.light_color;
	}
	shadow_factor = 1.0;
	for (uint z = 0; z < GetGlobalAreaLightCount(); z++)
	{
		float2 uv = LTC_UV(shading_data.nv,surface.roughness);
		float4 t1 = LTC_LUT1Value(uv);
		float4 t2 = LTC_LUT2Value(uv);
		float3x3 Minv = float3x3(
			float3(t1.x,   0,   t1.z),
			float3(0,      1,   0),
			float3(t1.y,   0,   t1.w)
		);
		float3 rect[4];
		rect[0] = _AreaLights[z]._points[0].xyz;
		rect[1] = _AreaLights[z]._points[1].xyz;
		rect[2] = _AreaLights[z]._points[2].xyz;
		rect[3] = _AreaLights[z]._points[3].xyz;
		float3 diffuse = LTC_Evaluate(surface.wnormal,  shading_data.view_dir, world_pos, Identity(), rect, _AreaLights[z]._is_twosided);
		float3 specular = LTC_Evaluate(surface.wnormal, shading_data.view_dir, world_pos, Minv, rect, _AreaLights[z]._is_twosided);
		specular *= (F0 * t2.x + (1.0f - F0) * t2.y);
		diffuse *= (1.0 - surface.metallic) * surface.albedo.rgb;
		if(_AreaLights[z]._shadowmap_index != -1)
		{ 
			float4 shadow_pos = mul(_AreaLights[z]._shadow_matrix,float4(world_pos,1.0));
			shadow_factor = ApplyShadowAddLight(shadow_pos, nl, world_pos,_AreaLights[z]._shadowmap_index,
				_AreaLights[z]._constant_bias,_AreaLights[z]._slope_bias);
		}
		light += (diffuse + specular) * _AreaLights[z]._LightColor * shadow_factor;
	}
	//indirect light

	float lod = surface.roughness * PREFILTER_CUBEMAP_NUM;
	float3 irradiance = RadianceTex.Sample(g_LinearWrapSampler, surface.wnormal);
	float3 radiance = PrefilterEnvTex.SampleLevel(g_AnisotropicClampSampler, reflect(shading_data.view_dir,surface.wnormal),lod);
	float2 lut = IBLLut.Sample(g_LinearClampSampler,float2(shading_data.nv,surface.roughness)).xy;
	float ao = SAMPLE_TEXTURE2D(_OcclusionTex,g_LinearClampSampler,screen_uv).r;// * g_IndirectLightingIntensity;
	float voxel_shadow = 1.0;//ConeTraceShadow(world_pos + surface.wnormal * 0.05,-_DirectionalLights[0]._LightPosOrDir);
	light += voxel_shadow * AmbientLighting(surface,shading_data,irradiance,radiance,lut,ao);
#ifdef _VOXEL_GI_ON
	light *= voxel_shadow;
	light += VoxelGI(world_pos,surface.wnormal) * surface.albedo.rgb;
#endif
	return light * ao;
}
float3 CalculateLightSimple(SurfaceData surface,float3 world_pos,float2 screen_uv)
{
	float3 light = float3(0.0, 0.0, 0.0);
	float3 view_dir = normalize(_CameraPos.xyz - world_pos);
	ShadingData shading_data = (ShadingData)0;
	LightData light_data;
	light_data.shadow_atten = 1.0;
	float nl = saturate(dot(_DirectionalLights[0]._LightPosOrDir, surface.wnormal));
	float shadow_factor = 1.0;
	light_data.shadow_atten = shadow_factor;
	shading_data.nv = saturate(dot(view_dir,surface.wnormal));//max(saturate(dot(view_dir,surface.wnormal)),0.000001);
	for (uint i = 0; i < GetGlobalDirectionalLightCount(); i++)
	{
		light_data = GetDirectionalLight(i);
		InitShadingData(light_data,surface,shading_data);
		if(_DirectionalLights[i]._shadowmap_index != -1)
		{
			//shadow_factor = ApplyCascadeShadow(nl, world_pos.xyz,_DirectionalLights[i]._ShadowDistance);
		}
		light_data.shadow_atten = shadow_factor;
		light += light_data.shadow_atten * surface.albedo.rgb * shading_data.nl * _DirectionalLights[i]._LightColor;
	}

	shadow_factor = 1.0;
	for (uint j = 0; j < GetGlobalPointLightCount(); j++)
	{
		light_data = GetPointLight(j,world_pos);
		InitShadingData(light_data,surface,shading_data);
		if(_PointLights[j]._shadowmap_index != -1)
		{
			shadow_factor = ApplyShadowPointLight(0.01,_PointLights[j]._LightParam0 * 1.5f,shading_data.nl,
				world_pos,light_data.light_pos,j);
		}
		light_data.shadow_atten = shadow_factor;
		light += light_data.shadow_atten * surface.albedo.rgb * shading_data.nl * _PointLights[j]._LightColor * GetPointLightIrridance(j, world_pos);
	}
	shadow_factor = 1.0;
	for (uint k = 0; k < GetGlobalSpotLightCount(); k++)
	{
		light_data = GetSpotLight(k,world_pos);
		InitShadingData(light_data,surface,shading_data);
		if(_SpotLights[k]._shadowmap_index != -1)
		{ 
			float4 shadow_pos = mul(_SpotLights[k]._shadow_matrix,float4(world_pos,1.0));
			shadow_factor = ApplyShadowAddLight(shadow_pos, nl, world_pos,_SpotLights[k]._shadowmap_index,
				_SpotLights[k]._constant_bias,_SpotLights[k]._slope_bias);
		}
		light_data.shadow_atten = shadow_factor;
		light += (light_data.shadow_atten * surface.albedo.rgb * shading_data.nl * _SpotLights[k]._LightColor * GetSpotLightIrridance(k, world_pos));
	}
	shadow_factor = 1.0;
	for (uint z = 0; z < GetGlobalAreaLightCount(); z++)
	{
		float2 uv = LTC_UV(shading_data.nv,surface.roughness);
		float4 t1 = LTC_LUT1Value(uv);
		float4 t2 = LTC_LUT2Value(uv);
		float3x3 Minv = float3x3(
			float3(t1.x,   0,   t1.z),
			float3(0,      1,   0),
			float3(t1.y,   0,   t1.w)
		);
		float3 rect[4];
		rect[0] = _AreaLights[z]._points[0].xyz;
		rect[1] = _AreaLights[z]._points[1].xyz;
		rect[2] = _AreaLights[z]._points[2].xyz;
		rect[3] = _AreaLights[z]._points[3].xyz;
		float3 diffuse = LTC_Evaluate(surface.wnormal, view_dir, world_pos, Identity(), rect, _AreaLights[z]._is_twosided);
		diffuse *= (1.0 - surface.metallic) * surface.albedo;
		if(_AreaLights[z]._shadowmap_index != -1)
		{ 
			float4 shadow_pos = mul(_AreaLights[z]._shadow_matrix,float4(world_pos,1.0));
			shadow_factor = ApplyShadowAddLight(shadow_pos, nl, world_pos,_AreaLights[z]._shadowmap_index,
				_AreaLights[z]._constant_bias,_AreaLights[z]._slope_bias);
		}
		light += shadow_factor * diffuse * _AreaLights[z]._LightColor;// + specular;
	}
	//indirect light

	// float3 irradiance = RadianceTex.Sample(g_LinearWrapSampler, surface.wnormal);
	// light += (1.0 - surface.metallic) * surface.albedo.rgb * irradiance;
	return light;
	// float lod = surface.roughness * PREFILTER_CUBEMAP_NUM;
	// float3 radiance = 0.0.xxx;//PrefilterEnvTex.SampleLevel(g_AnisotropicClampSampler, reflect(view_dir,surface.wnormal),lod);
	// float2 lut = 0.0.xx;//IBLLut.Sample(g_LinearClampSampler,float2(shading_data.nv,surface.roughness)).xy;
	// float ao = SAMPLE_TEXTURE2D(_OcclusionTex,g_LinearClampSampler,screen_uv).r;// * g_IndirectLightingIntensity;
	// light += AmbientLighting(surface,shading_data,irradiance,radiance,lut,ao) * 0.75f;
	// return light;
}

#endif //__LIGHTING_H__