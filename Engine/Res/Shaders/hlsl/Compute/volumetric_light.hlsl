#pragma kernel LightInjection
#pragma kernel LocalLightInjection
#pragma kernel TemporalLighting
#pragma kernel LightIntegration

#include "cs_common.hlsli"
#include "../shadow.hlsli"
#include "../froxel_common.hlsli"
#include "../punctual_lighting.hlsli"

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
    float _prev_cam_near;
    float _prev_cam_far;
    float _temporal_current_weight;
    float _fog_height;
    float _height_falloff;
    float2 _zmax_uv_scale;
    float _g;
    float _intensity;
    float _ambient_intensity;
    float _jitter_scale;
    float _environment_intensity;
    bool _has_main_light_shadow;
    bool _has_environment_sh;
    uint _local_fog_count;
    uint _medium_debug_mode;
    bool _local_lights_enabled;
    bool _local_light_shadows;
    bool _has_spot_light_shadow;
    bool _has_point_light_shadow;
    uint _local_light_index;
    uint _local_light_type;
    uint4 _bounds_min;
    uint4 _bounds_size;
    float _local_light_intensity;
    bool _local_light_supersampling;
CBUFFER_END

struct FogMedium
{
    float _extinction;
    float3 _scattering;
};

struct LocalFogVolumeData
{
    float4x4 _world_to_local;
    float3 _scattering_albedo;
    float _density;
    float _edge_fade;
    uint _shape;
    uint _padding0;
    uint _padding1;
};
StructuredBuffer<LocalFogVolumeData> _LocalFogVolumes;

FogMedium EvaluateFogMedium(float3 world_pos)
{
    const float exponent = clamp(-(world_pos.y - _fog_height) * _height_falloff, -10.0f, 10.0f);
    FogMedium medium;
    medium._extinction = max(_FogDensity, 0.0f) * 0.01f * exp(exponent);
    if (_medium_debug_mode == 2u)
        medium._extinction = 0.0f;
    medium._scattering = medium._extinction.xxx;
    if (_medium_debug_mode != 1u)
    {
        for (uint volume_index = 0u; volume_index < _local_fog_count; ++volume_index)
        {
            const LocalFogVolumeData volume = _LocalFogVolumes[volume_index];
            const float3 local_pos = mul(volume._world_to_local, float4(world_pos, 1.0f)).xyz;
            const float3 box_edge = 0.5f - abs(local_pos);
            const float distance_to_edge = volume._shape == 0u
                ? min(box_edge.x, min(box_edge.y, box_edge.z)) : 1.0f - length(local_pos);
            const float weight = saturate(distance_to_edge / max(volume._edge_fade, 1e-4f));
            const float local_extinction = max(volume._density, 0.0f) * weight;
            medium._extinction += local_extinction;
            medium._scattering += local_extinction * saturate(volume._scattering_albedo);
        }
    }
    return medium;
}

RWTEXTURE3D(_VolumetricLight,float4)
RWTEXTURE3D(_FogMedium,float4)
TEXTURE3D(_History_VolumetricLight)
RWTEXTURE3D(_FogAccum,float4)
TEXTURE2D(_BlueNoise)
StructuredBuffer<float4> g_environment_sh : register(t18);
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

float EvaluatePunctualFogWeight(float3 light_pos, float range, float3 world_pos,
    float3 sample_to_camera_dir, out float3 light_travel_dir)
{
    const float3 light_to_sample = world_pos - light_pos;
    const float light_distance = sqrt(max(dot(light_to_sample, light_to_sample), 1e-8f));
    light_travel_dir = light_to_sample / light_distance;
    return GetDistanceAtt(light_distance, range) *
        PhaseHG(dot(light_travel_dir, sample_to_camera_dir), clamp(_g, -0.999f, 0.999f)) * max(_intensity, 0.0f);
}

float3 EvaluateSpotFogLighting(uint light_index, float3 world_pos, float3 sample_to_camera_dir)
{
    const ShaderSpotlLightData light = _SpotLights[light_index];
    float3 light_travel_dir;
    const float weight = EvaluatePunctualFogWeight(light._LightPos, light._Rdius, world_pos,
        sample_to_camera_dir, light_travel_dir);
    const float attenuation = weight *
        GetAngleAtt(light_travel_dir, light._LightDir, light._LightAngleScale, light._LightAngleOffset);
    float shadow = 1.0f;
    if (_local_light_shadows && _has_spot_light_shadow && light._shadowmap_index >= 0 && attenuation > 0.0f)
    {
        const float4 shadow_pos = mul(light._shadow_matrix, float4(world_pos, 1.0f));
        if (shadow_pos.w > 1e-5f && all(abs(shadow_pos.xy) <= shadow_pos.w) &&
            shadow_pos.z >= 0.0f && shadow_pos.z <= shadow_pos.w)
            shadow = ApplyShadowAddLight(shadow_pos, 1.0f, world_pos, light._shadowmap_index,
                light._constant_bias, light._slope_bias);
    }
    return max(light._LightColor, 0.0f.xxx) * attenuation * shadow;
}

float3 EvaluatePointFogLighting(uint light_index, float3 world_pos, float3 sample_to_camera_dir)
{
    const ShaderDirectionalAndPointLightData light = _PointLights[light_index];
    float3 light_travel_dir;
    const float attenuation = EvaluatePunctualFogWeight(light._LightPosOrDir, light._LightParam0,
        world_pos, sample_to_camera_dir, light_travel_dir);
    float shadow = 1.0f;
    if (_local_light_shadows && _has_point_light_shadow && light._shadowmap_index >= 0 && attenuation > 0.0f)
        shadow = ApplyShadowPointLightIndexed(0.01f, light._ShadowDistance, 1.0f, world_pos,
            light._LightPosOrDir, light_index, light._shadowmap_index);
    return max(light._LightColor, 0.0f.xxx) * attenuation * shadow;
}

float3 EvaluateFogLighting(float3 world_pos, float3 sample_to_camera_dir)
{
    const bool has_main_light = _ActiveLightCount.x > 0.0f;
    // Renderer stores the directional light's travel direction here; surface BRDFs negate it to get
    // the direction from the shaded point toward the light source.
    const float3 mainlight_dir = has_main_light ? _MainlightWorldPosition.xyz : 0.0f.xxx;
    const float mainlight_dir_len_sq = dot(mainlight_dir, mainlight_dir);
    float3 light_travel_dir = mainlight_dir_len_sq > 1e-8f
                                  ? mainlight_dir * rsqrt(mainlight_dir_len_sq)
                                  : float3(0.0f, 1.0f, 0.0f);
    float3 light_color = has_main_light ? _DirectionalLights[0]._LightColor : 0.0f.xxx;
    const float cos_theta = dot(light_travel_dir, sample_to_camera_dir);
    const float phase = PhaseHG(cos_theta, clamp(_g, -0.999f, 0.999f));
    float shadow = 1.0f;
    if (has_main_light && _has_main_light_shadow)
    {
        shadow = ApplyCascadeShadowHard(1.0f, _cam_pos, world_pos, _DirectionalLights[0]._ShadowDistance,
                                        _DirectionalLights[0]._constant_bias, _DirectionalLights[0]._slope_bias);
    }
    float3 directional_source = has_main_light ? light_color * shadow * phase * _intensity : 0.0f.xxx;
    // SH L0 is an isotropic approximation of the environment; keep it in the source term so it
    // receives the same medium extinction and froxel integration as directional in-scattering.
    float3 environment_source = _has_environment_sh
        ? max(g_environment_sh[0].rgb, 0.0f.xxx) * 0.2820947918f *
              max(_environment_intensity, 0.0f) * max(_ambient_intensity, 0.0f)
        : 0.0f.xxx;
    return directional_source + environment_source;
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
    float jitter = (blue.r - 0.5f) * _jitter_scale;
    float3 world_pos = VolumetricVoxelUVWToWorld(uvw, _matrix_iv, _matrix_ip, _cam_near, _cam_far);
    const float3 sample_to_camera_dir = normalize(_cam_pos - world_pos);
    const float3 world_pos_no_jitter = world_pos;
    world_pos += -sample_to_camera_dir * jitter * SliceThickness(idx.z, _cam_near, _cam_far, d);
    const FogMedium medium = EvaluateFogMedium(world_pos);
    _FogMedium[idx] = float4(medium._scattering, medium._extinction);
    // RGB is the source function; alpha belongs exclusively to the current medium.
    const float3 scattering_albedo = medium._extinction > 0.0f
        ? medium._scattering / medium._extinction : 0.0f.xxx;
    float3 injected = EvaluateFogLighting(world_pos, sample_to_camera_dir) * scattering_albedo;
    // NaN/Inf 绝不能进入 3D 贴图：一旦进去，采样它的每个像素都会变黑（history 打开时还会一直留着重投影下去）。
    if (any(!isfinite(injected)))
        injected = 0.0f.xxx;
    _VolumetricLight[idx] = float4(injected, medium._extinction);
}

[numthreads(8,8,4)]
void LocalLightInjection(CSInput input)
{
    if (any(input.DispatchThreadID >= _bounds_size.xyz))
        return;
    const uint3 idx = input.DispatchThreadID + _bounds_min.xyz;
    uint w, h, d;
    _VolumetricLight.GetDimensions(w, h, d);
    if (any(idx >= uint3(w, h, d)))
        return;
    const float4 medium = _FogMedium[idx];
    if (medium.a <= 0.0f)
        return;
    const float3 uvw = (float3(idx) + .5f) / float3(w, h, d);
    float3 radiance = 0.0f.xxx;
    if (_local_light_supersampling)
    {
        // Integrate over the cell instead of testing a narrow cone at one jittered point. Fixed stratified
        // samples remain stable without temporal reprojection, including at the light's apex.
        const float center_depth = _cam_near * pow(_cam_far / _cam_near, uvw.z);
        const float near_depth = SliceDistance(idx.z, _cam_near, _cam_far, d);
        const float far_depth = SliceDistance(idx.z + 1u, _cam_near, _cam_far, d);
        [unroll]
        for (uint sample_index = 0u; sample_index < 8u; ++sample_index)
        {
            const float2 offset = float2((sample_index & 1u) ? .25f : -.25f,
                (sample_index & 2u) ? .25f : -.25f);
            const float3 sample_uvw = float3(uvw.xy + offset / float2(w, h), uvw.z);
            const float sample_depth = lerp(near_depth, far_depth, (sample_index & 4u) ? .75f : .25f);
            float3 world_pos = VolumetricVoxelUVWToWorld(sample_uvw, _matrix_iv, _matrix_ip, _cam_near, _cam_far);
            world_pos = _cam_pos + (world_pos - _cam_pos) * (sample_depth / center_depth);
            const float3 sample_to_camera_dir = normalize(_cam_pos - world_pos);
            radiance += _local_light_type == 0u
                ? EvaluatePointFogLighting(_local_light_index, world_pos, sample_to_camera_dir)
                : EvaluateSpotFogLighting(_local_light_index, world_pos, sample_to_camera_dir);
        }
        radiance *= .125f;
    }
    else
    {
        float3 world_pos = VolumetricVoxelUVWToWorld(uvw, _matrix_iv, _matrix_ip, _cam_near, _cam_far);
        const float3 sample_to_camera_dir = normalize(_cam_pos - world_pos);
        const float jitter = (SampleBlueNoise(idx.xy, _FrameIndex).r - .5f) * _jitter_scale;
        world_pos -= sample_to_camera_dir * jitter * SliceThickness(idx.z, _cam_near, _cam_far, d);
        radiance = _local_light_type == 0u
            ? EvaluatePointFogLighting(_local_light_index, world_pos, sample_to_camera_dir)
            : EvaluateSpotFogLighting(_local_light_index, world_pos, sample_to_camera_dir);
    }
    const float3 contribution = radiance * max(_local_light_intensity, 0.0f) * (medium.rgb / medium.a);
    if (all(isfinite(contribution)))
    {
        float4 lighting = _VolumetricLight[idx];
        lighting.rgb += contribution;
        _VolumetricLight[idx] = lighting;
    }
}

[numthreads(8,8,4)]
void TemporalLighting(CSInput input)
{
    uint w, h, d;
    _VolumetricLight.GetDimensions(w, h, d);
    const uint3 idx = input.DispatchThreadID;
    if (any(idx >= uint3(w, h, d)))
        return;
    const float3 uvw = (float3(idx) + .5f) / float3(w, h, d);
    const float3 world_pos_no_jitter = VolumetricVoxelUVWToWorld(uvw, _matrix_iv, _matrix_ip, _cam_near, _cam_far);
    const float4 current = _VolumetricLight[idx];
    float3 injected = current.rgb;
    if (_temporal_reprojection)
    {
        float4 prev_clip;
        float3 prev_view_pos;
        float3 prev_uvw = WorldToVolumetricVoxelUVW(world_pos_no_jitter, _matrix_pre_v, _matrix_pre_p,
                                                     _prev_cam_near, _prev_cam_far, prev_clip, prev_view_pos);
        const float prev_depth = abs(prev_view_pos.z);
        const bool valid_prev_clip = prev_clip.w > 1e-5f && prev_view_pos.z > 0.0f;
        const bool valid_prev_depth = prev_depth >= _prev_cam_near && prev_depth <= _prev_cam_far;
        const bool valid_prev_uv = all(prev_uvw.xy >= 0.0f) && all(prev_uvw.xy <= 1.0f);
        const bool valid_prev_slice = prev_uvw.z >= 0.0f && prev_uvw.z <= 1.0f;
        if (!valid_prev_clip || !valid_prev_depth || !valid_prev_uv || !valid_prev_slice)
        {
            _VolumetricLight[idx] = float4(injected, current.a);
            return;
        }
        const float4 history_sample = _History_VolumetricLight.SampleLevel(g_LinearClampSampler, prev_uvw, 0);
        const float relative_medium_difference = abs(current.a - history_sample.a) /
            max(max(current.a, history_sample.a), 1e-4f);
        if (!isfinite(history_sample.a) || relative_medium_difference > 0.25f)
        {
            _VolumetricLight[idx] = float4(injected, current.a);
            return;
        }
        float3 prev = history_sample.rgb;
        if (any(!isfinite(prev)))
            prev = 0.0f.xxx;
        const float current_luma = dot(max(injected, 0.0f.xxx), float3(0.2126f, 0.7152f, 0.0722f));
        const float history_luma = dot(max(prev, 0.0f.xxx), float3(0.2126f, 0.7152f, 0.0722f));
        const float relative_luma_difference = abs(current_luma - history_luma) /
                                               max(max(current_luma, history_luma), 0.01f);
        const float history_confidence = saturate(1.0f - relative_luma_difference / 0.35f);
        const float current_weight = 1.0f - (1.0f - saturate(_temporal_current_weight)) * history_confidence;
        injected = lerp(prev, injected.rgb, current_weight);
    }
    _VolumetricLight[idx] = float4(injected, current.a);
}

// https://github.com/Unity-Technologies/VolumetricLighting/blob/master/Assets/VolumetricFog/Shaders/Scatter.compute
float4 AccumulateFog(float thickness, float3 accum_scattering, float accum_transmittance,
                     float3 slice_scattering, float slice_extinction)
{
    const float optical_depth = max(slice_extinction, 0.0f) * max(thickness, 0.0f);
    const float slice_transmittance = exp(-optical_depth);
    // Avoid cancellation near zero density. Injected lighting is the source function (unit scattering albedo),
    // so density scales both scattering and extinction through optical depth; no division by density is needed.
    const float slice_opacity = optical_depth < 1e-3f
        ? optical_depth * (1.0f - 0.5f * optical_depth + optical_depth * optical_depth / 6.0f)
        : 1.0f - slice_transmittance;
    float3 slice_scattering_integral = slice_scattering * slice_opacity;

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
    float2 froxel_uv = (float2(id.x, id.y) + 0.5) / float2(w, h);
    float3 view_ray = normalize(GetViewRay(froxel_uv));
    float ray_z = max(abs(view_ray.z), 1e-4f);
    float2 uv = froxel_uv;
    uv.y = 1.0 - uv.y;
    uv *= _zmax_uv_scale;
    //float max_z = SAMPLE_TEXTURE2D_LOD(_MaxZ_Texture, g_LinearClampSampler, uv, 0).r;
    //float view_z = LinearEyeDepth(max_z, _cam_near, _cam_far);
    float4 accum_scattering_transmittance = float4(0.0f, 0.0f, 0.0f, 1.0f);
    for (uint z = 0; z < d; ++z)
    {
        uint3 cur_id = uint3(id.x, id.y, z);
        float slice_z_near = SliceDistance(z, _cam_near, _cam_far, d);
        // if (slice_z_near >= view_z)
        // {
        //     _FogAccum[cur_id] = accum_scattering_transmittance;
        //     continue;
        // }
        float4 voxel = _VolumetricLight[cur_id];
        float3 slice_scattering = voxel.xyz;
        float slice_extinction = voxel.a;
        float slice_z_far = SliceDistance(z + 1, _cam_near, _cam_far, d);
        //slice_z_far = min(slice_z_far, view_z);
        float thickness = (slice_z_far - slice_z_near) / ray_z;
        accum_scattering_transmittance = AccumulateFog(thickness, accum_scattering_transmittance.rgb,
            accum_scattering_transmittance.a, slice_scattering, slice_extinction);
        _FogAccum[cur_id] = accum_scattering_transmittance;
    }
}
