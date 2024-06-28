//info bein
//pass begin::
//name: deferred_lighting
//vert: DeferredLightingVSMain
//pixel: DeferredLightingPSMain
//Cull: Back
//Stencil: {Ref 5 Comp Always Pass Replace}
//Queue: Opaque
//multi_compile _ DEBUG_NORMAL DEBUG_ALBEDO DEBUG_WORLDPOS
//pass end::
//Properties
//{
//}
//info end

#include "common.hlsli"
#include "input.hlsli"
#include "cbuffer.hlsli"
#include "lighting.hlsli"
#include "shadow.hlsli"


//Texture2D MainLightShadowMap : register(t7); 

Texture2D _GBuffer0 : register(t0); 
Texture2D _GBuffer1 : register(t1);
Texture2D _GBuffer2 : register(t2);
Texture2D _CameraDepthTexture : register(t3);

struct DeferredVSInput
{ 
	float3 position : POSITION;
	float2 uv : TEXCOORD;
};

struct DeferredPSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

DeferredPSInput DeferredLightingVSMain(DeferredVSInput v)
{
	DeferredPSInput result;
	result.position = float4(v.position.xy,0.0, 1.0);
	result.uv = v.uv;
	return result;
}

float4 DeferredLightingPSMain(DeferredPSInput input) : SV_TARGET
{
	float2 gbuf0 = _GBuffer0.Sample(g_LinearWrapSampler,input.uv).xy;
	float4 gbuf1 = _GBuffer1.Sample(g_LinearWrapSampler,input.uv);
	float4 gbuf2 = _GBuffer2.Sample(g_LinearWrapSampler,input.uv);
  
	float depth = _CameraDepthTexture.Sample(g_LinearWrapSampler,input.uv);
	input.uv.y = 1.0 - input.uv.y;
	float3 world_pos = Unproject(input.uv,depth);

	SurfaceData surface_data;
	surface_data.wnormal = UnpackNormal(gbuf0);
	surface_data.albedo = float4(gbuf1.rgb,1.0f);
	surface_data.roughness = gbuf1.w;
	surface_data.metallic = gbuf2.w;
	surface_data.emssive = float3(0.f,0.f,0.f), 
	surface_data.specular = gbuf2.rgb;
	uint material_id = (uint)gbuf2.r;
#ifdef DEBUG_NORMAL
	return float4(surface_data.wnormal,1);
#elif DEBUG_ALBEDO
	return surface_data.albedo;
#elif DEBUG_WORLDPOS
	return float4(world_pos.xyz,1);
#else
	float3 light = max(0.0, CalculateLightPBR(surface_data, world_pos.xyz));
	light += surface_data.emssive;
	return float4(light, 1.0); 
#endif 
}
