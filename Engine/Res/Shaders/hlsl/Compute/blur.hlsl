#include "cs_common.hlsli"
#pragma kernel blur_x
#pragma kernel blur_y

Texture2D _SourceTex;
RWTexture2D<float3> _OutTex;

#define RADIUS 8

[numthreads(16,16,1)]
void blur_x(CSInput input)
{
    float3 c = 0.0.xxx;
    for(int i = -RADIUS;i <= RADIUS;i++)
    {
        uint2 uv = input.DispatchThreadID.xy + int2(i,0);
        c += _SourceTex.Load(uint3(uv,0)).rgb;
    }
    c /= (RADIUS * 2 + 1);
    _OutTex[input.DispatchThreadID.xy] = c;
}

[numthreads(16,16,1)]
void blur_y(CSInput input)
{
    float3 c = 0.0.xxx;
    for(int i = -RADIUS;i <= RADIUS;i++)
    {
        uint2 uv = input.DispatchThreadID.xy + int2(0,i);
        c += _SourceTex.Load(uint3(uv,0)).rgb;
    }
    c /= (RADIUS * 2 + 1);
    _OutTex[input.DispatchThreadID.xy] = c;
}
