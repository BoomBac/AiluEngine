#ifndef __LIGHTING_H__
#define __LIGHTING_H__


//#include "common.hlsli"
#include "cbuffer.hlsli"
#include "brdf.hlsli"
#include "constants.hlsli"
#include "shadow.hlsli"
#include "ltc.hlsli"



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

#define ACTIVE_DIRECTION_LIGHT_NUM  min((uint)(_ActiveLightCount.x),MAX_DIRECTIONAL_LIGHT)
#define ACTIVE_POINT_LIGHT_NUM      min((uint)(_ActiveLightCount.y),MAX_POINT_LIGHT)
#define ACTIVE_SPOT_LIGHT_NUM       min((uint)(_ActiveLightCount.z),MAX_SPOT_LIGHT)
#define ACTIVE_AREA_LIGHT_NUM       min((uint)(_ActiveLightCount.w),MAX_AREA_LIGHT)

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

float3 CalculateLightPBR(SurfaceData surface,float3 world_pos,float2 screen_uv)
{
	float3 light = float3(0.0, 0.0, 0.0);
	float3 view_dir = normalize(_CameraPos.xyz - world_pos);
	ShadingData shading_data;
	LightData light_data;
	light_data.shadow_atten = 1.0;
	float nl = saturate(dot(_DirectionalLights[0]._LightPosOrDir, surface.wnormal));
	float shadow_factor = 1.0;
	light_data.shadow_atten = shadow_factor;
	shading_data.nv = saturate(dot(view_dir,surface.wnormal));//max(saturate(dot(view_dir,surface.wnormal)),0.000001);
	for (uint i = 0; i < ACTIVE_DIRECTION_LIGHT_NUM; i++)
	{
		float3 light_dir = -_DirectionalLights[i]._LightPosOrDir;
		float3 hv = normalize(view_dir + light_dir);

		shading_data.nl = max(saturate(dot(light_dir, surface.wnormal)), 0.000001);
		shading_data.vh = max(saturate(dot(view_dir, hv)), 0.000001);
		shading_data.lh = max(saturate(dot(light_dir, hv)), 0.000001);
		shading_data.nh = max(saturate(dot(surface.wnormal, hv)), 0.000001);
		shading_data.th = dot(surface.tangent, hv);
		shading_data.bh = dot(surface.bitangent, hv);
		light_data.light_dir = light_dir;
		light_data.light_color = _DirectionalLights[i]._LightColor;
		light_data.light_pos = _DirectionalLights[i]._LightPosOrDir;
		//return surface.wnormal.xyzz;
		// float alpha = surface.roughness;
		// float anisotropy = surface.anisotropy * 2 - 1;
		// float alpha_x = max(alpha * alpha * (1.0 + anisotropy), 0.001);
        // float alpha_y = max(alpha * alpha * (1.0 - anisotropy), 0.001);

		// return D_GGXaniso(alpha_x,alpha_y,shading_data.nh,shading_data.th,shading_data.bh).xxxx;
		if(_DirectionalLights[i]._ShadowDataIndex != -1)
		{
			shadow_factor = ApplyCascadeShadow(nl, world_pos.xyz,_DirectionalLights[i]._ShadowDistance);
		}
		light_data.shadow_atten = shadow_factor;
		light += light_data.shadow_atten * CookTorranceBRDF(surface, shading_data) * shading_data.nl * _DirectionalLights[i]._LightColor;
	}

	shadow_factor = 1.0;
	for (uint j = 0; j < ACTIVE_POINT_LIGHT_NUM; j++)
	{
		float3 light_dir = -normalize(world_pos - _PointLights[j]._LightPosOrDir);
		float3 hv = normalize(view_dir + light_dir);
		shading_data.nl = max(saturate(dot(light_dir, surface.wnormal)), 0.000001);
		shading_data.vh = max(saturate(dot(view_dir, hv)), 0.000001);
		shading_data.lh = max(saturate(dot(light_dir, hv)), 0.000001);
		shading_data.nh = max(saturate(dot(surface.wnormal, hv)), 0.000001);
		shading_data.th = dot(surface.tangent, hv);
		shading_data.bh = dot(surface.bitangent, hv);
		light_data.light_dir = light_dir;
		light_data.light_color = _PointLights[j]._LightColor;
		light_data.light_pos = _PointLights[j]._LightPosOrDir;
		if(_PointLights[j]._ShadowDataIndex != -1)
		{
			shadow_factor = ApplyShadowPointLight(10,_PointLights[j]._LightParam0 * 1.5f,shading_data.nl,
				world_pos,light_data.light_pos,j);
		}
		light_data.shadow_atten = shadow_factor;
		light += light_data.shadow_atten * CookTorranceBRDF(surface, shading_data) * shading_data.nl * _PointLights[j]._LightColor * GetPointLightIrridance(j, world_pos);
	}
	shadow_factor = 1.0;
	for (uint k = 0; k < ACTIVE_SPOT_LIGHT_NUM; k++)
	{
		float3 light_dir = -normalize(_SpotLights[k]._LightDir);
		float3 hv = normalize(view_dir + light_dir);
		shading_data.nl = max(saturate(dot(light_dir, surface.wnormal)), 0.000001);
		shading_data.vh = max(saturate(dot(view_dir, hv)), 0.000001);
		shading_data.lh = max(saturate(dot(light_dir, hv)), 0.000001);
		shading_data.nh = max(saturate(dot(surface.wnormal, hv)), 0.000001);
		shading_data.th = dot(surface.tangent, hv);
		shading_data.bh = dot(surface.bitangent, hv);
		light_data.light_dir = light_dir;
		light_data.light_color = _SpotLights[k]._LightColor;
		light_data.light_pos = _SpotLights[k]._LightPos;
		if(_SpotLights[k]._ShadowDataIndex != -1)
		{ 
			uint shadow_index = _SpotLights[k]._ShadowDataIndex;
			float4 shadow_pos = TransformFromWorldToLightSpace(shadow_index,world_pos.xyz);
			shadow_factor = ApplyShadowAddLight(shadow_pos, nl, world_pos.xyz,k);
		}
		light_data.shadow_atten = shadow_factor;
		light += (light_data.shadow_atten * CookTorranceBRDF(surface, shading_data) * shading_data.nl * _SpotLights[k]._LightColor * GetSpotLightIrridance(k, world_pos));
	}
	shadow_factor = 1.0;
	for (uint z = 0; z < ACTIVE_AREA_LIGHT_NUM; z++)
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
		float3 specular = LTC_Evaluate(surface.wnormal, view_dir, world_pos, Minv, rect, _AreaLights[z]._is_twosided);
		specular *= (F0 * t2.x + (1.0f - F0) * t2.y);
		diffuse *= 1.0 - surface.metallic;
		light += (diffuse + specular) * _AreaLights[z]._LightColor;// + specular;
	}
	//indirect light

	float lod = surface.roughness * PREFILTER_CUBEMAP_NUM;
	float3 irradiance = RadianceTex.Sample(g_LinearWrapSampler, surface.wnormal);
	float3 radiance = PrefilterEnvTex.SampleLevel(g_AnisotropicClampSampler, reflect(view_dir,surface.wnormal),lod);
	float2 lut = IBLLut.Sample(g_LinearClampSampler,float2(shading_data.nv,surface.roughness)).xy;
	float ao = SAMPLE_TEXTURE2D(_OcclusionTex,g_LinearClampSampler,screen_uv).r;// * g_IndirectLightingIntensity;
	light += AmbientLighting(surface,shading_data,irradiance,radiance,lut,ao);
	return light;
}
#endif //__LIGHTING_H__