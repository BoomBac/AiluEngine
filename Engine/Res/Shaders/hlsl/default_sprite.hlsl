//info bein
//pass begin::
//name: sprite_default
//vert: VSMain
//pixel: PSMain
//Cull: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
//ZWrite: Off
//ZTest: LEqual
//pass end::
//info end

#include "common.hlsli"

PerMaterialCBufferBegin
    uint _base_instance_index;
PerMaterialCBufferEnd

struct sprite_instance_data
{
    float4x4 local_to_world;
    float4 uv_rect;
    float4 color;
    float4 size_pivot;
    uint texture_index;
    uint entity_id;
    uint flags;
    uint padding;
};

StructuredBuffer<sprite_instance_data> g_sprite_instances : register(t10);

struct VSInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    uint instance_id : SV_InstanceID;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

TEXTURE2D(_MainTex)

PSInput VSMain(VSInput input)
{
    sprite_instance_data instance = g_sprite_instances[input.instance_id + _base_instance_index];

    float2 unit_position = input.position;
    float2 uv = input.uv;

    // Apply flip flags
    if ((instance.flags & 1u) != 0u)
    {
        uv.x = 1.0f - uv.x;
    }
    if ((instance.flags & 2u) != 0u)
    {
        uv.y = 1.0f - uv.y;
    }

    // Apply pivot and size
    float2 size = instance.size_pivot.xy;
    float2 pivot = instance.size_pivot.zw;
    float2 local_position = (unit_position - pivot) * size;

    // Transform to world space, then clip space
    float4 world_position = mul(instance.local_to_world, float4(local_position, 0.0f, 1.0f));

    PSInput output;
    output.position = mul(_MatrixVP, world_position);
    output.uv = instance.uv_rect.xy + uv * instance.uv_rect.zw;
    output.color = instance.color;

    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 tex_color = SAMPLE_TEXTURE2D_LOD(_MainTex, g_LinearClampSampler, input.uv, 0);
    return tex_color * input.color;
}
