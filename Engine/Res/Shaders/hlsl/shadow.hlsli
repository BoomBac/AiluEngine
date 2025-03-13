#ifndef __SHADOW_H__
#define __SHADOW_H__
#include "common.hlsli"

Texture2DArray MainLightShadowMap : register(t10);
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

float GetShadowAtten(float dis,float max_dis,float fade_out_factor)
{
	return fade_out_factor - dis / max_dis * fade_out_factor;
}

float GetShadowDepthBias(float nl,float constant_bias,float slope_bias)
{
	float c = nl;
	float s = sqrt(1 - c*c); // sin(acos(L·N))
	float t = s / c;    // tan(acos(L·N))
	float bias = constant_bias + clamp(t,0,slope_bias);
	/* or
	float offsetMod = 1.0 - clamp(dot(N, L), 0, 1)
	float offset = minOffset + maxSlopeOffset * offsetMod;
	*/
	return bias;
}

static const float2 poissonDisk[64] =
{
	float2(-0.5119625f, -0.4827938f),
    float2(-0.2171264f, -0.4768726f),
    float2(-0.7552931f, -0.2426507f),
    float2(-0.7136765f, -0.4496614f),
    float2(-0.5938849f, -0.6895654f),
    float2(-0.3148003f, -0.7047654f),
    float2(-0.42215f, -0.2024607f),
    float2(-0.9466816f, -0.2014508f),
    float2(-0.8409063f, -0.03465778f),
    float2(-0.6517572f, -0.07476326f),
    float2(-0.1041822f, -0.02521214f),
    float2(-0.3042712f, -0.02195431f),
    float2(-0.5082307f, 0.1079806f),
    float2(-0.08429877f, -0.2316298f),
    float2(-0.9879128f, 0.1113683f),
    float2(-0.3859636f, 0.3363545f),
    float2(-0.1925334f, 0.1787288f),
    float2(0.003256182f, 0.138135f),
    float2(-0.8706837f, 0.3010679f),
    float2(-0.6982038f, 0.1904326f),
    float2(0.1975043f, 0.2221317f),
    float2(0.1507788f, 0.4204168f),
    float2(0.3514056f, 0.09865579f),
    float2(0.1558783f, -0.08460935f),
    float2(-0.0684978f, 0.4461993f),
    float2(0.3780522f, 0.3478679f),
    float2(0.3956799f, -0.1469177f),
    float2(0.5838975f, 0.1054943f),
    float2(0.6155105f, 0.3245716f),
    float2(0.3928624f, -0.4417621f),
    float2(0.1749884f, -0.4202175f),
    float2(0.6813727f, -0.2424808f),
    float2(-0.6707711f, 0.4912741f),
    float2(0.0005130528f, -0.8058334f),
    float2(0.02703013f, -0.6010728f),
    float2(-0.1658188f, -0.9695674f),
    float2(0.4060591f, -0.7100726f),
    float2(0.7713396f, -0.4713659f),
    float2(0.573212f, -0.51544f),
    float2(-0.3448896f, -0.9046497f),
    float2(0.1268544f, -0.9874692f),
    float2(0.7418533f, -0.6667366f),
    float2(0.3492522f, 0.5924662f),
    float2(0.5679897f, 0.5343465f),
    float2(0.5663417f, 0.7708698f),
    float2(0.7375497f, 0.6691415f),
    float2(0.2271994f, -0.6163502f),
    float2(0.2312844f, 0.8725659f),
    float2(0.4216993f, 0.9002838f),
    float2(0.4262091f, -0.9013284f),
    float2(0.2001408f, -0.808381f),
    float2(0.149394f, 0.6650763f),
    float2(-0.09640376f, 0.9843736f),
    float2(0.7682328f, -0.07273844f),
    float2(0.04146584f, 0.8313184f),
    float2(0.9705266f, -0.1143304f),
    float2(0.9670017f, 0.1293385f),
    float2(0.9015037f, -0.3306949f),
    float2(-0.5085648f, 0.7534177f),
    float2(0.9055501f, 0.3758393f),
    float2(0.7599946f, 0.1809109f),
    float2(-0.2483695f, 0.7942952f),
    float2(-0.4241052f, 0.5581087f),
    float2(-0.1020106f, 0.6724468f),
};

// float ApplyShadow(float4 shadow_coord, float nl, float3 world_pos,float shadow_distance)
// {
// 	float dis = distance(_CameraPos.xyz,world_pos);
// 	if(dis > shadow_distance)
// 		return 1.0;	
// 	float z_bias = 0.025 * tan(acos(nl));
// 	z_bias = clamp(z_bias, 0, 0.001);
// 	shadow_coord.xyz /= shadow_coord.w;
// 	float depth = saturate(shadow_coord.z);
// 	float2 shadow_uv;
// 	shadow_uv.x = shadow_coord.x * 0.5f + 0.5f;
// 	shadow_uv.y = shadow_coord.y * -0.5f + 0.5f;
// 	float shadow_factor = 0.0;
// 	for (int i = 0; i < 8; i++)
// 	{
// 		uint random_index = uint(8.0 * Random(float4(world_pos.xyy, i))) % 8;
// 		//uint random_index = i;
// 		shadow_factor += lerp(0.0, 0.125, MainLightShadowMap.SampleCmpLevelZero(g_ShadowSampler, shadow_uv.xy + poissonDisk[random_index] * 0.00048828125, depth - z_bias).r);
// 	}
// 	shadow_factor =  (1.0 - shadow_factor) * ShadowDistaanceAtten(dis,shadow_distance);
// 	return 1.0 - shadow_factor;// *= lerp(1,0.0,saturate(dis/_MainLightShadowDistance));
// }

#define _ShadowMapTexelSize 0.0009765625// 1/1024
#define _SOFT_SAMPLE_COUNT 16


float ApplyCascadeShadow(float nl, float3 world_pos,float shadow_distance)
{
	float dis = distance(_CameraPos.xyz,world_pos);
	// if(dis > shadow_distance)
	// 	return 1.0;	
	// float z_bias = 0.025 * tan(acos(nl));
	// z_bias = clamp(z_bias, 0, 0.001);
	//if dis~[0~max_dis*fade_out] out 1
	//else [max_dis*fade_out~max_dis] out 1->0
	float z_bias = GetShadowDepthBias(nl,_DirectionalLights[0]._constant_bias,_DirectionalLights[0]._slope_bias);
	float atten = saturate((_CascadeShadowParams.y * (1.0 - dis * _CascadeShadowParams.z)));
	int cascade_count = (int)_CascadeShadowParams.x;
	for(int cascade_index = 0;cascade_index < cascade_count;cascade_index++)
	{
		if(SqrDistance(world_pos,_CascadeShadowSplit[cascade_index].xyz) <= _CascadeShadowSplit[cascade_index].w)
		{
			float4 shadow_coord = mul(_CascadeShadowMatrix[cascade_index],float4(world_pos,1.0));
			if (shadow_coord.x < -1 || shadow_coord.x > 1 || shadow_coord.y < -1 || shadow_coord.y > 1)
				return 1.0f;
			shadow_coord.xyz /= shadow_coord.w;
			float depth = saturate(shadow_coord.z);
		#if defined(_REVERSED_Z)
			depth += z_bias;
		#else
			depth -= z_bias;
		#endif
			float2 shadow_uv;
			shadow_uv.x = shadow_coord.x * 0.5f + 0.5f;
			shadow_uv.y = shadow_coord.y * -0.5f + 0.5f;
			float shadow_factor = 0.0;
			for (int i = 0; i < _SOFT_SAMPLE_COUNT; i++)
			{
				//uint random_index = uint(8.0 * Random(float4(world_pos.xyy, i))) % 8;
				// shadow_factor += lerp(0.0, 0.125, MainLightShadowMap.SampleCmpLevelZero(g_ShadowSampler, 
				// 	float3(shadow_uv.xy + poissonDisk[i] * _ShadowMapTexelSize,cascade_index), depth - z_bias).r);
				shadow_factor += 1 - MainLightShadowMap.SampleCmpLevelZero(g_ShadowSampler, 
					float3(shadow_uv.xy + poissonDisk[i] * _ShadowMapTexelSize,cascade_index), depth).r;
			}
			shadow_factor /= _SOFT_SAMPLE_COUNT;
			shadow_factor =  (1.0 - shadow_factor);
			return 1.0 - shadow_factor * atten;
		}
	}
	return 1.0;
}

float ApplyShadowAddLight(float4 shadow_coord,float nl, float3 world_pos,int shadowmap_index,float const_bias,float slope_bias)
{
	float dis = distance(_CameraPos.xyz,world_pos);
	// if(dis > shadow_distance)
	// 	return 1.0;	
	float z_bias = GetShadowDepthBias(nl,const_bias,slope_bias);
	shadow_coord.xyz /= shadow_coord.w;
	float depth = saturate(shadow_coord.z);
	float2 shadow_uv;
	shadow_uv.x = shadow_coord.x * 0.5f + 0.5f;
	shadow_uv.y = shadow_coord.y * -0.5f + 0.5f;
	float shadow_factor = 0.0; 
#if defined(_REVERSED_Z)
	depth += z_bias;
#else
	depth -= z_bias;
#endif
	for (int i = 0; i < 16; i++)
	{
		uint random_index = uint(16.0 * Random(float4(world_pos.xyy, i))) % 16;
		//uint random_index = i;
		shadow_factor += lerp(0.0, 0.0625, 1 - AddLightShadowMaps.SampleCmpLevelZero(g_ShadowSampler, float3(shadow_uv.xy + 
			poissonDisk[random_index] * 0.0009765625,shadowmap_index), depth).r);
	}
	shadow_factor =  (1.0 - shadow_factor);// * ShadowDistaanceAtten(dis,shadow_distance);
	return 1.0 - shadow_factor;// *= lerp(1,0.0,saturate(dis/_MainLightShadowDistance));
}


float ApplyShadowPointLight(float shadow_near,float shadow_far ,float nl, float3 world_pos,float3 light_pos,int shadow_index)
{
	float dis = distance(light_pos,world_pos);
	if(dis > shadow_far)
		return 1.0;
	float z_bias = GetShadowDepthBias(nl,_PointLights[shadow_index]._constant_bias,_PointLights[shadow_index]._slope_bias);
	//z_bias = clamp(z_bias, 0, 0.5);
	float3 shaodw_dir = normalize(world_pos - light_pos);
#if defined(_REVERSED_Z)
	float linear_z = 1 - saturate((dis - shadow_near) / (shadow_far - shadow_near));
	linear_z += z_bias;
#else
	float linear_z = saturate((dis - shadow_near) / (shadow_far - shadow_near));
	linear_z -= z_bias;
#endif
	float shadow_factor = 0.0;
	for (int q = 0; q < 16; q++)
	{
		uint random_index = uint(16.0 * Random(float4(floor(world_pos.xyy), q))) % 16;
		shaodw_dir.xz += poissonDisk[random_index] * 0.0009765625;
		float cur_sample = PointLightShadowMaps.SampleCmpLevelZero(g_ShadowSampler, float4(shaodw_dir,(float)shadow_index),linear_z);
	#if defined(_REVERSED_Z)
		cur_sample = 1 - cur_sample;
	#endif
		shadow_factor += cur_sample * 0.0625;
	}
	//shadow_factor =  (1.0 - shadow_factor) * ShadowDistaanceAtten(dis,shadow_far);
	return shadow_factor;// *= lerp(1,0.0,saturate(dis/_MainLightShadowDistance));
}
#endif