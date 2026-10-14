#pragma kernel Main
#include "../Engine/Res/Shaders/hlsl/material_sampling.hlsli"
Texture2D _Input : register(t0);
RWStructuredBuffer<float4> _Output : register(u0);
cbuffer ComputeCB : register(b0)
{
    uint _Repeat;
    uint _Clamp;
    uint _Point;
    uint _Linear;
    uint _LodSampler;
};
[numthreads(1,1,1)]
void Main(uint3 id : SV_DispatchThreadID)
{
    _Output[0] = _Input.SampleLevel(g_material_samplers[_Repeat], float2(1.125, 0.125), 0);
    _Output[1] = _Input.SampleLevel(g_material_samplers[_Clamp], float2(1.125, 0.125), 0);
    _Output[2] = _Input.SampleLevel(g_material_samplers[_Point], float2(0.5, 0.125), 0);
    _Output[3] = _Input.SampleLevel(g_material_samplers[_Linear], float2(0.5, 0.125), 0);
    _Output[4] = SampleMaterialTextureLevel(_Input, _LodSampler, float2(0.125, 0.125), 0, 0);
    _Output[5] = SampleMaterialTextureLevel(_Input, _LodSampler, float2(0.125, 0.125), 1, 0);
}
