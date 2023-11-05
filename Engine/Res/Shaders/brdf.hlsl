#ifndef __BRDF_H__
#define __BRDF_H__
#include "constants.hlsl"
//#include "cbuffer.hlsl"
#include "input.hlsl"

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

float3 SchlickFresnel(float3 H, float3 V, float3 f)
{
	return f + (float3(1.f, 1.f, 1.f) - f) * pow(1.f - dot(H, V), 5.f);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
	float3 f = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness),F0);
	return F0 + (f, - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float3 Fresnel(float3 n,float3 v,float3 f0)
{
    float v1 = (1.0 - pow(dot(n,v),5));
    return lerp(float3(v1,v1,v1),float3(1.0,1.0,1.0),f0);
}

float3 CookTorranceBRDF(SurfaceData surface,float3 view_dir,float3 light_dir)
{
	float3 base_color = surface.albedo.rgb;
	float roughness = surface.roughness;
	float metallic = surface.metallic;
	float3 N = normalize(surface.wnormal);
	float3 L = normalize(light_dir);
	float3 V = normalize(view_dir);
	float G = GeometrySmith(N, V, L, roughness);
	float3 H = normalize(L + V);
	float D = DistributionGGX(N, H, lerp(0.02, 1, roughness * roughness));
	//float3 F = SchlickFresnel(H, V, float3(0.4, 0.4, 0.4));
    float3 f0 = lerp(float3(0.04,0.04,0.04),surface.albedo.rgb,float3(surface.metallic,surface.metallic,surface.metallic));
	float3 F = Fresnel(N, V,f0);
	float3 up = D * G * F;
	float a = max(dot(N, V), 0.f);
	float b = max(dot(N, L), 0.f);
	float3 bottom = 4.f * a * b + 0.0000001f;
	float3 specular = up / bottom;
	float3 KS = F;
	float3 kD = float3(1.f, 1.f, 1.f) - KS;
	kD *= 1.f - metallic;
	// return kD * base_color / PI + KS * specular;
    // return D * float3(1.0,1.0,1.0);
    return surface.albedo + D * 0.00001;
}

#endif