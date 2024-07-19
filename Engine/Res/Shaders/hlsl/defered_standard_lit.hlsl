//info bein
//pass begin::
//name: defered_standard_lit
//vert: GBufferVSMain
//pixel: GBufferPSMain
//Cull: Back
//Queue: Opaque
//Stencil: {Ref:127,Comp:Always,Pass:Replace}
//multi_compile _ ALPHA_TEST
//pass end::
//Properties
//{
//  _AlphaCulloff("AlphaCulloff",Range(0,1)) = 0
//	_AlbedoTex("Albedo",Texture2D) = "white"
//	_NormalTex("Normal",Texture2D) = "white"
//	_EmissionTex("Emission",Texture2D) = "white"
//	_RoughnessMetallicTex("RoughnessMetallicTex",Texture2D) = "white"
//	_SpecularTex("Specular",Texture2D) = "white"
//	_AlbedoValue("BaseColor",Color) = (1,1,1,0)
//	_EmissionValue("Emission",Color) = (0,0,0,0)
//	_SpecularValue("Specular",Color) = (0,0,0,0)
//	_RoughnessValue("Roughness",Range(0,1)) = 0
//	_MetallicValue("Metallic",Range(0,1)) = 0
//}
//info end

#include "standard_lit_common.hlsli"

struct GBuffer
{
	float2 packed_normal : SV_Target0;
	float4 color_roughness : SV_Target1;
	float4 specular_metallic : SV_Target2;
};

PSInput GBufferVSMain(VSInput v)
{
	PSInput result;
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

GBuffer GBufferPSMain(PSInput input) : SV_TARGET
{
	SurfaceData surface_data;
	InitSurfaceData(input, surface_data.wnormal, surface_data.albedo, surface_data.roughness, surface_data.metallic, surface_data.emssive, surface_data.specular);
#ifdef ALPHA_TEST
	clip(surface_data.albedo.a - _AlphaCulloff);
#endif
	GBuffer output;
	output.packed_normal = PackNormal(surface_data.wnormal);
	output.color_roughness = float4(surface_data.albedo.rgb,surface_data.roughness);
	output.specular_metallic = float4(surface_data.specular,surface_data.metallic);
	return output;
}