#include "cs_common.hlsli"
#include "../voxel_gi.hlsli"
#include "../pixel_packing.hlsli"

#pragma kernel FillTexture3D

RWStructuredBuffer<uint> _VoxelBuffer;
RWTexture3D<float4> _VoxelTex;

cbuffer VoxelCB : register(b0)
{
    uint4 _grid_num;
};

[numthreads(8,8,8)]
void FillTexture3D(CSInput input)
{
    float a;
    uint index = ToLinearIndex(input.DispatchThreadID,_grid_num.xyz);
    float3 color = DecodeRGBE(_VoxelBuffer[index],a);
    //_VoxelTex[input.DispatchThreadID] = lerp(float4(color,a),_VoxelTex[input.DispatchThreadID],0.5f);
	_VoxelTex[input.DispatchThreadID] = float4(color, a);
    _VoxelBuffer[index] = 0;
}
