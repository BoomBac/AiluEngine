//info begin
//pass begin::
//name: VolumetricFogComposite
//vert: FullscreenVSMain
//pixel: FogCompositePSMain
//Cull: Back
//ZTest: Always
//ZWrite: Off
//Blend: One,Src
//Queue: Transparent
//pass end::
//info end

#include "common.hlsli"
#include "fullscreen_quad.hlsli"

TEXTURE2D(_CameraDepthTexture)
TEXTURE3D(_VolumetricFogAccum)

PerMaterialCBufferBegin
    float _fog_far;
PerMaterialCBufferEnd

float4 FogCompositePSMain(FullScreenPSInput input) : SV_TARGET
{
    float depth = _CameraDepthTexture.Load(int3(uint2(input.position.xy), 0)).r;
    float near_plane = _ProjectionParams.y;
    float far_plane = _ProjectionParams.z;
    float slice_fraction = 1.0;
    // Sky has no finite surface: use the complete volume instead of reconstructing a far-plane world position.
    if (DeviceDepthToLogic(depth) < 1.0)
    {
        float view_depth = max(LinearEyeDepth(depth, near_plane, far_plane), near_plane);
        slice_fraction = saturate(log(view_depth / near_plane) / log(_fog_far / near_plane));
    }
    // The froxel grid uses bottom-left NDC UVs; screen textures use top-left UVs.
    float3 fog_uvw = float3(input.uv.x, 1.0 - input.uv.y, slice_fraction);
    float4 scattering_transmittance = _VolumetricFogAccum.SampleLevel(g_LinearClampSampler, fog_uvw, 0);
    return float4(scattering_transmittance.rgb, saturate(scattering_transmittance.a));
}
