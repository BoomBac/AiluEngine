#ifndef __INPUT_H__
#define __INPUT_H__

struct VSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float2 uv0 : TEXCOORD;
	float4 tangent : TANGENT;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float3 world_pos : POSITION;
	float3 normal : NORMAL;
	float2 uv0 : TEXCOORD0;
	float3x3 btn : BTN;
};

struct SurfaceData
{
	float3 wnormal;
	float4 albedo;
	float roughness;
	float metallic;
	float3 emssive;
	float specular;
};

struct LightData
{
    float3 light_pos;
    float3 light_dir;
    float3 light_color;
};

#endif //__INPUT_H__