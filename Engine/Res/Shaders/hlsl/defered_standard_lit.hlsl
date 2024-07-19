//info bein
//pass begin::
//name: defered_standard_lit
//vert: GBufferVSMain
//pixel: GBufferPSMain
//Cull: Back
//Queue: Opaque
//Stencil: {Ref:127,Comp:Always,Pass:Replace}
//pass end::
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

#include "standard_lit_common.ash"

struct GBuffer
{
	float2 packed_normal : SV_Target0;
	float4 color_roughness : SV_Target1;
	float4 specular_metallic : SV_Target2;
};

StandardPSInput GBufferVSMain(StandardVSInput v)
{
	StandardPSInput result;
	result.position = TransformToClipSpace(v.position);
	result.normal = v.normal;
	result.uv0 = v.uv0;
	v.tangent.xyz *= v.tangent.w;
	float3 T = TransformNormal(v.tangent.xyz);
	float3 B = TransformNormal(cross(v.tangent.xyz, v.normal));
	float3 N = TransformNormal(v.normal);
	result.btn = float3x3(T, B, N);
	result.normal = N;
	result.world_pos = TransformToWorldSpace(v.position);
	result.shadow_pos = TransformFromWorldToLightSpace(0, result.world_pos);
	return result;
}

GBuffer GBufferPSMain(StandardPSInput input) : SV_TARGET
{
	SurfaceData surface_data;
	InitSurfaceData(input, surface_data.wnormal, surface_data.albedo, surface_data.roughness, surface_data.metallic, surface_data.emssive, surface_data.specular);
	GBuffer output;
	output.packed_normal = PackNormal(surface_data.wnormal);
	output.color_roughness = float4(surface_data.albedo.rgb,surface_data.roughness);
	//output.specular_metallic = float4(surface_data.specular,surface_data.metallic);
	output.specular_metallic = float4(float(MaterialID).xxx,surface_data.metallic);
	return output;
}