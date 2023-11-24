#ifndef __BRDF_H__
#define __BRDF_H__
#include "constants.hlsl"
//#include "cbuffer.hlsl"
#include "input.hlsl"

#define F0_AIELECTRICS float3(0.04,0.04,0.04)


float GeometrySchlickGGX(float NdotV, float roughness)
{
	float r = (roughness + 1.0);
	float k = (r * r) / 8.0;
	float num = NdotV;
	float denom = NdotV * (1.0 - k) + k;
	return num / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	float ggx1 = GeometrySchlickGGX(NdotL, roughness);	
	return ggx1 * ggx2;
}

float GeometrySmith(float nv,float nl, float roughness)
{
	float ggx2 = GeometrySchlickGGX(nv, roughness);
	float ggx1 = GeometrySchlickGGX(nl, roughness);	
	return ggx1 * ggx2;
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH = max(dot(N, H), 0.f);
	float NdotH2 = NdotH * NdotH;
	float up = a2;
	float bottom = NdotH2 * (a2 - 1.f) + 1.f;
	bottom = PI * bottom * bottom;
	return up / bottom;
}

float DistributionGGX(float nh, float roughness)
{
	float a = lerp(0.002,1.0,roughness);
	float a2 = a * a;
	float nh2 = nh * nh;
	float up = a2;
	float bottom = nh2 * (a2 - 1.f) + 1.f;
	bottom = PI * bottom * bottom;
	return up / bottom;
}


// float3 SchlickFresnel(float3 H, float3 V, float3 f)
// {
// 	return f + (float3(1.f, 1.f, 1.f) - f) * pow(1.f - dot(H, V), 5.f);
// }

// float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
// {
// 	float3 f = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness),F0);
// 	return F0 + (f, - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
// }

// float3 Fresnel(float3 n,float3 v,float3 f0)
// {
//     float v1 = (1.0 - pow(dot(n,v),5));
//     return lerp(float3(v1,v1,v1),float3(1.0,1.0,1.0),f0);
// }
float3 Fresnel(float3 base_color,float metallic,float vh)
{
	float3 F0 = lerp(F0_AIELECTRICS,base_color,metallic);
	return F0 + (F3_WHITE - F0) * exp2((-5.55473 * vh - 6.98316) * vh);
}

float3 DirectDiffuse(SurfaceData surface,ShadingData shading_data,LightData light_data)
{
	return surface.albedo.rgb / PI;
}

float3 DirectSpecular(SurfaceData surface,ShadingData shading_data,LightData light_data,out float3 F)
{
	float D = DistributionGGX(shading_data.nh,surface.roughness);
	float G = GeometrySmith(shading_data.nv,shading_data.nl,surface.roughness);
	F = Fresnel(surface.albedo.rgb,surface.metallic,shading_data.vh);
	return (D * G * F * 0.25) / (shading_data.nv * shading_data.nl);
}

float3 CookTorranceBRDF(SurfaceData surface,ShadingData shading_data,LightData light_data)
{
	float3 F;
	float3 specular = DirectSpecular(surface,shading_data,light_data,F);
	float3 kd = (F3_WHITE - F) * (F3_WHITE - surface.metallic);
	float3 diffuse = DirectDiffuse(surface,shading_data,light_data) * kd;
	return diffuse + specular;
}

#endif