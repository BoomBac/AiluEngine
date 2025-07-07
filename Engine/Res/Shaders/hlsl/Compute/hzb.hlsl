#pragma kernel CSMain
#include "cs_common.hlsli"

TEXTURE2D(_DepthInput)

RWTEXTURE2D(_HZ_Buffer_Mip1,float)
RWTEXTURE2D(_HZ_Buffer_Mip2,float)
RWTEXTURE2D(_HZ_Buffer_Mip3,float)
RWTEXTURE2D(_HZ_Buffer_Mip4,float)

CBUFFER_START(CB_HZB)
    uint NumMipLevels;	// Number of OutMips to write: [1-4]
CBUFFER_END

static const uint2 kOffset[4] = {
    uint2(0,0),
    uint2(1,0),
    uint2(0,0),
    uint2(1,1),
};
float CalculateHZ(float4 value)
{
#if defined(_REVERSED_Z)
    return MaxCompFloat(value);
#else
    return MinCompFloat(value);
#endif
}

groupshared float hiz_buffer[64];
[numthreads(8,8,1)]
void CSMain(CSInput input)
{
    uint2 index = input.DispatchThreadID.xy * 2;
    float4 quad_depth;
    for(int i = 0; i < 4; i++)
    {
        uint2 pos = index + kOffset[i];
        if (pos.x >= 0 && pos.y >= 0 && pos.x < _ScreenParams.z && pos.y < _ScreenParams.w)
            quad_depth[i] = LOAD_TEXTURE2D_LOD(_DepthInput,pos,0);
        else
            quad_depth[i] = kZFar;
    }
    float hz = CalculateHZ(quad_depth);
    _HZ_Buffer_Mip1[input.DispatchThreadID.xy] = hz;
    if (NumMipLevels == 1)
        return;
    hiz_buffer[input.GroupIndex] = hz;
    GroupMemoryBarrierWithGroupSync();
    // With low three bits for X and high three bits for Y, this bit mask
    // (binary: 001001) checks that X and Y are even. 偶数索引线程执行
    if ((input.GroupIndex & 0x9 ) == 0)
    {
        quad_depth[0] = hz;
        quad_depth[1] = hiz_buffer[input.GroupIndex + 0x01];
        quad_depth[2] = hiz_buffer[input.GroupIndex + 0x08];
        quad_depth[3] = hiz_buffer[input.GroupIndex + 0x09];
        hz = CalculateHZ(quad_depth);
        _HZ_Buffer_Mip2[input.DispatchThreadID.xy / 2] = hz;
    }
    if (NumMipLevels == 2)
        return;
    hiz_buffer[input.GroupIndex] = hz;
    GroupMemoryBarrierWithGroupSync();
    // This bit mask (binary: 011011) checks that X and Y are multiples of four.
    if ((input.GroupIndex & 0x1B ) == 0)
    {
        quad_depth[0] = hz;
        quad_depth[1] = hiz_buffer[input.GroupIndex + 0x02];
        quad_depth[2] = hiz_buffer[input.GroupIndex + 0x10];
        quad_depth[3] = hiz_buffer[input.GroupIndex + 0x12];
        hz = CalculateHZ(quad_depth);
        _HZ_Buffer_Mip3[input.DispatchThreadID.xy / 4] = hz;
    }
    if ( NumMipLevels == 3 )
        return;
    hiz_buffer[input.GroupIndex] = hz;
    GroupMemoryBarrierWithGroupSync();
    if (input.GroupIndex == 0)
    {
        quad_depth[0] = hz;
        quad_depth[1] = hiz_buffer[input.GroupIndex + 0x04];
        quad_depth[2] = hiz_buffer[input.GroupIndex + 0x20];
        quad_depth[3] = hiz_buffer[input.GroupIndex + 0x24];
        hz = CalculateHZ(quad_depth);
        _HZ_Buffer_Mip4[input.DispatchThreadID.xy / 8] = hz;
    }
}