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

float3 CaclulateDirectionalLight(uint index, float3 normal, float3 view_dir)
{
	float3 light_dir = normalize(-_DirectionalLights[index]._LightPosOrDir.xyz);
	float diffuse = max(dot(light_dir, normal), 0.0);
	float3 half_dir = normalize(light_dir + view_dir);
	float specular = pow(max(0.0, dot(half_dir, normal)), 256);
	return saturate(diffuse * _DirectionalLights[index]._LightColor + specular);
}

#define LinearAtten
float3 CaclulatPointlLight(uint index, float3 normal, float3 pos, float3 view_dir)
{
	float3 pixel_to_light = _PointLights[index]._LightPosOrDir - pos;
	float nl = dot(normal, normalize(pixel_to_light));
	float distance = length(pixel_to_light);
	float atten = 0;
	float3 half_dir = normalize(pixel_to_light + view_dir);
#if defined(QuadAtten)
	atten = 10 / (1 + _PointLights[index]._LightParam.x * distance + _PointLights[index]._LightParam.b * distance * _PointLights[index]._LightParam.b * distance)
#elif defined(InvAtten)
	atten = 10 / distance * distance;
#elif defined(LinearAtten)
	atten = (1.0 - saturate(distance / (0.00001 + _PointLights[index]._LightParam0)));
#endif
	float diffuse = max(0.0, nl) * atten;
	float specular = pow(max(0.0, dot(half_dir, normal)), 256) * atten;
	return saturate(diffuse * _PointLights[index]._LightColor + specular);
}

float3 CaclulatSpotlLight(uint index, float3 normal, float3 pos, float3 view_dir)
{
	float3 pixel_to_light = normalize(_SpotLights[index]._LightPos - pos);
	float nl = dot(normal, normalize(-_SpotLights[index]._LightDir));
	float dis = distance(_SpotLights[index]._LightPos , pos);
	float atten = 0;
	float3 half_dir = normalize(-_SpotLights[index]._LightDir + view_dir);
#if defined(QuadAtten)
	atten = 10 / (1 + _PointLights[index]._LightParam.x * distance + _PointLights[index]._LightParam.b * distance * _PointLights[index]._LightParam.b * distance)
#elif defined(InvAtten)
	atten = 10 / distance * distance;
#elif defined(LinearAtten)
	atten = (1.0 - saturate(dis / (0.00001 + _SpotLights[index]._Rdius)));
#endif
	float angle_cos = dot(-pixel_to_light, _SpotLights[index]._LightDir);
	float inner_cos = cos(_SpotLights[index]._InnerAngle / 2 * ToRadius);
	float outer_cos = cos(_SpotLights[index]._OuterAngle / 2 * ToRadius);
	atten *= step(outer_cos, angle_cos);
	float angle_lerp = (inner_cos - angle_cos) / (inner_cos - outer_cos + 0.00001);
	float angle_atten = lerp(1.0, 0.0, angle_lerp);
	atten *= angle_atten;
	float diffuse = max(0.0, nl) * atten;
	float specular = pow(max(0.0, dot(half_dir, normal)), 256) * atten;
	return saturate(diffuse * _SpotLights[index]._LightColor + specular);
}

float3 CaclulateDirectionalLightPBR(uint index, float3 normal, float3 view_dir)
{
	float3 light_dir = normalize(-_DirectionalLights[index]._LightPosOrDir.xyz);
	float diffuse = max(dot(light_dir, normal), 0.0);
	float3 half_dir = normalize(light_dir + view_dir);
	float specular = pow(max(0.0, dot(half_dir, normal)), 256);
	return saturate(diffuse * _DirectionalLights[index]._LightColor + specular);
}

float3 GetPointLightIrridance(uint index,float3 world_pos)
{
	float dis = distance(_PointLights[index]._LightPosOrDir,world_pos);
	float atten = 0;
#if defined(QuadAtten)
	atten = 10 / (1 + _PointLights[index]._LightParam.x * dis + _PointLights[index]._LightParam.b * dis * _PointLights[index]._LightParam.b * distance)
#elif defined(InvAtten)
	atten = 10 / dis * dis;
#elif defined(LinearAtten)
	atten = (1.0 - saturate(dis / (0.00001 + _PointLights[index]._LightParam0)));
#endif
	return _PointLights[index]._LightColor * atten;
}

float3 GetSpotLightIrridance(uint index,float3 world_pos)
{
	float dis = distance(_SpotLights[index]._LightPos,world_pos);
	float3 pixel_to_light = normalize(_SpotLights[index]._LightPos - world_pos);
	float atten = 0;
#if defined(QuadAtten)
	atten = 10 / (1 + _SpotLights[index]._LightParam.x * dis + _SpotLights[index]._LightParam.b * dis * _SpotLights[index]._LightParam.b * distance)
#elif defined(InvAtten)
	atten = 10 / dis * dis;
#elif defined(LinearAtten)
	atten = (1.0 - saturate(dis / (0.00001 + _SpotLights[index]._Rdius)));
#endif
	float angle_cos = dot(-pixel_to_light, _SpotLights[index]._LightDir);
	float inner_cos = cos(_SpotLights[index]._InnerAngle / 2 * ToRadius);
	float outer_cos = cos(_SpotLights[index]._OuterAngle / 2 * ToRadius);
	atten *= step(outer_cos, angle_cos);
	float angle_lerp = (inner_cos - angle_cos) / (inner_cos - outer_cos + 0.00001);
	float angle_atten = lerp(1.0, 0.0, angle_lerp);
	atten *= angle_atten;
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
		float3 hv = normalize(view_dir +  light_dir);
		shading_data.nl = max(saturate(dot(light_dir,surface.wnormal)),0.000001);
		shading_data.vh = max(saturate(dot(view_dir,hv)),0.000001);
		shading_data.lh = max(saturate(dot(light_dir,hv)),0.000001);
		shading_data.nh = max(saturate(dot(surface.wnormal,hv)),0.000001);
		light_data.light_dir = light_dir;
		light_data.light_color = _DirectionalLights[i]._LightColor;
		light_data.light_pos = _DirectionalLights[i]._LightPosOrDir;
		light += CookTorranceBRDF(surface, shading_data, light_data) * shading_data.nl * _DirectionalLights[i]._LightColor;
	}
	for (uint j = 0; j < MAX_POINT_LIGHT; j++)
	{
		float3 light_dir = -normalize(world_pos - _PointLights[j]._LightPosOrDir);
		float3 hv = normalize(view_dir +  light_dir);
		shading_data.nl = max(saturate(dot(light_dir,surface.wnormal)),0.000001);
		shading_data.vh = max(saturate(dot(view_dir,hv)),0.000001);
		shading_data.lh = max(saturate(dot(light_dir,hv)),0.000001);
		shading_data.nh = max(saturate(dot(surface.wnormal,hv)),0.000001);
		light_data.light_dir = light_dir;
		light_data.light_color = _PointLights[j]._LightColor;
		light_data.light_pos = _PointLights[j]._LightPosOrDir;
		light += CookTorranceBRDF(surface, shading_data, light_data) * shading_data.nl * _PointLights[j]._LightColor * GetPointLightIrridance(j,world_pos);
	} 
	for (uint k = 0; k < MAX_SPOT_LIGHT; k++)
	{
		float3 light_dir = -normalize(_SpotLights[k]._LightDir);
		float3 hv = normalize(view_dir +  light_dir);
		shading_data.nl = max(saturate(dot(light_dir,surface.wnormal)),0.000001);
		shading_data.vh = max(saturate(dot(view_dir,hv)),0.000001);
		shading_data.lh = max(saturate(dot(light_dir,hv)),0.000001);
		shading_data.nh = max(saturate(dot(surface.wnormal,hv)),0.000001);
		light_data.light_dir = light_dir;
		light_data.light_color = _SpotLights[k]._LightColor;
		light_data.light_pos = _SpotLights[k]._LightPos;
		light += CookTorranceBRDF(surface, shading_data, light_data) * shading_data.nl * _SpotLights[k]._LightColor * GetSpotLightIrridance(k,world_pos);
	}
	//indirect light
	float3 irradiance = DiffuseIBL.Sample(g_LinearWrapSampler, surface.wnormal);
	float3 F0 = lerp(F0_AIELECTRICS, surface.albedo.rgb, surface.metallic);
	float3 ks = FresnelSchlickRoughness(saturate(shading_data.nv), F0,surface.roughness);
	float3 kd = F3_WHITE - ks;
	float3 diffuse = irradiance * surface.albedo.rgb;
	//light += diffuse * kd;
	return light;
}

float3 CalculateLight
	(
	float3 pos, float3 normal)
{
	float3 light = float3(0.0, 0.0, 0.0);
	float3 view_dir = normalize(_CameraPos.xyz - pos);
	for (uint i = 0; i < MAX_DIRECTIONAL_LIGHT; i++)
	{
		light += CaclulateDirectionalLight(i, normal, view_dir);
	}
	for (uint j = 0; j < MAX_POINT_LIGHT; j++)
	{
		light += CaclulatPointlLight(j, normal, pos, view_dir);
	}
	for (uint k = 0; k < MAX_SPOT_LIGHT; k++)
	{
		light += CaclulatSpotlLight(k, normal, pos, view_dir);
	}
	return light;

}
#endif //__LIGHTING_H__