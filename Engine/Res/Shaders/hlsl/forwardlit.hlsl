//info bein
//pass begin::
//name: forward_standard_lit
//vert: ForwardVSMain
//pixel: ForwardPSMain
//Cull: Back
//ZWrite: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
//multi_compile _ ALPHA_TEST
//pass end::
//pass begin::
//name: ShadowCaster
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
//ZWrite: On
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
//	_IOR("IOR",Range(0,3)) = 1
//	_Transmission("_Transmission",Range(0,1)) = 0
//}
//info end

#define AL_SCENE_PRIMITIVE 1
#include "standard_lit_common.hlsli"
#include "primitive.hlsli"
#include "lighting.hlsli"
#include "shadow_caster.hlsli"

StandardPSInput ForwardVSMain(StandardVSInput v)
{
	StandardPSInput result;
	const PrimitiveData primitive = LoadPrimitive(v.instance_id);
	result.position = TransformPrimitiveToClipSpace(primitive, v.position);
	result.normal = v.normal;
	result.uv0 = v.uv0;
	v.tangent.xyz *= v.tangent.w;
	float3 T = TransformPrimitiveNormal(primitive, v.tangent.xyz);
	float3 B = TransformPrimitiveNormal(primitive, cross(v.tangent.xyz, v.normal));
	float3 N = TransformPrimitiveNormal(primitive, v.normal);
	result.btn = float3x3(T, B, N);
	result.normal = N;
	result.world_pos = TransformPrimitiveToWorld(primitive, v.position);
	return result;
}

float4 ForwardPSMain(StandardPSInput input) : SV_TARGET
{
	SurfaceData surface_data;
	InitSurfaceData(input, surface_data);
	float alpha = surface_data.albedo.a;
	alpha = pow(alpha,0.4545);
#ifdef ALPHA_TEST
	clip(alpha - _AlphaCulloff);
#endif
    float3 light = max(0.0, CalculateLightPBR(surface_data, input.world_pos,input.uv0));
    light += surface_data.emssive;
    return float4(light, alpha);
}
