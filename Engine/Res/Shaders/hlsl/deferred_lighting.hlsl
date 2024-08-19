//info bein
//pass begin::
//name: deferred_lighting
//vert: FullscreenVSMain
//pixel: DeferredLightingPSMain
//ZTest: Always
//Cull: Back
//Stencil: {Ref:127,Comp:Equal,Pass:Keep}
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
#include "fullscreen_quad.hlsli"


//Texture2D MainLightShadowMap : register(t7); 

Texture2D _GBuffer0 : register(t0); 
Texture2D _GBuffer1 : register(t1);
Texture2D _GBuffer2 : register(t2);
TEXTURE2D(_GBuffer3,3);
TEXTURE2D(_CameraDepthTexture,4);


// PSInput DeferredLightingVSMain(VSInput v)
// {
// 	PSInput result;
// 	result.position = float4(v.position.xy,0.0, 1.0);
// 	result.uv = v.uv;
// 	return result;
// }
PSInput FullscreenVSMain(uint vertex_id : SV_VERTEXID);

float4 DeferredLightingPSMain(PSInput input) : SV_TARGET
{
	//float2 gbuf0 = _GBuffer0.SampleLevel(g_LinearWrapSampler,input.uv,0).xy;
	float2 gbuf0 = SAMPLE_TEXTURE2D_LOD(_GBuffer0, g_LinearClampSampler, input.uv,0).xy;
	float4 gbuf1 = _GBuffer1.Sample(g_LinearWrapSampler,input.uv);
	float4 gbuf2 = _GBuffer2.Sample(g_LinearWrapSampler,input.uv);
	float2 motion_vector = SAMPLE_TEXTURE2D_LOD(_GBuffer3, g_LinearClampSampler, input.uv,0).xy;
	motion_vector = motion_vector * 0.5 + 0.5;
  
	float depth = _CameraDepthTexture.Sample(g_LinearWrapSampler,input.uv);
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
	// float dis = distance(_CameraPos.xyz,world_pos);	
	// float c = saturate((_CascadeShadowParams.y * (1.0 - dis * _CascadeShadowParams.z)));
	// return c.xxxx; 
	// if(SqrDistance(world_pos,_CascadeShadowSplit[0].xyz) <= _CascadeShadowSplit[0].w)
	// {
	// 	return float4(1,0,0,1);
	// }
	// if(SqrDistance(world_pos,_CascadeShadowSplit[1].xyz) <= _CascadeShadowSplit[1].w)
	// {
	// 	return float4(0,1,0,1);
	// }
	// return float4(world_pos.xyz,1);
#else
	float3 light = max(0.0, CalculateLightPBR(surface_data, world_pos.xyz,input.uv));
	light += surface_data.emssive;
	return float4(light, 1.0); 
#endif 
}
