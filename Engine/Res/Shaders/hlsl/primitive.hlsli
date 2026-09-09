#ifndef __PRIMITIVE_HLSLI__
#define __PRIMITIVE_HLSLI__

#include "cbuffer.hlsli"

// Fixed register contract, space0:
//   b0: per-draw root constants (or the legacy per-object CBV for non-scene shaders)
//   b1: material, b2: scene, b3: camera
//   t0-t15: existing material, lighting, shadow and LTC resources; keep stable for compatibility.
//   t16-t23: GPU scene resources. Reserve this range so per-object scene data never collides with a material texture.
StructuredBuffer<PrimitiveData> g_primitive_data : register(t16);
StructuredBuffer<uint> g_instance_primitive_indices : register(t17);

uint GetPrimitiveIndex(uint instance_id)
{
    if ((_flags & kPrimitiveDrawUseInstanceIndex) != 0u)
        return g_instance_primitive_indices[_instance_index_offset + instance_id];
    return _primitive_base + instance_id;
}

PrimitiveData LoadPrimitive(uint instance_id)
{
    return g_primitive_data[GetPrimitiveIndex(instance_id)];
}

float4 TransformPrimitiveToClipSpace(PrimitiveData primitive, float3 object_pos)
{
    return mul(mul(_MatrixVP, primitive._local_to_world), float4(object_pos, 1.0f));
}

float4 TransformPrimitiveToClipSpaceNoJitter(PrimitiveData primitive, float3 object_pos)
{
    return mul(mul(_MatrixVP_NoJitter, primitive._local_to_world), float4(object_pos, 1.0f));
}

float3 TransformPrimitiveToWorld(PrimitiveData primitive, float3 object_pos)
{
    return mul(primitive._local_to_world, float4(object_pos, 1.0f)).xyz;
}

float3 TransformPrimitivePreviousToWorld(PrimitiveData primitive, float3 object_pos)
{
    return mul(primitive._prev_local_to_world, float4(object_pos, 1.0f)).xyz;
}

float3 TransformPrimitiveNormal(PrimitiveData primitive, float3 object_normal)
{
    return normalize(mul(transpose((float3x3)primitive._world_to_local), object_normal));
}

#endif
