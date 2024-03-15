//info bein
//name: defered_standard_lit
//vert: GBufferVSMain
//pixel: GBufferPSMain
//Cull: Back
//Queue: Opaque
//Blend: Src,OneMinusSrc
//Properties
//{
//	Albedo("Albedo",Texture2D) = "white"
//	Normal("Normal",Texture2D) = "white"
//	Emssive("Emssive",Texture2D) = "white"
//	Roughness("Roughness",Texture2D) = "white"
//	Metallic("Metallic",Texture2D) = "white"
//	Specular("Specular",Texture2D) = "white"
//	BaseColor("BaseColor",Color) = (1,1,1,0)
//	EmssiveColor("Emssive",Color) = (0,0,0,0)
//	SpecularColor("Specular",Color) = (0,0,0,0)
//	RoughnessValue("Roughness",Range(0,1)) = 0
//	MetallicValue("Metallic",Range(0,1)) = 0
//}
//info end

#include "common.hlsli"
#include "cbuffer.hlsli"
#include "input.hlsli"
#include "lighting.hlsli"



CBufBegin
	float4 BaseColor; //0
	float4 EmssiveColor; //16
	float4 SpecularColor; //32
	float RoughnessValue; //36
	float MetallicValue; //40
	uint SamplerMask; //44
CBufEnd



void InitSurfaceData(PSInput input,out float3 wnormal,out float4 albedo,out float roughness,out float metallic,out float3 emssive,out float3 specular)
{
	if(SamplerMask & 2)
	{
		wnormal = Normal.Sample(g_LinearWrapSampler, input.uv0).xyz;
		wnormal = normalize(mul(wnormal* 2.0 - 1.0,input.btn));
	}
	else
		wnormal = normalize(input.normal);
	if(SamplerMask & 4)
	{
		emssive = Emssive.Sample(g_LinearWrapSampler, input.uv0).rgb;
	}
	else emssive = EmssiveColor;
	if(SamplerMask & 8) roughness = Roughness.Sample(g_LinearWrapSampler, input.uv0).r;
	else roughness = RoughnessValue;
	if(SamplerMask & 16) metallic = Metallic.Sample(g_LinearWrapSampler, input.uv0).r;
	else metallic = MetallicValue;
	if (SamplerMask & 32) specular = Specular.Sample(g_LinearWrapSampler, input.uv0).rgb;
	else specular = SpecularColor;
	if (SamplerMask & 1)
		albedo = Albedo.SampleLevel(g_LinearWrapSampler, input.uv0, lerp(0.0, 7.0, roughness));
	else
		albedo = BaseColor;
}

struct GBuffer
{
	float2 packed_normal : SV_Target0;
	float4 color_roughness : SV_Target1;
	float4 specular_metallic : SV_Target2;
};

GBuffer GBufferPSMain
	(PSInput input) :
	SV_TARGET
{
	SurfaceData surface_data;
	InitSurfaceData(input, surface_data.wnormal, surface_data.albedo, surface_data.roughness, surface_data.metallic, surface_data.emssive, surface_data.specular);
	GBuffer output;
	//output.packed_normal = float4(normalize(surface_data.wnormal),1.0);
	surface_data.wnormal = normalize(surface_data.wnormal);
	output.packed_normal = surface_data.wnormal.xy;
	output.color_roughness = float4(surface_data.albedo.rgb,surface_data.roughness);
	output.specular_metallic = float4(surface_data.specular,surface_data.metallic);
	return output;
}

PSInput GBufferVSMain
	(VSInput v)
{
	PSInput result;
	result.position = TransformToClipSpace(v.position);
	result.normal = v.normal;
	result.uv0 = v.uv0;
	float3 B = TransformNormal(cross(v.tangent.xyz, v.normal));
	float3 T = TransformNormal(v.tangent.xyz);
	float3 N = TransformNormal(v.normal);
	result.btn = float3x3(T, B, N);
	result.normal = N;
	result.world_pos = TransformToWorldSpace(v.position);
	result.shadow_pos = TransformFromWorldToLightSpace(0, result.world_pos);
	return result;
}



// 	float4 PSMain
// 	(PSInput input):
// 	SV_TARGET
// {
// 		SurfaceData surface_data;
// 		InitSurfaceData(input, surface_data.wnormal, surface_data.albedo, surface_data.roughness, surface_data.metallic, surface_data.emssive, surface_data.specular);
// 		float nl = saturate(dot(_DirectionalLights[0]._LightPosOrDir, surface_data.wnormal));
// 		float shadow_factor = ApplyShadow(input.shadow_pos, nl, input.world_pos);
// 	// if (shadow_factor == 0)
// 	// 	return float4(0.0, 0.0, 0.0, 1.0);
// 		float3 light = max(0.0, CalculateLightPBR(surface_data, input.world_pos));
// 		light.r += 0.000001 * surface_data.metallic;
// 		light.g += 0.000001 * surface_data.roughness;
// 		light.b += 0.000001 * surface_data.specular;
// 		light += surface_data.emssive;

// 	//float shadow_factor = MainLightShadowMap.Sample(g_LinearBorderSampler, shadow_uv.xy).r; 
// 	//if (shadow_factor <= depth)
// 	//	return float4(0.0,0.0,0.0,1.0); 
// 		light *= lerp(0.2,1.0,shadow_factor);
// //		GammaCorrect(light, 2.2f);
// #ifdef TEST
// 	return float4(light,1.0);
// #else
// 		return float4(0.0, 1.0, 0.0, 1.0);
// #endif
// 	}

// 	PSInput VSMain
// 	(VSInput v)
// {
// 		PSInput result;
// 		result.position = TransformToClipSpace(v.position);
// 		result.normal = v.normal;
// 		result.uv0 = v.uv0;
// 		float3 B = TransformNormal(cross(v.tangent.xyz, v.normal));
// 		float3 T = TransformNormal(v.tangent.xyz);
// 		float3 N = TransformNormal(v.normal);
// 		result.btn = float3x3(T, B, N);  
// 		result.normal = N; 
// 		result.world_pos = TransformToWorldSpace(v.position);
// 		result.shadow_pos = TransformFromWorldToLightSpace(0, result.world_pos); 
// 		return result;
// 	}
