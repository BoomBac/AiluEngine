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
	float3 normal : NORMAL;
	float2 uv0 : TEXCOORD0;
	float3x3 btn : BTN;
};


struct SurfaceData
{
	float3 wnormal;
	float roughness;
	float4 albedo;
	float3 emssive;
	float metallic;
	float3 specular;
	float anisotropy;
	float3 tangent;
	float3 bitangent;
};

struct ShadingData
{
	float3 view_dir;
	float nl;
	float nv;
	float vh;
	float lh;
	float nh;
	float th;
	float bh;
};

struct LightData
{
    float3 light_pos;
    float3 light_dir;
    float3 light_color;
	float  shadow_atten;
};

#endif //__INPUT_H__