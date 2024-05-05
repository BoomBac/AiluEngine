#ifndef __SHADOW_H__
#define __SHADOW_H__
#include "common.hlsli"

Texture2D MainLightShadowMap : register(t10);
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
			shadow_factor += lerp(0.0, 0.0625, MainLightShadowMap.SampleCmpLevelZero(g_ShadowSampler, shadow_uv.xy + poissonDisk[random_index] / 2200, depth - z_bias).r);
		}
		//shadow_factor = MainLightShadowMap.SampleCmpLevelZero(g_ShadowSampler, shadow_uv.xy, depth).r;
		return shadow_factor;
	}
#endif