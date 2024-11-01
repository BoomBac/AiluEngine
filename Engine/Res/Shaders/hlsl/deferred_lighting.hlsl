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


TEXTURE2D(_GBuffer0)
TEXTURE2D(_GBuffer1)
TEXTURE2D(_GBuffer2)
TEXTURE2D(_GBuffer3)
TEXTURE2D(_GBuffer4)
TEXTURE2D(_CameraDepthTexture)

PSInput FullscreenVSMain(uint vertex_id : SV_VERTEXID);

float4 DeferredLightingPSMain(PSInput input) : SV_TARGET
{
	//float2 gbuf0 = _GBuffer0.SampleLevel(g_LinearWrapSampler,input.uv,0).xy;
	float2 gbuf0 = SAMPLE_TEXTURE2D_LOD(_GBuffer0, g_LinearClampSampler, input.uv,0).xy;
	float4 gbuf1 = _GBuffer1.Sample(g_LinearWrapSampler,input.uv);
	float4 gbuf2 = _GBuffer2.Sample(g_LinearWrapSampler,input.uv);
	float2 motion_vector = SAMPLE_TEXTURE2D_LOD(_GBuffer3, g_LinearClampSampler, input.uv,0).xy;
	half4 emssion = SAMPLE_TEXTURE2D_LOD(_GBuffer4, g_LinearClampSampler, input.uv,0);
	motion_vector = motion_vector * 0.5 + 0.5;
	float depth = _CameraDepthTexture.Sample(g_LinearWrapSampler,input.uv);
	float3 world_pos = Unproject(input.uv,depth);

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
