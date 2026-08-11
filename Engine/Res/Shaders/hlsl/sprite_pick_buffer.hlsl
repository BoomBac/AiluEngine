//info bein
//pass begin::
//name: sprite_pick_buffer
//vert: VSMain
//pixel: PSMain
//Cull: Off
//ZTest: LEqual
//ZWrite: On
//Queue: Opaque
//pass end::
//info end

#include "common.hlsli"

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
TEXTURE2D(_MainTex)

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
    uint entity_id : TEXCOORD1;
};

PSInput VSMain(VSInput input)
{
    sprite_instance_data instance = g_sprite_instances[input.instance_id];
    float2 uv = input.uv;
    if ((instance.flags & 1u) != 0u) uv.x = 1.0f - uv.x;
    if ((instance.flags & 2u) != 0u) uv.y = 1.0f - uv.y;

    float2 local_position = (input.position - instance.size_pivot.zw) * instance.size_pivot.xy;
    float4 world_position = mul(instance.local_to_world, float4(local_position, 0.0f, 1.0f));

    PSInput output;
    output.position = mul(_MatrixVP, world_position);
    output.uv = instance.uv_rect.xy + uv * instance.uv_rect.zw;
    output.color = instance.color;
    output.entity_id = instance.entity_id;
    return output;
}

uint PSMain(PSInput input) : SV_TARGET
{
    float alpha = SAMPLE_TEXTURE2D(_MainTex, g_LinearClampSampler, input.uv).a * input.color.a;
    clip(alpha - 0.01f);
    return (input.entity_id & 0xFFFFFFu) << 8u;
}
