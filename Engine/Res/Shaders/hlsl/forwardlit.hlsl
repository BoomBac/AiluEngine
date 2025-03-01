//info bein
//pass begin::
//name: forward_standard_lit
//vert: ForwardVSMain
//pixel: ForwardPSMain
//Cull: Back
//ZWrite: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
//pass end::
//Properties
//{
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
#include "lighting.hlsli"

StandardPSInput ForwardVSMain(StandardVSInput v)
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
	result.world_pos = TransformObjectToWorld(v.position);
	result.shadow_pos = TransformFromWorldToLightSpace(0, result.world_pos);
	return result;
}

float4 ForwardPSMain(StandardPSInput input) : SV_TARGET
{
	SurfaceData surface_data;
	InitSurfaceData(input, surface_data);
    float alpha = surface_data.albedo.a;
    float3 light = max(0.0, CalculateLightPBR(surface_data, input.world_pos,input.uv0));
    light += surface_data.emssive;
    return float4(light, alpha);
}