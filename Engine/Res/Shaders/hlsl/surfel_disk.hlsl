//info begin
//pass begin::
//name: SurfelDisk
//vert: VSMain
//pixel: PSMain
//Cull: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
//ZTest: LEqual
//ZWrite: Off
//pass end::
//info end

#include "common.hlsli"
#include "surfel_common.hlsli"

StructuredBuffer<SurfelData> _SurfelPool;
StructuredBuffer<SurfelLightingData> _SurfelLighting;

PerMaterialCBufferBegin
    int _DebugCascade;
    int _DebugShowIrradiance;
    float _DebugIrradianceExposure;
    int _DebugOpaqueDisks;
    int _DebugLightingMode;
    uint _DebugFrameIndex;
PerMaterialCBufferEnd

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    uint visible : TEXCOORD0;
};

static const float3 kCascadeColors[5] = {
    float3(0.15f, 0.85f, 1.0f),
    float3(0.25f, 1.0f, 0.25f),
    float3(1.0f, 0.85f, 0.15f),
    float3(1.0f, 0.35f, 0.15f),
    float3(0.85f, 0.25f, 1.0f)
};
float3 BuildSurfelTangent(float3 normal)
{
    float3 axis = abs(normal.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    return normalize(cross(axis, normal));
}

PSInput VSMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    PSInput result;
    SurfelData surfel = _SurfelPool[instance_id];
    result.visible = (surfel._flags & 1u) != 0u && (_DebugCascade < 0 || (uint)_DebugCascade == surfel._cascade_level);
    uint segment = vertex_id / 3u;
    uint corner = vertex_id % 3u;
    float2 ring_uv[3] = {
        float2(0.0f, 0.0f),
        float2(cos(2.0f * PI * (float)segment / 16.0f), sin(2.0f * PI * (float)segment / 16.0f)),
        float2(cos(2.0f * PI * (float)(segment + 1u) / 16.0f), sin(2.0f * PI * (float)(segment + 1u) / 16.0f))
    };
    float3 normal = normalize(surfel._normal);
    float3 tangent = BuildSurfelTangent(normal);
    float3 bitangent = cross(normal, tangent);
    float2 local_position = ring_uv[corner] * surfel._radius;
    float3 world_position = surfel._position + tangent * local_position.x + bitangent * local_position.y;
    world_position += normal * 0.001f; // offset to avoid z-fighting with the surface
    result.position = result.visible ? TransformWorldToHClipNoJitter(world_position) : float4(0.0f, 0.0f, 2.0f, 1.0f);
    uint color_index = min(surfel._cascade_level, 4u);
    float3 color = kCascadeColors[color_index];
    if (_DebugLightingMode == 1 || (_DebugLightingMode == 0 && _DebugShowIrradiance != 0))
    {
        SurfelLightingData lighting = _SurfelLighting[instance_id];
        if ((lighting._flags & kSurfelLightingValid) == 0u)
        {
            color = float3(0.5f, 0.0f, 0.35f);
        }
        else
        {
            color = max(lighting._irradiance, 0.0f.xxx) * max(_DebugIrradianceExposure, 0.0f);
            color = color / (1.0f.xxx + color);
        }
        result.color = float4(color, _DebugOpaqueDisks != 0 ? 1.0f : 0.9f);
        return result;
    }

    if (_DebugLightingMode == 2 || _DebugLightingMode == 3)
    {
        SurfelLightingData lighting = _SurfelLighting[instance_id];
        if ((lighting._flags & kSurfelLightingValid) == 0u)
        {
            color = 0.0f.xxx;
        }
        else if (_DebugLightingMode == 2)
        {
            const float t = saturate(log2((float)lighting._sample_count + 1.0f) / 8.0f);
            color = lerp(float3(0.02f, 0.0f, 0.16f), float3(0.0f, 0.1f, 1.0f), saturate(t * 2.0f));
            color = lerp(color, float3(0.0f, 1.0f, 0.85f), saturate((t - 0.25f) * 2.0f));
            color = lerp(color, float3(0.2f, 1.0f, 0.0f), saturate((t - 0.5f) * 2.0f));
            color = lerp(color, float3(1.0f, 0.8f, 0.0f), saturate((t - 0.7f) * 3.33f));
            color = lerp(color, float3(1.0f, 1.0f, 1.0f), saturate((t - 0.9f) * 10.0f));
        }
        else
        {
            const uint age = _DebugFrameIndex - lighting._last_update_frame;
            const float t = saturate(log2((float)age + 1.0f) / 8.0f);
            color = lerp(float3(0.0f, 0.25f, 1.0f), float3(1.0f, 0.0f, 0.0f), t);
        }
        result.color = float4(color, _DebugOpaqueDisks != 0 ? 1.0f : 0.9f);
        return result;
    }

    if ((surfel._flags & kSurfelMigratedFiner) != 0u)
        color = float3(0.0f, 1.0f, 1.0f);
    else if ((surfel._flags & kSurfelMigratedCoarser) != 0u)
        color = float3(1.0f, 1.0f, 0.0f);
    else if ((surfel._flags & kSurfelNeedsOverlapResolve) != 0u)
        color = float3(1.0f, 0.0f, 0.0f);
    result.color = float4(color, _DebugOpaqueDisks != 0 ? 1.0f : 0.9f);
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    if (input.visible == 0u)
        discard;
    return input.color;
}
