#ifndef __INPUT_H__
#define __INPUT_H__

struct StandardVSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float2 uv0 : TEXCOORD;
	float4 tangent : TANGENT;
};

struct StandardPSInput
{
	float4 position : SV_POSITION;
	float3 world_pos : POSITION;
	float4 shadow_pos : SHADOW_COORD;
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
	float3 specular;
};

struct ShadingData
{
	float nl;
	float nv;
	float vh;
	float lh;
	float nh;
};

struct LightData
{
    float3 light_pos;
    float3 light_dir;
    float3 light_color;
	float  shadow_atten;
};

#endif //__INPUT_H__