#ifndef __LIGHTING_H__
#define __LIGHTING_H__

#include "common.hlsli"
#include "cbuffer.hlsli"
#include "brdf.hlsli"
#include "constants.hlsli"
#include "shadow.hlsli"
#include "standard_lit_common.hlsli"

TextureCube RadianceTex : register(t5);
TextureCube PrefilterEnvTex : register(t6);
Texture2D   IBLLut : register(t7);

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

float3 CalculateLightPBR(SurfaceData surface,float3 world_pos)
{
	float3 light = float3(0.0, 0.0, 0.0);
	float3 view_dir = normalize(_CameraPos.xyz - world_pos);
	ShadingData shading_data;
	LightData light_data;
	float nl = saturate(dot(_DirectionalLights[0]._LightPosOrDir, surface.wnormal));
	shading_data.nv = saturate(dot(view_dir,surface.wnormal));//max(saturate(dot(view_dir,surface.wnormal)),0.000001);
	for (uint i = 0; i < MAX_DIRECTIONAL_LIGHT; i++)
	{
		light_data.shadow_atten = 1.0;
		float3 light_dir = -_DirectionalLights[i]._LightPosOrDir;
		float3 hv = normalize(view_dir + light_dir);
		shading_data.nl = max(saturate(dot(light_dir, surface.wnormal)), 0.000001);
		shading_data.vh = max(saturate(dot(view_dir, hv)), 0.000001);
		shading_data.lh = max(saturate(dot(light_dir, hv)), 0.000001);
		shading_data.nh = max(saturate(dot(surface.wnormal, hv)), 0.000001);
		light_data.light_dir = light_dir;
		light_data.light_color = _DirectionalLights[i]._LightColor;
		light_data.light_pos = _DirectionalLights[i]._LightPosOrDir;
		if(_DirectionalLights[i]._ShadowDataIndex != -1)
		{
			light_data.shadow_atten = ApplyCascadeShadow(nl, world_pos.xyz,_DirectionalLights[i]._ShadowDistance);
		}
		light += light_data.shadow_atten * CookTorranceBRDF(surface, shading_data) * shading_data.nl * _DirectionalLights[i]._LightColor;
	}
	light_data.shadow_atten = 1.0f;
	for (uint j = 0; j < MAX_POINT_LIGHT; j++)
	{
		light_data.shadow_atten = 1.0;
		float3 light_dir = -normalize(world_pos - _PointLights[j]._LightPosOrDir);
		float3 hv = normalize(view_dir + light_dir);
		shading_data.nl = max(saturate(dot(light_dir, surface.wnormal)), 0.000001);
		shading_data.vh = max(saturate(dot(view_dir, hv)), 0.000001);
		shading_data.lh = max(saturate(dot(light_dir, hv)), 0.000001);
		shading_data.nh = max(saturate(dot(surface.wnormal, hv)), 0.000001);
		light_data.light_dir = light_dir;
		light_data.light_color = _PointLights[j]._LightColor;
		light_data.light_pos = _PointLights[j]._LightPosOrDir;
		if(_PointLights[j]._ShadowDataIndex != -1)
		{
			light_data.shadow_atten = ApplyShadowPointLight(10,_PointLights[j]._LightParam0 * 1.5f,shading_data.nl,
				world_pos,light_data.light_pos,j);
		}
		light += light_data.shadow_atten * CookTorranceBRDF(surface, shading_data) * shading_data.nl * _PointLights[j]._LightColor * GetPointLightIrridance(j, world_pos);
	}
	for (uint k = 0; k < MAX_SPOT_LIGHT; k++)
	{
		light_data.shadow_atten = 1.0;
		float3 light_dir = -normalize(_SpotLights[k]._LightDir);
		float3 hv = normalize(view_dir + light_dir);
		shading_data.nl = max(saturate(dot(light_dir, surface.wnormal)), 0.000001);
		shading_data.vh = max(saturate(dot(view_dir, hv)), 0.000001);
		shading_data.lh = max(saturate(dot(light_dir, hv)), 0.000001);
		shading_data.nh = max(saturate(dot(surface.wnormal, hv)), 0.000001);
		light_data.light_dir = light_dir;
		light_data.light_color = _SpotLights[k]._LightColor;
		light_data.light_pos = _SpotLights[k]._LightPos;
		if(_SpotLights[k]._ShadowDataIndex != -1)
		{ 
			uint shadow_index = _SpotLights[k]._ShadowDataIndex;
			float4 shadow_pos = TransformFromWorldToLightSpace(shadow_index,world_pos.xyz);
			light_data.shadow_atten = ApplyShadowAddLight(shadow_pos, nl, world_pos.xyz,k);
		}
		light += light_data.shadow_atten * CookTorranceBRDF(surface, shading_data) * shading_data.nl * _SpotLights[k]._LightColor * GetSpotLightIrridance(k, world_pos);
	}
	//indirect light
	float3 diffuse_color = surface.albedo.rgb * (1.0 - DIELECTRIC_SPECULAR) * (1.0 - surface.metallic);
	float3 F0 = lerp(DIELECTRIC_SPECULAR.xxx, surface.albedo.rgb, surface.metallic);
	float lod = surface.roughness * 7;
	float3 irradiance = RadianceTex.Sample(g_LinearWrapSampler, surface.wnormal);
	float3 radiance = PrefilterEnvTex.SampleLevel(g_AnisotropicClampSampler, reflect(view_dir,surface.wnormal),lod);
	float2 envBRDF = IBLLut.Sample(g_LinearClampSampler,float2(shading_data.nv,surface.roughness)).xy;

	// // Roughness dependent fresnel, from Fdez-Aguera
    // float3 Fr = max((1.0 - surface.roughness).xxx, F0) - F0;
    // float3 k_S = F0 + Fr * pow(1.0 - shading_data.nv, 5.0);
    // float3 FssEss = k_S * envBRDF.x + envBRDF.y;
	// // Multiple scattering, from Fdez-Aguera
    // float Ems = (1.0 - (envBRDF.x + envBRDF.y));
    // float3 F_avg = F0 + (1.0 - F0) / 21.0;
    // float3 FmsEms = Ems * FssEss * F_avg / (1.0 - F_avg * Ems);
    // float3 k_D = diffuse_color * (1.0 - FssEss - FmsEms);
	float3 FssEss = F0 * envBRDF.x + envBRDF.y;
	light += (FssEss * radiance + diffuse_color * irradiance) * g_IndirectLightingIntensity;
	return light; 
}
#endif //__LIGHTING_H__