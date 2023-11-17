#ifndef __CBUFFER_H__
#define __CBUFFER_H__

#include "constants.hlsl"

struct ShaderDirectionalAndPointLightData
{
	float3 _LightPosOrDir;
	float _LightParam0;
	float3 _LightColor;
	float _LightParam1;
};

struct ShaderSpotlLightData
{
	float3 _LightDir;
	float _Rdius;
	float3 _LightPos;
	float _InnerAngle;
	float3 _LightColor;
	float _OuterAngle;
};

cbuffer SceneObjectBuffer : register(b0)
{
	float4x4 _MatrixWorld;
}

cbuffer SceneMaterialBuffer : register(b1)
{
	float4 		BaseColor; //0
	float4 		EmssiveColor;//16
	float4		SpecularColor;//32
	float	 	RoughnessValue;//36
	float	 	MetallicValue;//40
	uint 		SamplerMask;//44
};

cbuffer SceneStatetBuffer : register(b2)
{
	float4x4 _MatrixV;
	float4x4 _MatrixP;
	float4x4 _MatrixVP;
	float4 _CameraPos;
	ShaderDirectionalAndPointLightData _DirectionalLights[MAX_DIRECTIONAL_LIGHT];
	ShaderDirectionalAndPointLightData _PointLights[MAX_POINT_LIGHT];
	ShaderSpotlLightData _SpotLights[MAX_SPOT_LIGHT];
	float padding[44];
};

#endif // !CBUFFER_H__

