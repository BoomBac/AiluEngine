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

struct SurfelData
{
    float3 _position;
    float _radius;
    float3 _normal;
    uint _flags;
    uint _cascade_level;
    uint _last_touched_frame;
    uint _object_id;
    uint _padding;
    uint2 _spawn_pixel;
};

StructuredBuffer<SurfelData> _SurfelPool;

PerMaterialCBufferBegin
    int _DebugCascade;
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
    result.color = float4(kCascadeColors[color_index], 0.9f);
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    if (input.visible == 0u)
        discard;
    return input.color;
}
