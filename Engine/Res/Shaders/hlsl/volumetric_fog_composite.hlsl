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
    uint accum_width, accum_height, accum_depth;
    _VolumetricFogAccum.GetDimensions(accum_width, accum_height, accum_depth);
    // Sky has no finite surface: use the complete volume instead of reconstructing a far-plane world position.
    if (DeviceDepthToLogic(depth) < 1.0)
    {
        float view_depth = LinearEyeDepth(depth, near_plane, far_plane);
        if (view_depth <= near_plane)
            return float4(0.0f, 0.0f, 0.0f, 1.0f);
        slice_fraction = saturate(log(view_depth / near_plane) / log(_fog_far / near_plane));
    }
    // The froxel grid uses bottom-left NDC UVs; screen textures use top-left UVs.
    float boundary_index = slice_fraction * accum_depth;
    float4 scattering_transmittance;
    if (boundary_index < 1.0f)
    {
        float3 first_slice_uvw = float3(input.uv.x, 1.0 - input.uv.y, 0.5f / accum_depth);
        float4 first_slice = _VolumetricFogAccum.SampleLevel(g_LinearClampSampler, first_slice_uvw, 0);
        scattering_transmittance = lerp(float4(0.0f, 0.0f, 0.0f, 1.0f), first_slice, boundary_index);
    }
    else
    {
        // Each stored value is after a slice's far boundary; offset the boundary index to texel centers.
        float sample_z = (boundary_index - 0.5f) / accum_depth;
        float3 fog_uvw = float3(input.uv.x, 1.0 - input.uv.y, sample_z);
        scattering_transmittance = _VolumetricFogAccum.SampleLevel(g_LinearClampSampler, fog_uvw, 0);
    }
    return float4(scattering_transmittance.rgb * GetPreExposure(), saturate(scattering_transmittance.a));
}
