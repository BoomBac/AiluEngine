#pragma kernel QuadTreeProcessor
#pragma kernel GenPatches
#include "cs_common.hlsli"
#include "../gpu_terrain_common.hlsli"

CBUFFER_START(ComputeCB)
    uint   cur_lod;
    float3 camera_pos;
    float  node_width;
    float  control_factor;
CBUFFER_END

ConsumeStructuredBuffer<uint2>  src_nodes;
AppendStructuredBuffer<uint2>   temp_nodes;
AppendStructuredBuffer<uint3>   final_nodes;

bool ShouldSubdivide(uint2 node_pos,float node_width,float3 camera_pos,float factor)
{
    float3 node_center_pos = 0.0.xxx;
    node_center_pos.x = node_pos.x * node_width + node_width*0.5;
    node_center_pos.y = node_pos.y * node_width + node_width*0.5;
    return distance(node_center_pos,camera_pos) / (node_width * factor) < 1.0;
}
//m
static const float kNodeWidth[6] = {64,128,256,512,1024,2048};
//static const float2 kCenterOffset = float2(-5120,-5120);
static const float2 kCenterOffset = float2(-0,-0);

[numthreads(1,1,1)]
void QuadTreeProcessor(CSInput input)
{
    uint2 node_pos = src_nodes.Consume();
    if(cur_lod > 0 && ShouldSubdivide(node_pos,kNodeWidth[cur_lod],camera_pos,control_factor))
    {
        //divide
        temp_nodes.Append(node_pos * 2);
        temp_nodes.Append(node_pos * 2 + uint2(1,0));
        temp_nodes.Append(node_pos * 2 + uint2(0,1));
        temp_nodes.Append(node_pos * 2 + uint2(1,1));
    }
    else
    {
        final_nodes.Append(uint3(node_pos,cur_lod));
    }
}

RenderPatch CreatePatch(uint3 node_pos,uint2 node_offset)
{
    RenderPatch patch;
    //left-bottom
    float2 node_start_pos = node_pos.xy * kNodeWidth[node_pos.z];
    float patch_width = kNodeWidth[node_pos.z] / 8;
    patch._position = patch_width * node_offset + node_start_pos - kCenterOffset;
    patch.lod = node_pos.z;
    return patch;
}

RWStructuredBuffer<uint3>             _input_final_nodes;
AppendStructuredBuffer<RenderPatch>   _patch_list;
[numthreads(8,8,1)]
void GenPatches(CSInput input)
{
    uint3 node_pos = _input_final_nodes[input.GroupID.x];
    uint2 patch_offset = input.GroupThreadID.xy;
    _patch_list.Append(CreatePatch(node_pos,patch_offset));
}