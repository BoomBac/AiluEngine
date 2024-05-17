#ifndef __LIGHTING_H__
#define __LIGHTING_H__

#include "common.hlsli"
#include "cbuffer.hlsli"
#include "brdf.hlsli"
#include "constants.hlsli"

Texture2D Albedo : register(t0);
Texture2D Normal : register(t1);
Texture2D Emssive : register(t2);
Texture2D Roughness : register(t3);
Texture2D Metallic : register(t4);
Texture2D Specular : register(t5);
TextureCube DiffuseIBL : register(t6);
TextureCube SkyBox : register(t7);
Texture2D IBLLut : register(t8);

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
float getAngleAtt(float3 normalizedLightVector ,float3 lightDir,float lightAngleScale ,float lightAngleOffset)
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
	atten *= getAngleAtt(l,_SpotLights[index]._LightDir,light_angle_scale,light_angle_offset);
	atten *= GetDistanceAtt(dis,_SpotLights[index]._Rdius);
	return _SpotLights[index]._LightColor * atten;
}

float3 CalculateLightPBR(SurfaceData surface,float3 world_pos)
{
	float3 light = float3(0.0, 0.0, 0.0);
	float3 view_dir = normalize(_CameraPos.xyz - world_pos);
	ShadingData shading_data;
	LightData light_data;
	shading_data.nv = max(saturate(dot(view_dir,surface.wnormal)),0.000001);
	for (uint i = 0; i < MAX_DIRECTIONAL_LIGHT; i++)
	{
		float3 light_dir = -_DirectionalLights[i]._LightPosOrDir;
		float3 hv = normalize(view_dir + light_dir);
		shading_data.nl = max(saturate(dot(light_dir, surface.wnormal)), 0.000001);
		shading_data.vh = max(saturate(dot(view_dir, hv)), 0.000001);
		shading_data.lh = max(saturate(dot(light_dir, hv)), 0.000001);
		shading_data.nh = max(saturate(dot(surface.wnormal, hv)), 0.000001);
		light_data.light_dir = light_dir;
		light_data.light_color = _DirectionalLights[i]._LightColor;
		light_data.light_pos = _DirectionalLights[i]._LightPosOrDir;
		light += CookTorranceBRDF(surface, shading_data) * shading_data.nl * _DirectionalLights[i]._LightColor;
	}
	for (uint j = 0; j < MAX_POINT_LIGHT; j++)
	{
		float3 light_dir = -normalize(world_pos - _PointLights[j]._LightPosOrDir);
		float3 hv = normalize(view_dir + light_dir);
		shading_data.nl = max(saturate(dot(light_dir, surface.wnormal)), 0.000001);
		shading_data.vh = max(saturate(dot(view_dir, hv)), 0.000001);
		shading_data.lh = max(saturate(dot(light_dir, hv)), 0.000001);
		shading_data.nh = max(saturate(dot(surface.wnormal, hv)), 0.000001);
		light_data.light_dir = light_dir;
		light_data.light_color = _PointLights[j]._LightColor;
		light_data.light_pos = _PointLights[j]._LightPosOrDir;
		light += CookTorranceBRDF(surface, shading_data) * shading_data.nl * _PointLights[j]._LightColor * GetPointLightIrridance(j, world_pos);
	}
	for (uint k = 0; k < MAX_SPOT_LIGHT; k++)
	{
		float3 light_dir = -normalize(_SpotLights[k]._LightDir);
		float3 hv = normalize(view_dir + light_dir);
		shading_data.nl = max(saturate(dot(light_dir, surface.wnormal)), 0.000001);
		shading_data.vh = max(saturate(dot(view_dir, hv)), 0.000001);
		shading_data.lh = max(saturate(dot(light_dir, hv)), 0.000001);
		shading_data.nh = max(saturate(dot(surface.wnormal, hv)), 0.000001);
		light_data.light_dir = light_dir;
		light_data.light_color = _SpotLights[k]._LightColor;
		light_data.light_pos = _SpotLights[k]._LightPos;
		light += CookTorranceBRDF(surface, shading_data) * shading_data.nl * _SpotLights[k]._LightColor * GetSpotLightIrridance(k, world_pos);
	}
	//indirect light
	float3 irradiance = DiffuseIBL.Sample(g_LinearWrapSampler, surface.wnormal);
	float3 F0 = lerp(F0_AIELECTRICS, surface.albedo.rgb, surface.metallic);
	float3 ks = F_SchlickRoughness(shading_data.nv,F0,surface.roughness);
	float3 kd = 1.0 - ks;
	float ao = 1.0;
	float3 diffuse = ao * kd * irradiance * surface.albedo.rgb;
	//float3 specular = SkyBox.SampleLevel(g_LinearWrapSampler, surface.wnormal,);

	float lod             = lerp(0,8,surface.roughness);
	float3 prefilteredColor = SkyBox.SampleLevel(g_LinearWrapSampler, reflect(-view_dir,surface.wnormal),lod);
	float2 envBRDF          = IBLLut.SampleLevel(g_LinearWrapSampler,float2(lerp(0,0.99,surface.roughness),lerp(0,0.99,shading_data.nv)),0).xy;
	envBRDF.x = pow(envBRDF.x,1/2.2);
	envBRDF.y = pow(envBRDF.y,1/2.2);
	float3 specular = prefilteredColor * (ks * envBRDF.x + envBRDF.y);

	light += (specular + prefilteredColor);
	return light; 
}
#endif //__LIGHTING_H__