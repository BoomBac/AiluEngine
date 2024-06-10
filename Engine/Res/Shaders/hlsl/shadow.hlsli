#ifndef __SHADOW_H__
#define __SHADOW_H__
#include "common.hlsli"

Texture2D MainLightShadowMap : register(t10);
//Texture2D AddLightShadowMap : register(t11);
Texture2DArray AddLightShadowMaps : register(t11);
//TextureCube PointLightShadowMap : register(t12);
TextureCubeArray PointLightShadowMaps : register(t12);

float Random(float4 seed4)
{
	float dp = dot(seed4, float4(12.9898, 78.233, 45.164, 94.673));
	return frac(sin(dp) * 43758.5453);
}

float ShadowDistaanceAtten(float dis, float max_dis) 
{
	if(dis < 0.8 * max_dis)
		return 1;
	return lerp(1,0,(dis - 0.8*max_dis) / (0.2*max_dis));//lerp(0,1,(dis - 0.8*max_dis) / 0.2*max_dis);
//   float t = saturate(dis / (max_dis * 1.2));
//   return 1.0 - 0.5 * t * (3.0 - 2.0 * t);
}

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

	float ApplyShadow(float4 shadow_coord, float nl, float3 world_pos,float shadow_distance)
	{
		float dis = distance(_CameraPos.xyz,world_pos);
		if(dis > shadow_distance)
			return 1.0;	
		float z_bias = 0.025 * tan(acos(nl));
		z_bias = clamp(z_bias, 0, 0.001);
		shadow_coord.xyz /= shadow_coord.w;
		float depth = saturate(shadow_coord.z);
		float2 shadow_uv;
		shadow_uv.x = shadow_coord.x * 0.5f + 0.5f;
		shadow_uv.y = shadow_coord.y * -0.5f + 0.5f;
		float shadow_factor = 0.0;
		for (int i = 0; i < 8; i++)
		{
			uint random_index = uint(8.0 * Random(float4(world_pos.xyy, i))) % 8;
			//uint random_index = i;
			shadow_factor += lerp(0.0, 0.125, MainLightShadowMap.SampleCmpLevelZero(g_ShadowSampler, shadow_uv.xy + poissonDisk[random_index] * 0.00048828125, depth - z_bias).r);
		}
		shadow_factor =  (1.0 - shadow_factor) * ShadowDistaanceAtten(dis,shadow_distance);
		return 1.0 - shadow_factor;// *= lerp(1,0.0,saturate(dis/_MainLightShadowDistance));
	}

	float ApplyShadowAddLight(float4 shadow_coord, float nl, float3 world_pos,float shadow_distance,int shadow_index)
	{
		float dis = distance(_CameraPos.xyz,world_pos);
		if(dis > shadow_distance)
			return 1.0;	
		float z_bias = 0.0025 * tan(acos(nl));
		z_bias = clamp(z_bias, 0, 0.005);
		shadow_coord.xyz /= shadow_coord.w;
		float depth = saturate(shadow_coord.z);
		float2 shadow_uv;
		shadow_uv.x = shadow_coord.x * 0.5f + 0.5f;
		shadow_uv.y = shadow_coord.y * -0.5f + 0.5f;
		float shadow_factor = 0.0;
		for (int i = 0; i < 16; i++)
		{
			uint random_index = uint(16.0 * Random(float4(world_pos.xyy, i))) % 16;
			//uint random_index = i;
			shadow_factor += lerp(0.0, 0.0625, AddLightShadowMaps.SampleCmpLevelZero(g_ShadowSampler, float3(shadow_uv.xy + poissonDisk[random_index] * 0.0009765625,shadow_index), depth - z_bias).r);
		}
		shadow_factor =  (1.0 - shadow_factor) * ShadowDistaanceAtten(dis,shadow_distance);
		return 1.0 - shadow_factor;// *= lerp(1,0.0,saturate(dis/_MainLightShadowDistance));
	}

	float ApplyShadowPointLight(float shadow_near,float shadow_far ,float nl, float3 world_pos,float3 light_pos,int shadow_index)
	{
		float dis = distance(light_pos,world_pos);
		if(dis > shadow_far)
			return 1.0;	
		float z_bias = 0.0025 * tan(acos(nl));
		z_bias = clamp(z_bias, 0, 0.5);
		float3 shaodw_dir = world_pos - light_pos;
		float linear_z = (dis - shadow_near) / (shadow_far - shadow_near);
		// float2 shadow_uv;
		// shadow_uv.x = shadow_coord.x * 0.5f + 0.5f;
		// shadow_uv.y = shadow_coord.y * -0.5f + 0.5f;
		float shadow_factor = 0.0;
		for (int i = 0; i < 1; i++)
		{
			uint random_index = uint(16.0 * Random(float4(world_pos.xyy, i))) % 16;
			//uint random_index = i;
			// shadow_factor += lerp(0.0, 1.0, PointLightShadowMap.SampleCmpLevelZero(g_ShadowSampler, 
			// 	shaodw_dir, linear_z).r);
			shadow_factor += PointLightShadowMaps.SampleCmpLevelZero(g_ShadowSampler, float4(shaodw_dir,(float)shadow_index),linear_z - z_bias);
		}
		//shadow_factor =  (1.0 - shadow_factor) * ShadowDistaanceAtten(dis,shadow_far);
		return shadow_factor;// *= lerp(1,0.0,saturate(dis/_MainLightShadowDistance));
	}
#endif