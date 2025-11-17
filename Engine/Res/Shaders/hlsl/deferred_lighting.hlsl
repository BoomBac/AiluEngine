//info bein
//pass begin::
//name: deferred_lighting
//vert: FullscreenVSMain
//pixel: DeferredLightingPSMain
//ZTest: Always
//Cull: Back
//Stencil: {Ref:0,Comp:NotEqual,Pass:Keep}
//Queue: Opaque
//multi_compile _ DEBUG_NORMAL DEBUG_ALBEDO DEBUG_WORLDPOS DEBUG_GI
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


TEXTURE2D(_GBuffer0)
TEXTURE2D(_GBuffer1)
TEXTURE2D(_GBuffer2)
TEXTURE2D(_GBuffer3)
TEXTURE2D(_CameraDepthTexture)

FullScreenPSInput FullscreenVSMain(uint vertex_id : SV_VERTEXID);

float4 DeferredLightingPSMain(FullScreenPSInput input) : SV_TARGET
{
	//float2 gbuf0 = _GBuffer0.SampleLevel(g_LinearWrapSampler,input.uv,0).xy;
	float2 gbuf0 = SAMPLE_TEXTURE2D_LOD(_GBuffer0, g_LinearClampSampler, input.uv,0).xy;
	float4 gbuf1 = _GBuffer1.Sample(g_LinearWrapSampler,input.uv);
	float4 gbuf2 = _GBuffer2.Sample(g_LinearWrapSampler,input.uv);
	half4 emssion = SAMPLE_TEXTURE2D_LOD(_GBuffer3, g_LinearClampSampler, input.uv,0);
	float depth = SAMPLE_TEXTURE2D_LOD(_CameraDepthTexture,g_LinearWrapSampler,input.uv,0);
	float3 world_pos = ComputeWorldSpacePosition(input.uv,depth,_MatrixIVP);
	//float3 world_pos = _CameraPos.xyz + UnprojectByCameraRay(input.uv,depth);
	SurfaceData surface_data;
	surface_data.wnormal = UnpackNormal(gbuf0);
	surface_data.albedo = float4(gbuf1.rgb,1.0f);
	surface_data.roughness = gbuf1.w;
	surface_data.metallic = gbuf2.w;
	surface_data.specular = 1.0.xxx;
	surface_data.anisotropy = gbuf2.z;
	surface_data.emssive = emssion.rgb;
	OrthonormalBasis(surface_data.wnormal,surface_data.tangent,surface_data.bitangent);
	uint material_id = (uint)gbuf2.r;
	return float4(surface_data.wnormal,1); 
#ifdef DEBUG_NORMAL
	return float4(surface_data.wnormal,1); 
#elif DEBUG_ALBEDO
	return surface_data.albedo;
#elif DEBUG_WORLDPOS
	return float4(world_pos.xyz,1);
#else
	float3 light = max(0.0, CalculateLightPBR(surface_data, world_pos.xyz,input.uv));
	light += surface_data.emssive;
	return float4(light, 1.0); 
#endif 
}
