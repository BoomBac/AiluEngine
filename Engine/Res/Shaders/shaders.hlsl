//info bein
//name: shaders
//vert: VSMain
//pixel: PSMain
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
#include "input.hlsli"
#include "cbuffer.hlsli"
#include "lighting.hlsli"


Texture2D MainLightShadowMap : register(t7);

CBufBegin
	float4 BaseColor; //0
	float4 EmssiveColor; //16
	float4 SpecularColor; //32
	float RoughnessValue; //36
	float MetallicValue; //40
	uint SamplerMask; //44
CBufEnd


#pragma vs
PSInput VSMain(VSInput v);


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
float Random(float4 seed4)
{
	float dp = dot(seed4, float4(12.9898, 78.233, 45.164, 94.673));
	return frac(sin(dp) * 43758.5453);
}

	float ApplyShadow
	(
	float4 shadow_coord, float nl, float3 world_pos)
{
		// static const float2 poissonDisk[4] =
		// {
		// 	float2(-0.94201624, -0.39906216),
		// 	float2(0.94558609, -0.76890725),
		// 	float2(-0.094184101, -0.92938870),
		// 	float2(0.34495938, 0.29387760)
		// };

		static const float2 poissonDisk[16] =
		{
			float2(-0.94201624, -0.39906216),
			float2(0.94558609, -0.76890725),
			float2(-0.094184101, -0.92938870),
			float2(0.34495938, 0.29387760),
			float2(-0.91588581, 0.45771432),
			float2(-0.81544232, -0.87912464),
			float2(-0.38277543, 0.27676845),
			float2(0.97484398, 0.75648379),
			float2(0.44323325, -0.97511554),
			float2(0.53742981, -0.47373420),
			float2(-0.26496911, -0.41893023),
			float2(0.79197514, 0.19090188),
			float2(-0.24188840, 0.99706507),
			float2(-0.81409955, 0.91437590),
			float2(0.19984126, 0.78641367),
			float2(0.14383161, -0.14100790)
		};

	
		float z_bias = 0.005 * tan(acos(nl));
		z_bias = clamp(z_bias, 0, 0.002);
		shadow_coord.xyz /= shadow_coord.w;
		float depth = saturate(shadow_coord.z);
		float2 shadow_uv;
		shadow_uv.x = shadow_coord.x * 0.5f + 0.5f;
		shadow_uv.y = shadow_coord.y * -0.5f + 0.5f;
		float shadow_factor = 0.0;
		for (int i = 0; i < 8; i++)
		{
			uint random_index = uint(16.0 * Random(float4(world_pos.xyy, i))) % 16;
			shadow_factor += lerp(0.0, 0.0625, MainLightShadowMap.SampleCmpLevelZero(g_ShadowSampler, shadow_uv.xy + poissonDisk[random_index] / 1800, depth - z_bias).r);
		}
		//shadow_factor = MainLightShadowMap.SampleCmpLevelZero(g_ShadowSampler, shadow_uv.xy, depth).r;
		return shadow_factor;
	}

	float4 PSMain
	(PSInput input):
	SV_TARGET
{
		SurfaceData surface_data;
		InitSurfaceData(input, surface_data.wnormal, surface_data.albedo, surface_data.roughness, surface_data.metallic, surface_data.emssive, surface_data.specular);
		float nl = saturate(dot(_DirectionalLights[0]._LightPosOrDir, surface_data.wnormal));
		float shadow_factor = ApplyShadow(input.shadow_pos, nl, input.world_pos);
	// if (shadow_factor == 0)
	// 	return float4(0.0, 0.0, 0.0, 1.0);
		float3 light = max(0.0, CalculateLightPBR(surface_data, input.world_pos));
		light.r += 0.000001 * surface_data.metallic;
		light.g += 0.000001 * surface_data.roughness;
		light.b += 0.000001 * surface_data.specular;
		light += surface_data.emssive;

	//float shadow_factor = MainLightShadowMap.Sample(g_LinearBorderSampler, shadow_uv.xy).r; 
	//if (shadow_factor <= depth)
	//	return float4(0.0,0.0,0.0,1.0); 
		light *= lerp(0.2,1.0,shadow_factor);
//		GammaCorrect(light, 2.2f);
#ifdef TEST
	return float4(light,1.0);
#else
		return float4(0.0, 1.0, 0.0, 1.0);
#endif
	}

	PSInput VSMain
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
