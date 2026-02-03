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
#include "froxel_common.hlsli"


TEXTURE2D(_GBuffer0)
TEXTURE2D(_GBuffer1)
TEXTURE2D(_GBuffer2)
TEXTURE2D(_GBuffer3)
TEXTURE2D(_CameraDepthTexture)

TEXTURE3D(_VolumetricLightTexture)

// float3 GetViewRay(float2 uv)
// {
//     float2 ndc = uv * 2.0 - 1.0;
//     float3 ray;
//     ray.x = ndc.x / _MatrixP._11;
//     ray.y = ndc.y / _MatrixP._22;
//     ray.z = 1.0;
//     return normalize(ray);
// }

float4 RaymarchVolumetric(float3 uvw,float scene_depth)
{
    if (any(uvw < 0) || any(uvw > 1))
        return 0;
	uint w, h, d;
	_VolumetricLightTexture.GetDimensions(w, h, d);
	uint z = (uint)(uvw.z * d);
	float slice_far = SliceDistance(z+1,_ProjectionParams.y,_ProjectionParams.z);
	float slice_near = SliceDistance(z,_ProjectionParams.y,_ProjectionParams.z);
	float view_z = LinearEyeDepth(scene_depth,_ProjectionParams.y,_ProjectionParams.z);
	float fade = saturate((view_z - slice_near) / (slice_far - slice_near));
	// if (slice_far > view_z)
	// 	return 0;
	float4 light_and_slice_far = _VolumetricLightTexture.SampleLevel(g_PointClampSampler, uvw, 0);
    return _VolumetricLightTexture.SampleLevel(g_LinearClampSampler, uvw, 0);
}

FullScreenPSInput FullscreenVSMain(FullScreenVSInput i);

float4 DeferredLightingPSMain(FullScreenPSInput input) : SV_TARGET
{
	//float2 gbuf0 = _GBuffer0.SampleLevel(g_LinearWrapSampler,input.uv,0).xy;
	float2 gbuf0 = SAMPLE_TEXTURE2D_LOD(_GBuffer0, g_LinearClampSampler, input.uv,0).xy;
	float4 gbuf1 = _GBuffer1.Sample(g_LinearWrapSampler,input.uv);
	float4 gbuf2 = _GBuffer2.Sample(g_LinearWrapSampler,input.uv);
	half4 emssion = SAMPLE_TEXTURE2D_LOD(_GBuffer3, g_LinearClampSampler, input.uv,0);
	float depth = SAMPLE_TEXTURE2D_LOD(_CameraDepthTexture,g_LinearWrapSampler,input.uv,0).r;
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
	//return float4(surface_data.wnormal,1);
#ifdef DEBUG_NORMAL
	return float4(surface_data.wnormal,1);
#elif DEBUG_ALBEDO
	return surface_data.albedo;
#elif DEBUG_WORLDPOS
	return float4(world_pos.xyz,1);
#else
	float3 light = max(0.0, CalculateLightPBR(surface_data, world_pos.xyz,input.uv));
	light += surface_data.emssive;
    float3 vol_light_uvw = WorldToVolumetricVoxelUVW(world_pos.xyz,_MatrixV,_MatrixP,_ProjectionParams.y,_ProjectionParams.z);
	float4 vol_light = RaymarchVolumetric(vol_light_uvw,depth);
	//light = vol_light.r == 19.5? float3(0,1,0) : float3(1,0,0);
	light += vol_light.rgb;
	//light = LinearEyeDepth(depth,_ProjectionParams.z,_ProjectionParams.y).xxxx;
    //light = Linear01Depth(depth,_ZBufferParams).xxxx;
    //light = Linear01DepthNew(depth,100.0f,1.0f).xxxx;
	return float4(light, 1.0); 
#endif 
}
