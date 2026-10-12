//info begin
//pass begin::
//name: forward_standard_lit
//vert: ForwardVSMain
//pixel: ForwardPSMain
//Cull: Back
//ZWrite: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
//multi_compile _ ALPHA_TEST
//multi_compile _ EMISSION_TEXTURE_FACTOR
//multi_compile _ LINEAR_ALPHA
//multi_compile _ PER_OBJECT_CB
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
//	_RoughnessTex("Roughness Texture",Texture2D) = "white"
//	_MetallicTex("Metallic Texture",Texture2D) = "white"
//	_SpecularTex("Specular",Texture2D) = "white"
//	_AlbedoValue("BaseColor",Color) = (1,1,1,0)
//	[HDR]_EmissionValue("Emission",Color) = (0,0,0,0)
//	_SpecularValue("Specular",Color) = (0,0,0,0)
//	_RoughnessValue("Roughness",Range(0,1)) = 0
//	_MetallicValue("Metallic",Range(0,1)) = 0
//	_Anisotropy("Anisotropy",Range(0,1)) = 0
//	_IOR("IOR",Range(0,3)) = 1
//	_Transmission("Transmission",Range(0,1)) = 0
	//	_AttenuationColor("Attenuation Color",Color) = (1,1,1,1)
	//	_AttenuationDistance("Attenuation Distance",Range(0,1000)) = 1
//}
//info end

//PER_OBJECT_CB：资产预览等非场景绘制路径没有 scene primitive 缓冲，切回 per-object cbuffer
#if defined(PER_OBJECT_CB)
#include "standard_lit_common.hlsli"
#include "lighting.hlsli"
#else
#define AL_SCENE_PRIMITIVE 1
#include "standard_lit_common.hlsli"
#include "primitive.hlsli"
#include "lighting.hlsli"
#include "shadow_caster.hlsli"
#endif
#include "volumetric_fog_common.hlsli"
Texture2D _ThinGlassSceneColor;

StandardPSInput ForwardVSMain(StandardVSInput v)
{
	StandardPSInput result;
	v.tangent.xyz *= v.tangent.w;
#if defined(PER_OBJECT_CB)
        result.position = TransformToClipSpace(v.position);
        result.world_pos = TransformObjectToWorld(v.position);
        result.primitive_index = UINT32_MAX;
	float3 T = TransformNormal(v.tangent.xyz);
	float3 B = TransformNormal(cross(v.tangent.xyz, v.normal));
	float3 N = TransformNormal(v.normal);
#else
        const PrimitiveData primitive = LoadPrimitive(v.instance_id);
        result.primitive_index = GetPrimitiveIndex(v.instance_id);
	result.position = TransformPrimitiveToClipSpace(primitive, v.position);
	result.world_pos = TransformPrimitiveToWorld(primitive, v.position);
	float3 T = TransformPrimitiveNormal(primitive, v.tangent.xyz);
	float3 B = TransformPrimitiveNormal(primitive, cross(v.tangent.xyz, v.normal));
	float3 N = TransformPrimitiveNormal(primitive, v.normal);
#endif
	result.normal = N;
	result.uv0 = v.uv0;
	result.btn = float3x3(T, B, N);
	return result;
}

float4 ForwardPSMain(StandardPSInput input) : SV_TARGET
{
	SurfaceData surface_data;
	InitSurfaceData(input, surface_data);
	float alpha = surface_data.albedo.a;
#ifndef LINEAR_ALPHA
	alpha = pow(alpha,0.4545);
#endif
#ifdef ALPHA_TEST
	clip(alpha - _AlphaCulloff);
#endif
    float3 irradiance = EvaluateEnvironmentIrradiance(surface_data.wnormal);
#if !defined(PER_OBJECT_CB)
    const PrimitiveData primitive = g_primitive_data[input.primitive_index];
    if (HasPrimitiveStaticGI(primitive))
        irradiance = EvaluatePrimitiveStaticIrradiance(primitive, surface_data.wnormal);
#endif
    float2 screen_uv = input.position.xy * _ScreenParams.xy;
    float transmission = saturate(_Transmission) * (1.0 - saturate(surface_data.metallic));
#if defined(PER_OBJECT_CB)
    transmission = 0.0;
#else
    uint glass_width, glass_height;
    _ThinGlassSceneColor.GetDimensions(glass_width, glass_height);
    transmission *= glass_width > 1 && glass_height > 1 ? 1.0 : 0.0;
#endif
    float dielectric_specular = 0.04;
    if (transmission > 0.0)
    {
        float ior = max(_IOR, 1.0);
        dielectric_specular = pow((ior - 1.0) / (ior + 1.0), 2.0);
    }
    float3 light = max(0.0, CalculateLightPBR(surface_data, input.world_pos, input.uv0, irradiance, irradiance,
                                            false, 1.0 - transmission, dielectric_specular));
    light += surface_data.emssive;
    float view_depth = mul(_MatrixV, float4(input.world_pos, 1.0f)).z;
    if (transmission > 0.0)
    {
        float nv = saturate(abs(dot(normalize(_CameraPos.xyz - input.world_pos), surface_data.wnormal)));
        float fresnel = dielectric_specular > 0.0 ? F_Schlick(dielectric_specular.xxx, nv).r : 0.0;
        float3 throughput = transmission * (1.0 - fresnel) * saturate(surface_data.albedo.rgb);
        // The snapshot is already fogged. Only the new surface contribution needs foreground fog.
        float4 fog = SampleVolumetricFog(screen_uv, view_depth);
        light = light * saturate(fog.a) + (1.0 - throughput) * fog.rgb;
        light += throughput * _ThinGlassSceneColor.SampleLevel(g_LinearClampSampler, screen_uv, 0).rgb;
    }
    else
        light = ApplyVolumetricFogStraight(light, screen_uv, view_depth);
    return float4(light, alpha);
}
