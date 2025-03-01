//info bein
//pass begin::
//name: defered_standard_lit
//vert: GBufferVSMain
//pixel: GBufferPSMain
//Cull: Back
//Queue: Opaque
//Stencil: {Ref:1,Comp:Always,Pass:Replace}
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
//pass begin::
//name: VoxelLit
//vert: VoxelVSMain
//geometry: VoxelGSMain
//pixel: VoxelPSMain
//Cull: Off
//ZTest: Always
//Conservative: On
//Queue: Opaque
//multi_compile _ ALPHA_TEST
//pass end::
//pass begin::
//name: MotionVector
//vert: MotionVectorVSMain
//pixel: MotionVectorPSMain
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: LEqual
//ZWrite: On
//multi_compile _ ALPHA_TEST
//multi_compile _ CPU_DEFORM
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
#include "pixel_packing.hlsli"
#include "voxel_gi.hlsli"

struct GBuffer
{
	float2 packed_normal : SV_Target0;
	float4 color_roughness : SV_Target1;
	float4 specular_metallic : SV_Target2;
	half4  emission : SV_Target3;
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
	result.world_pos = TransformObjectToWorld(v.position);
	result.shadow_pos = TransformFromWorldToLightSpace(0, result.world_pos);
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
	else {};
#ifdef ALPHA_TEST
	clip(surface_data.albedo.a - _AlphaCulloff);
#endif
	GBuffer output;
	output.packed_normal = PackNormal(surface_data.wnormal);
	output.color_roughness = float4(surface_data.albedo.rgb, surface_data.roughness);
	//output.specular_metallic = float4(surface_data.specular, surface_data.metallic);
	output.specular_metallic = float4(0.0,0.0,surface_data.anisotropy,surface_data.metallic);
	output.emission = half4(surface_data.emssive.rgb,1.0);
	return output;
}

//---------------------shadow caster-------------------------
PSInput VSMain(VSInput v);
void PSMain(PSInput input);

//---------------------voxel light---------------------------
#include "lighting.hlsli"
struct VoxelGSInput
{
	float2 uv0 : TEXCOORD0;
	float3 normal : TEXCOORD1;
	float3 world_pos : TEXCOORD2;
	float3 shadow_pos : TEXCOORD3;
};
struct VoxelPSInput
{
	float4 position : SV_POSITION;
	float2 uv0 : TEXCOORD0;
	float3 normal : TEXCOORD1;
	float3 world_pos : TEXCOORD2;
	float3 shadow_pos : TEXCOORD3;
};

VoxelGSInput VoxelVSMain(StandardVSInput v)
{
	VoxelGSInput result;
	//result.position = TransformToClipSpace(v.position);
	result.normal = v.normal;
	result.uv0 = v.uv0;
	v.tangent.xyz *= v.tangent.w;
	float3 N = TransformNormal(v.normal);
	result.normal = TransformNormal(v.normal);
	result.world_pos = TransformObjectToWorld(v.position);
	result.shadow_pos = TransformFromWorldToLightSpace(0, result.world_pos);
	return result;
}
[maxvertexcount(6)]
void VoxelGSMain(triangle VoxelGSInput g[3],inout TriangleStream<StandardPSInput> results)
{
	float3 face_nromal = abs(g[0].normal + g[1].normal + g[2].normal);
	uint maxi = face_nromal[1] > face_nromal[0] ? 1 : 0;
	maxi = face_nromal[2] > face_nromal[maxi] ? 2 : maxi;
	for(uint i = 0; i < 3; i++)
	{
		StandardPSInput result;
		result.position = float4((g[i].world_pos.xyz - g_VXGI_Center.xyz) / g_VXGI_GridSize.xyz,1.0);
		if (maxi == 0)
			result.position.xyz = result.position.zyx;
		else if (maxi == 1)
			result.position.xyz = result.position.xzy;
		result.position.xy /= g_VXGI_GridNum.xy * 0.5;
		result.position.z = 1.0f;
		result.normal = g[i].normal;
		result.uv0 = g[i].uv0;
		result.btn = float3x3(float3(1,0,0),float3(0,1,0),float3(0,0,3));
		result.world_pos = g[i].world_pos;
		result.shadow_pos = float4(g[i].shadow_pos,1.0);
		results.Append(result);
	}
}

uint CalculateGridIndex(float3 world_pos)
{
    float3 rela_pos = world_pos - g_VXGI_Center.xyz;
	float3 voxel_pos = rela_pos / g_VXGI_GridSize.xyz;
    int3 rela_index = int3(floor(voxel_pos));
    rela_index.xyz += g_VXGI_GridNum.xyz / 2;
    rela_index.x = max(0, min(rela_index.x, g_VXGI_GridNum.x - 1));
    rela_index.y = max(0, min(rela_index.y, g_VXGI_GridNum.y - 1));
    rela_index.z = max(0, min(rela_index.z, g_VXGI_GridNum.z - 1));
	return ToLinearIndex(rela_index,g_VXGI_GridNum.xyz);
}
#include "color_space_utils.hlsli"
RWStructuredBuffer<uint> g_voxel_data_block;
void VoxelPSMain(StandardPSInput input)
{
	SurfaceData surface_data;
	InitSurfaceData(input, surface_data);
#ifdef ALPHA_TEST
	clip(surface_data.albedo.a - _AlphaCulloff);
#endif
    float alpha = surface_data.albedo.a;
    float3 light = max(0.0, CalculateLightSimple(surface_data, input.world_pos,input.uv0));
    light += surface_data.emssive;
	light.rgb += 0.01.xxx;
	//light = ACESFilm(light);
	uint index = CalculateGridIndex(input.world_pos);
	InterlockedMax(g_voxel_data_block[index],EncodeRGBE(light,1));
}


//---------------------motion vector--------------------------------------------------
#include "motion_vector.hlsl"
VertOutput MotionVectorVSMain(VertInput v);
float2 MotionVectorPSMain(VertOutput input);
//---------------------motion vector---------------------------------------------------
