#include "cs_common.hlsli"
#pragma kernel cs_main

Texture2D<uint> _PickBuffer;
RWBuffer<uint> _PickResult;

cbuffer CBPickReader : register(b0)
{
    float2 pixel_pos;
}

[numthreads(1,1,1)]
void cs_main(CSInput input)
{
    _PickResult[0] = _PickBuffer.Load(int3(pixel_pos,0));
}
