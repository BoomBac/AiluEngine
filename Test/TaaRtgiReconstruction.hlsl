#include "../Engine/Res/Shaders/hlsl/Compute/cs_common.hlsli"

#pragma kernel Reconstruct

TEXTURE2D(_AcceptanceDepth)
TEXTURE2D(_AcceptanceNormal)
RWTEXTURE2D(_AcceptanceWorld, float4)

[shader("compute")]
[numthreads(16, 16, 1)]
void Reconstruct(CSInput input)
{
    uint2 pixel = input.DispatchThreadID.xy;
    if (any(pixel >= uint2(_ScreenParams.zw))) return;
    float2 uv = (float2(pixel) + 0.5) * _ScreenParams.xy;
    float3 world = ComputeWorldSpacePosition(uv, _AcceptanceDepth[pixel].r, _MatrixIVP);
    float3 normal = normalize(UnpackNormal(_AcceptanceNormal[pixel].xy));
    _AcceptanceWorld[pixel] = float4(world, dot(normal, float3(0, 0, -1)));
}
