//info bein
//name: deferred_lighting
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
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
	//depth = depth * 2.0 - 1.0;
	input.uv.y = 1.0 - input.uv.y;
	float4 clipPos = float4(2.0f * input.uv - 1.0f, depth, 1.0);
	float4 world_pos = mul(_MatrixIVP, clipPos);
	world_pos /= world_pos.w;

	SurfaceData surface_data;
	surface_data.wnormal = UnpackNormal(gbuf0);
	//surface_data.wnormal = float3(0.0,1.0,0.0);
	//surface_data.wnormal = surface_data.wnormal * 2.0 - 1.0;
	surface_data.albedo = float4(gbuf1.rgb,1.0f);
	surface_data.roughness = gbuf1.w;
	surface_data.metallic = gbuf2.w;
	surface_data.emssive = float3(0.f,0.f,0.f), 
	surface_data.specular = gbuf2.rgb;
	float3 light = max(0.0, CalculateLightPBR(surface_data, world_pos.xyz));
	light += surface_data.emssive;
	float4 shadow_pos = TransformFromWorldToLightSpace(0,world_pos.xyz);
	float nl = saturate(dot(_DirectionalLights[0]._LightPosOrDir, surface_data.wnormal));
	float shadow_factor = ApplyShadow(shadow_pos, nl, world_pos.xyz);
	light *= lerp(0.1,1.0,shadow_factor);
	return float4(light, 1.0);
}
