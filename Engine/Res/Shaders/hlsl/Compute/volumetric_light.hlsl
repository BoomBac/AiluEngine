#pragma kernel LightInjection
#pragma kernel LightIntegration

#include "cs_common.hlsli"
#include "../shadow.hlsli"
#include "../froxel_common.hlsli"

struct FogVolumeVoxel
{
    float3 light;
    float  density;
};

CBUFFER_START(VolumetricLightParams)
    float4x4 _matrix_iv;
    float4x4 _matrix_ip;
    float3   _cam_pos;
    float    _cam_near;
    float    _cam_far;
    float    _FogDensity;
    bool _temporal_reprojection;
    float4x4 _matrix_pre_v;
    float4x4 _matrix_pre_p;
    float _temporal_blend_factor;
    float2 _zmax_uv_scale;
    float _g;
    float _intensity;
CBUFFER_END

RWTEXTURE3D(_VolumetricLight,float4)
TEXTURE3D(_History_VolumetricLight)
RWTEXTURE3D(_FogAccum,float4)
TEXTURE2D(_BlueNoise)
//TEXTURE2D(_MaxZ_Texture)

// float SampleBlueNoise(uint2 pixel,uint frame)
// {
//     uint2 uv = (pixel + uint2(frame*17, frame*23)) % 1024;
//     return LOAD_TEXTURE2D(_BlueNoise, uv).r;
// }
float3 SampleBlueNoise(uint2 pixel, uint frame)
{
    uint hash = frame * 1664525u + 1013904223u;
    uint2 uv = (pixel + uint2(hash, hash >> 16)) & 1023;
    return _BlueNoise.Load(int3(uv, 0)).rgb;
}
float PhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(denom, 1.5));
}

[numthreads(16,16,4)]
void LightInjection(CSInput input)
{
    uint w, h, d;
    _VolumetricLight.GetDimensions(w, h, d);

    if (any(input.DispatchThreadID >= uint3(w, h, d)))
        return;

    uint3 idx = input.DispatchThreadID;

    float3 uvw = (float3(idx) + 0.5) / float3(w, h, d);

    float3 blue = SampleBlueNoise(idx.xy, _FrameIndex);
    blue = blue - 0.5;
    float jitter = blue.r * 4;
    // ---- 4. Lighting ----
    // _MainlightWorldPosition 在没有方向光（或方向光被禁用）时是零向量：normalize(0) = NaN，
    // 再经 phase 与 `lightColor(0) * NaN` 传播写进 _VolumetricLight，采样它的像素全部变黑。
    // 这里退化成“正上方的虚拟太阳”，方向合法但颜色为 0，不会真的照亮任何东西。
    const float3 mainlight_dir = _MainlightWorldPosition.xyz;
    const float mainlight_dir_len_sq = dot(mainlight_dir, mainlight_dir);
    float3 lightDir = mainlight_dir_len_sq > 1e-8f
                          ? mainlight_dir * rsqrt(mainlight_dir_len_sq)
                          : float3(0.0f, 1.0f, 0.0f);
    float3 lightColor = _DirectionalLights[0]._LightColor;
    float3 worldPos = VolumetricVoxelUVWToWorld(uvw, _matrix_iv, _matrix_ip, _cam_near, _cam_far);
    float3 view_dir = normalize(worldPos - _cam_pos);
    float cosTheta = dot(view_dir, lightDir);
    float phase = PhaseHG(cosTheta, clamp(_g, -0.999f, 0.999f));
    float3 world_pos_no_jitter = worldPos;
    worldPos += view_dir * jitter * SliceThickness(idx.z, _cam_near, _cam_far);
    float shadow = ApplyCascadeShadowHard(1.0, _cam_pos, worldPos, _DirectionalLights[0]._ShadowDistance,
                                           _DirectionalLights[0]._constant_bias, _DirectionalLights[0]._slope_bias);
    float slice_far = SliceDistance(idx.z + 1, _cam_near, _cam_far);
    float3 injected = lightColor * shadow * phase * _intensity;
    // NaN/Inf 绝不能进入 3D 贴图：一旦进去，采样它的每个像素都会变黑（history 打开时还会一直留着重投影下去）。
    if (any(!isfinite(injected)))
        injected = 0.0f.xxx;
    if (_temporal_reprojection)
    {
        float3 prev_uvw = WorldToVolumetricVoxelUVW(world_pos_no_jitter, _matrix_pre_v, _matrix_pre_p, _cam_near, _cam_far);
        if (any(prev_uvw < 0) || any(prev_uvw > 1))
        {
            _VolumetricLight[idx] = float4(injected, slice_far);
            return;
        }
        float3 prev = _History_VolumetricLight.SampleLevel(g_LinearClampSampler, prev_uvw, 0).rgb;
        if (any(!isfinite(prev)))
            prev = 0.0f.xxx;
        injected = lerp(prev, injected.rgb, _temporal_blend_factor);
    }
    _VolumetricLight[idx] = float4(injected, slice_far);
}

// https://github.com/Unity-Technologies/VolumetricLighting/blob/master/Assets/VolumetricFog/Shaders/Scatter.compute
float4 accumulate(float thickness, float3 accum_scattering, float accum_transmittance, float3 slice_scattering, float slice_density)
{
    const float slice_transmittance = exp(-slice_density * thickness * 0.01f);

    float3 slice_scattering_integral = slice_scattering * (1.0 - slice_transmittance) / max(slice_density, 1e-6f);

    accum_scattering += slice_scattering_integral * accum_transmittance;
    accum_transmittance *= slice_transmittance;

    return float4(accum_scattering, accum_transmittance);
}

float Hash01(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x * (1.0 / 4294967296.0); // [0,1)
}

float3 RandomColor(uint seed)
{
    float r = Hash01(seed);
    float g = Hash01(seed + 1);
    float b = Hash01(seed + 2);
    return float3(r, g, b);
}

float3 GetViewRay(float2 uv)
{
    float4 clip = float4(uv * 2 - 1, 1, 1);
    float4 view = mul(_matrix_ip, clip);
    return view.xyz / view.w;
}


[numthreads(16,16,1)]
void LightIntegration(uint3 id : SV_DispatchThreadID)
{
    uint w, h, d;
    _VolumetricLight.GetDimensions(w, h, d);
    if (id.x >= w || id.y >= h)
        return;
    float2 uv = (float2(id.x, id.y) + 0.5) / float2(w, h);
    uv.y = 1.0 - uv.y;
    uv *= _zmax_uv_scale;
    //float max_z = SAMPLE_TEXTURE2D_LOD(_MaxZ_Texture, g_LinearClampSampler, uv, 0).r;
    //float view_z = LinearEyeDepth(max_z, _cam_near, _cam_far);
    float4 accum_scattering_transmittance = float4(0.0f, 0.0f, 0.0f, 1.0f);
    for (uint z = 0; z < VOXEL_SLICE_COUNT; ++z)
    {
        uint3 cur_id = uint3(id.x, id.y, z);
        float slice_z_near = SliceDistance(z, _cam_near, _cam_far);
        // if (slice_z_near >= view_z)
        // {
        //     _FogAccum[cur_id] = accum_scattering_transmittance;
        //     continue;
        // }
        float4 voxel = _VolumetricLight[cur_id];
        float3 slice_scattering = voxel.xyz;
        float slice_density = _FogDensity;
        float slice_z_far = SliceDistance(z + 1, _cam_near, _cam_far);
        //slice_z_far = min(slice_z_far, view_z);
        accum_scattering_transmittance = accumulate(slice_z_far - slice_z_near, accum_scattering_transmittance.rgb, 
            accum_scattering_transmittance.a, slice_scattering, slice_density);
        _FogAccum[cur_id] = float4(accum_scattering_transmittance.rgb, z);
    }
}
