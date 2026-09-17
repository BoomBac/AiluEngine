//info begin
//pass begin::
//name: DDGIProbeSphere
//vert: DDGIProbeSphereVS
//pixel: DDGIProbeSpherePS
//Cull: Back
//ZWrite: Off
//Queue: Transparent
//ZTest: LEqual
//Blend: Src,OneMinusSrc
//pass end::
//pass begin::
//name: DDGIProbeRayLine
//vert: DDGIProbeRayLineVS
//pixel: DDGIProbeRayLinePS
//Cull: Off
//ZWrite: Off
//Topology: Line
//Queue: Transparent
//ZTest: LEqual
//Blend: Src,OneMinusSrc
//pass end::
//pass begin::
//name: DDGIProbeRelocationBaseSphere
//vert: DDGIProbeRelocationBaseSphereVS
//pixel: DDGIProbeRelocationBaseSpherePS
//Cull: Back
//ZWrite: Off
//Queue: Transparent
//ZTest: LEqual
//Blend: Src,OneMinusSrc
//pass end::
//pass begin::
//name: DDGIProbeRelocationLine
//vert: DDGIProbeRelocationLineVS
//pixel: DDGIProbeRelocationLinePS
//Cull: Off
//ZWrite: Off
//Topology: Line
//Queue: Transparent
//ZTest: LEqual
//Blend: Src,OneMinusSrc
//pass end::
//info end

#include "ddgi_common.hlsli"

StructuredBuffer<DDGIProbeRayData> _probe_ray_data : register(t0);
StructuredBuffer<DDGIProbeData> _probe_data : register(t1);
Texture2D<float4> _radiance_texture : register(t2);
Texture2D<float2> _distance_texture : register(t3);

PerMaterialCBufferBegin
    float4x4 _volume_local_to_world;
    float4 _probe_spacing_and_distance;
    uint _probe_count_x;
    uint _probe_count_y;
    uint _probe_count_z;
    uint _debug_mode;
    uint _frame_index;
    uint _freeze_ray_rotation;
    uint _debug_probe_index;
    float _debug_probe_radius;
    float _debug_ray_length;
    uint _radiance_atlas_tiles_x;
    uint _radiance_atlas_tiles_y;
    uint _radiance_atlas_width;
    uint _radiance_atlas_height;
    uint _distance_atlas_tiles_x;
    uint _distance_atlas_tiles_y;
    uint _distance_atlas_width;
    uint _distance_atlas_height;
    uint _use_probe_relocation;
    uint _draw_relocation_debug;
    uint _debug_only_relocated;
    float _random_ray_backface_threshold;
    uint2 _ddgi_debug_padding;
PerMaterialCBufferEnd

uint3 DDGIGetDebugProbeCount()
{
    return uint3(_probe_count_x, _probe_count_y, _probe_count_z);
}

float3 DDGIGetDebugBaseProbePosition(uint probe_index)
{
    const uint3 probe_count = DDGIGetDebugProbeCount();
    const float3 local_position = DDGIProbeLocalPosition(probe_index, probe_count, _probe_spacing_and_distance.xyz);
    return mul(_volume_local_to_world, float4(local_position, 1.0f)).xyz;
}

float3 DDGIGetProbeRelocationOffset(uint probe_index)
{
    return _use_probe_relocation != 0u ? _probe_data[probe_index]._relocation_offset : 0.0f.xxx;
}

float3 DDGIGetDebugProbePosition(uint probe_index)
{
    const float3 base_position = DDGIGetDebugBaseProbePosition(probe_index);
    const float3 offset = DDGIGetProbeRelocationOffset(probe_index);
    return base_position + mul((float3x3)_volume_local_to_world, offset);
}

bool DDGIIsRelocated(uint probe_index)
{
    const float3 offset = DDGIGetProbeRelocationOffset(probe_index);
    return dot(offset, offset) > 1e-8f;
}

float3 DDGIGetDistanceDebugColor(float signed_distance, float max_ray_distance)
{
    if (DDGIIsMiss(signed_distance, max_ray_distance))
    {
        return float3(0.65f, 0.0f, 0.9f);
    }

    const float normalized_distance = saturate(abs(signed_distance) / max(max_ray_distance, 0.001f));
    const float3 near_color = float3(1.0f, 0.0f, 0.0f);
    const float3 middle_color = float3(0.0f, 1.0f, 0.0f);
    const float3 far_color = float3(0.0f, 0.0f, 1.0f);
    float3 color = normalized_distance < 0.5f
                       ? lerp(near_color, middle_color, normalized_distance * 2.0f)
                       : lerp(middle_color, far_color, normalized_distance * 2.0f - 1.0f);
    if (DDGIIsBackFaceHit(signed_distance, max_ray_distance))
    {
        color = lerp(color, float3(1.0f, 0.0f, 1.0f), 0.65f);
    }
    return color;
}

float3 DDGIGetHitTypeDebugColor(float signed_distance, float max_ray_distance)
{
    if (DDGIIsMiss(signed_distance, max_ray_distance))
    {
        return float3(0.0f, 0.25f, 1.0f);
    }
    return DDGIIsBackFaceHit(signed_distance, max_ray_distance)
               ? float3(1.0f, 0.0f, 0.0f)
               : float3(0.0f, 1.0f, 0.0f);
}

float3 DDGISampleProbeIrradiance(uint probe_index, float3 direction)
{
    if (_radiance_atlas_tiles_x == 0u || _radiance_atlas_tiles_y == 0u ||
        _radiance_atlas_width == 0u || _radiance_atlas_height == 0u)
    {
        return 0.0f.xxx;
    }

    const float2 oct_uv = DDGIOctEncode(direction);
    const uint2 tile_origin = uint2(probe_index % _radiance_atlas_tiles_x,
                                    probe_index / _radiance_atlas_tiles_x) * kRadiancePhysicalResolution;
    const float2 atlas_pixel = (float2)tile_origin + kRadianceBorderSize +
                               oct_uv * (float)kRadianceInteriorResolution;
    const float2 atlas_uv = atlas_pixel / float2(_radiance_atlas_width, _radiance_atlas_height);
    return _radiance_texture.SampleLevel(g_LinearClampSampler, atlas_uv, 0.0f).rgb;
}

float2 DDGISampleProbeDistance(uint probe_index, float3 direction)
{
    if (_distance_atlas_tiles_x == 0u || _distance_atlas_tiles_y == 0u ||
        _distance_atlas_width == 0u || _distance_atlas_height == 0u)
    {
        return 0.0f.xx;
    }

    const float2 oct_uv = DDGIOctEncode(direction);
    const uint2 tile_origin = uint2(probe_index % _distance_atlas_tiles_x,
                                    probe_index / _distance_atlas_tiles_x) * kDistancePhysicalResolution;
    const float2 atlas_pixel = (float2)tile_origin + kDistanceBorderSize +
                               oct_uv * (float)kDistanceInteriorResolution;
    const float2 atlas_uv = atlas_pixel / float2(_distance_atlas_width, _distance_atlas_height);
    return _distance_texture.SampleLevel(g_LinearClampSampler, atlas_uv, 0.0f);
}

float3 DDGIGetRadianceDebugColor(float3 radiance)
{
    return max(radiance, 0.0f.xxx) / (1.0f.xxx + max(radiance, 0.0f.xxx));
}

float3 DDGIGetIrradianceChromaticityDebugColor(float3 irradiance)
{
    irradiance = max(irradiance, 0.0f.xxx);

    const float max_component = max(irradiance.r, max(irradiance.g, irradiance.b));
    if (max_component < 1e-5f)
    {
        return 0.0f.xxx;
    }

    return irradiance / max_component;
}

float3 DDGIGetDistanceMeanDebugColor(float mean_distance, float max_ray_distance)
{
    const float normalized_distance = saturate(mean_distance / max(max_ray_distance, 0.001f));
    return normalized_distance < 0.5f
               ? lerp(float3(1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), normalized_distance * 2.0f)
               : lerp(float3(0.0f, 1.0f, 0.0f), float3(0.0f, 0.0f, 1.0f), normalized_distance * 2.0f - 1.0f);
}

float3 DDGIGetDistanceVarianceDebugColor(float2 moments, float max_ray_distance)
{
    const float variance = max(moments.y - moments.x * moments.x, 0.0f);
    const float normalized_variance = saturate(variance / max(max_ray_distance * max_ray_distance, 0.001f));
    return lerp(float3(0.0f, 0.0f, 1.0f), float3(1.0f, 0.0f, 0.0f), normalized_variance);
}


float3 DDGIGetDebugColor(DDGIProbeRayData ray_data, float3 ray_direction, uint probe_index)
{
    const float max_ray_distance = max(_probe_spacing_and_distance.w, 0.001f);
    if (_debug_mode == kDDGIProbeDebugModeRadiance)
    {
        return DDGIGetRadianceDebugColor(ray_data._radiance);
    }
    if (_debug_mode == kDDGIProbeDebugModeIrradiance)
    {
        return DDGIGetRadianceDebugColor(DDGISampleProbeIrradiance(probe_index, ray_direction));
    }
    if (_debug_mode == kDDGIProbeDebugModeDistanceMean)
    {
        return DDGIGetDistanceMeanDebugColor(DDGISampleProbeDistance(probe_index, ray_direction).x, max_ray_distance);
    }
    if (_debug_mode == kDDGIProbeDebugModeDistanceVariance)
    {
        return DDGIGetDistanceVarianceDebugColor(DDGISampleProbeDistance(probe_index, ray_direction), max_ray_distance);
    }
    if (_debug_mode == kDDGIProbeDebugModeDistance)
    {
        return DDGIGetDistanceDebugColor(ray_data._distance, max_ray_distance);
    }
    if (_debug_mode == kDDGIProbeDebugModeRayDirection)
    {
        return ray_direction * 0.5f + 0.5f;
    }
    return DDGIGetHitTypeDebugColor(ray_data._distance, max_ray_distance);
}

struct DDGIProbeSphereVSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    uint instance_id : SV_InstanceID;
};

struct DDGIProbeSpherePSInput
{
    float4 position : SV_POSITION;
    nointerpolation uint probe_index : TEXCOORD0;
    float3 world_position : TEXCOORD1;
    float3 world_normal : TEXCOORD2;
    nointerpolation float3 probe_position : TEXCOORD3;
    nointerpolation uint is_relocated : TEXCOORD4;
};

DDGIProbeSpherePSInput DDGIProbeSphereVS(DDGIProbeSphereVSInput input)
{
    DDGIProbeSpherePSInput output;
    const float3 probe_position = DDGIGetDebugProbePosition(input.instance_id);
    const float3 world_position = probe_position + input.position * _debug_probe_radius;
    output.position = TransformWorldToHClipNoJitter(world_position);
    output.probe_index = input.instance_id;
    output.world_position = world_position;
    output.world_normal = normalize(mul((float3x3)transpose(_volume_local_to_world), input.normal));
    output.probe_position = probe_position;
    output.is_relocated = DDGIIsRelocated(input.instance_id) ? 1u : 0u;
    return output;
}

float4 DDGIProbeSpherePS(DDGIProbeSpherePSInput input) : SV_TARGET
{
    if (_debug_only_relocated != 0u && input.is_relocated == 0u)
        discard;
    if (_debug_only_relocated != 0u)
        return float4(0.1f, 1.0f, 0.2f, 1.0f);
    if (_draw_relocation_debug != 0u)
    {
        const float state = _probe_data[input.probe_index]._state;
        if (state >= kDDGIProbeStateBackfaceRejected)
            return float4(1.0f, 0.0f, 0.0f, 1.0f);
        if (state >= kDDGIProbeStateBackfaceNear)
            return float4(1.0f, 0.35f, 0.0f, 1.0f);
        if (state >= kDDGIProbeStateRelocated || DDGIIsRelocated(input.probe_index))
            return float4(1.0f, 0.85f, 0.0f, 1.0f);
        return float4(0.0f, 1.0f, 0.15f, 1.0f);
    }
    const float3 sphere_direction = normalize(input.world_position - input.probe_position);
    if (_debug_mode == kDDGIProbeDebugModeIrradiance)
    {
        return float4(DDGIGetIrradianceChromaticityDebugColor(
            DDGISampleProbeIrradiance(input.probe_index, sphere_direction)), 1.0f);
    }
    if (_debug_mode == kDDGIProbeDebugModeDistanceMean)
    {
        return float4(DDGIGetDistanceMeanDebugColor(
            DDGISampleProbeDistance(input.probe_index, sphere_direction).x, _probe_spacing_and_distance.w), 1.0f);
    }
    if (_debug_mode == kDDGIProbeDebugModeDistanceVariance)
    {
        return float4(DDGIGetDistanceVarianceDebugColor(
            DDGISampleProbeDistance(input.probe_index, sphere_direction), _probe_spacing_and_distance.w), 1.0f);
    }

    const float3x3 ray_rotation = DDGIGetRayRotation(_frame_index, _freeze_ray_rotation, input.probe_index);
    float max_dot = -1.0f;
    uint closest_ray_index = 0u;
    [unroll]
    for (uint ray_index = 0u; ray_index < kRaysPerProbe; ++ray_index)
    {
        const float3 ray_direction = DDGIGetProbeRayDirection(ray_index, ray_rotation, _use_probe_relocation);
        const float similarity = dot(sphere_direction, ray_direction);
        if (similarity > max_dot)
        {
            max_dot = similarity;
            closest_ray_index = ray_index;
        }
    }

    const uint data_index = DDGIRayDataIndex(input.probe_index, closest_ray_index);
    const DDGIProbeRayData ray_data = _probe_ray_data[data_index];
    const float3 selected_direction = DDGIGetProbeRayDirection(closest_ray_index, ray_rotation,
                                                                _use_probe_relocation);
    return float4(DDGIGetDebugColor(ray_data, selected_direction, input.probe_index), 1.0f);
}

DDGIProbeSpherePSInput DDGIProbeRelocationBaseSphereVS(DDGIProbeSphereVSInput input)
{
    DDGIProbeSpherePSInput output;
    const float3 base_position = DDGIGetDebugBaseProbePosition(input.instance_id);
    const float3 world_position = base_position + input.position * _debug_probe_radius * 0.6f;
    output.position = TransformWorldToHClipNoJitter(world_position);
    output.probe_index = input.instance_id;
    output.world_position = world_position;
    output.world_normal = normalize(mul((float3x3)transpose(_volume_local_to_world), input.normal));
    output.probe_position = base_position;
    output.is_relocated = DDGIIsRelocated(input.instance_id) ? 1u : 0u;
    return output;
}

float4 DDGIProbeRelocationBaseSpherePS(DDGIProbeSpherePSInput input) : SV_TARGET
{
    if (input.is_relocated == 0u)
        discard;
    return float4(1.0f, 0.65f, 0.05f, 1.0f);
}

struct DDGIProbeRayLineVSInput
{
    uint vertex_id : SV_VertexID;
    uint instance_id : SV_InstanceID;
};

struct DDGIProbeRayLinePSInput
{
    float4 position : SV_POSITION;
    nointerpolation float4 color : COLOR0;
};

DDGIProbeRayLinePSInput DDGIProbeRayLineVS(DDGIProbeRayLineVSInput input)
{
    const bool is_selected_probe = _debug_probe_index != kDDGIInvalidProbeIndex;
    const uint probe_index = is_selected_probe ? _debug_probe_index : input.instance_id / kRaysPerProbe;
    const uint ray_index = is_selected_probe ? input.instance_id : input.instance_id - probe_index * kRaysPerProbe;
    const uint3 probe_count = DDGIGetDebugProbeCount();
    const uint probe_count_total = probe_count.x * probe_count.y * probe_count.z;
    const bool valid_probe = probe_index < probe_count_total;
    if (!valid_probe)
    {
        DDGIProbeRayLinePSInput invalid_output;
        invalid_output.position = 0.0f.xxxx;
        invalid_output.color = 0.0f.xxxx;
        return invalid_output;
    }

    const float3 probe_position = DDGIGetDebugProbePosition(probe_index);
    const float3x3 ray_rotation = DDGIGetRayRotation(_frame_index, _freeze_ray_rotation, probe_index);
    const float3 ray_direction = DDGIGetProbeRayDirection(ray_index, ray_rotation, _use_probe_relocation);
    const DDGIProbeRayData ray_data = _probe_ray_data[DDGIRayDataIndex(probe_index, ray_index)];
    const float max_ray_distance = max(_probe_spacing_and_distance.w, 0.001f);
    const bool miss = DDGIIsMiss(ray_data._distance, max_ray_distance);
    const float line_length = miss
                                  ? min(max(_debug_ray_length, 0.0f), max_ray_distance)
                                  : min(abs(ray_data._distance), max_ray_distance);
    const float3 world_position = input.vertex_id == 0u
                                      ? probe_position
                                      : probe_position + ray_direction * line_length;

    DDGIProbeRayLinePSInput output;
    output.position = TransformWorldToHClipNoJitter(world_position);
    output.color = float4(DDGIGetDebugColor(ray_data, ray_direction, probe_index), 1.0f);
    return output;
}

float4 DDGIProbeRayLinePS(DDGIProbeRayLinePSInput input) : SV_TARGET
{
    return input.color;
}

struct DDGIProbeRelocationLineVSInput
{
    uint vertex_id : SV_VertexID;
    uint instance_id : SV_InstanceID;
};

struct DDGIProbeRelocationLinePSInput
{
    float4 position : SV_POSITION;
    nointerpolation uint is_relocated : TEXCOORD0;
};

DDGIProbeRelocationLinePSInput DDGIProbeRelocationLineVS(DDGIProbeRelocationLineVSInput input)
{
    DDGIProbeRelocationLinePSInput output;
    const float3 offset = DDGIGetProbeRelocationOffset(input.instance_id);
    output.is_relocated = dot(offset, offset) > 1e-8f ? 1u : 0u;
    const float3 base_position = DDGIGetDebugBaseProbePosition(input.instance_id);
    const float3 relocated_position = DDGIGetDebugProbePosition(input.instance_id);
    const float3 world_position = input.vertex_id == 0u ? base_position : relocated_position;
    output.position = TransformWorldToHClipNoJitter(world_position);
    return output;
}

float4 DDGIProbeRelocationLinePS(DDGIProbeRelocationLinePSInput input) : SV_TARGET
{
    if (input.is_relocated == 0u)
        discard;
    return float4(1.0f, 0.65f, 0.05f, 1.0f);
}
