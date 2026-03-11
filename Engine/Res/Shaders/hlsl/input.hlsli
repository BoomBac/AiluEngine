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
SurfaceData SurfaceData_Constructor(
	float3 wnormal,
	float roughness,
	float4 albedo,
	float3 emssive,
	float metallic,
	float3 specular,
	float anisotropy,
	float3 tangent,
	float3 bitangent)
{
	SurfaceData data;
	data.wnormal = wnormal;
	data.roughness = max(roughness,1e-3);
	data.albedo = albedo;
	data.emssive = emssive;
	data.metallic = metallic;
	data.specular = specular;
	data.anisotropy = anisotropy;
	data.tangent = tangent;
	data.bitangent = bitangent;
	return data;
}

ShadingData ShadingData_Constructor(
	float3 view_dir,
	float nl,
	float nv,
	float vh,
	float lh,
	float nh,
	float th,
	float bh)
{
	ShadingData data;
	data.view_dir = view_dir;
	data.nl = nl;
	data.nv = nv;
	data.vh = vh;
	data.lh = lh;
	data.nh = nh;
	data.th = th;
	data.bh = bh;
	return data;
}

#endif //__INPUT_H__