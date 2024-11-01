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
//pass begin::
//name: ShadowCaster
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
//multi_compile _ ALPHA_TEST
//multi_compile _ CAST_POINT_SHADOW
//pass end::
//Properties
//{
//	_AlphaCulloff("AlphaCulloff",Range(0,1)) = 0
//	_AlbedoTex("Albedo",Texture2D) = "white"
//	_NormalTex("Normal",Texture2D) = "white"
//	_EmissionTex("Emission",Texture2D) = "white"
//	_RoughnessMetallicTex("RoughnessMetallicTex",Texture2D) = "white"
//	_SpecularTex("Specular",Texture2D) = "white"
//	_AlbedoValue("BaseColor",Color) = (1,1,1,0)
//	[HDR]_EmissionValue("Emission",Color) = (0,0,0,0)
//	_SpecularValue("Specular",Color) = (0,0,0,0)
//	_RoughnessValue("Roughness",Range(0,1)) = 0
//	_MetallicValue("Metallic",Range(0,1)) = 0
//	_Anisotropy("Anisotropy",Range(0,1)) = 0
//}
//info end

#include "standard_lit_common.hlsli"
#include "shadow_caster.hlsli"

struct GBuffer
{
	float2 packed_normal : SV_Target0;
	float4 color_roughness : SV_Target1;
	float4 specular_metallic : SV_Target2;
	float2 motion_vector : SV_Target3;
	half4  emission : SV_Target4;
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
	result.clip_pos_cur_frame = mul(_MatrixWorld,float4(v.position,1.0f));
	result.clip_pos_pre_frame = mul(_MatrixWorldPreTick,float4(v.position,1.0f));
	return result;
}

GBuffer GBufferPSMain(StandardPSInput input) : SV_TARGET
{
	SurfaceData surface_data;
	if (_MaterialID == 0 || _MaterialID == 1)// Defualt
	{
		InitSurfaceData(input, surface_data);
	}
	else if (_MaterialID == 2) // scheckboard
	{
		InitSurfaceDataCheckboard(input, surface_data);
	}

	input.clip_pos_cur_frame.xyz /= input.clip_pos_cur_frame.w;
	input.clip_pos_pre_frame.xyz /= input.clip_pos_pre_frame.w;
#ifdef ALPHA_TEST
	clip(surface_data.albedo.a - _AlphaCulloff);
#endif
	GBuffer output;
	output.packed_normal = PackNormal(surface_data.wnormal);
	output.color_roughness = float4(surface_data.albedo.rgb, surface_data.roughness);
	//output.specular_metallic = float4(surface_data.specular, surface_data.metallic);
	output.specular_metallic = float4(0.0,0.0,surface_data.anisotropy,surface_data.metallic);
	output.motion_vector = float2(input.clip_pos_cur_frame.xy - input.clip_pos_pre_frame.xy);
	output.emission = half4(surface_data.emssive.rgb,1.0);
	return output;
}

//---------------------shadow caster-------------------------
PSInput VSMain(VSInput v);
void PSMain(PSInput input);