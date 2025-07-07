#pragma kernel MinMaxHeightGen
#pragma kernel QuadTreeProcessor
#pragma kernel GenPatches
#pragma kernel GenLODMap
#pragma multi_compile GenPatches _ _ENABLE_CULL
#include "cs_common.hlsli"
#include "../geometry.hlsli"
#include "../gpu_terrain_common.hlsli"


TEXTURE2D(HeightMap)
RWTEXTURE2D(MinMaxMap,float2)

CBUFFER_START(GenCB)
    int lod;
CBUFFER_END


[numthreads(8,8,1)]
void MinMaxHeightGen(CSInput input)
{
    uint2 index = input.DispatchThreadID.xy * 2;
    if (lod == 0)
    {
        MinMaxMap[input.DispatchThreadID.xy] = HeightMap[input.DispatchThreadID.xy].rr;
    }
    else if (lod == 1)
    {
        half tl = HeightMap[index].r;
        half tr = HeightMap[index + uint2(1,0)].r;
        half bl = HeightMap[index + uint2(0,1)].r;
        half br = HeightMap[index + uint2(1,1)].r;
        MinMaxMap[input.DispatchThreadID.xy] = float2(MinCompHalf(float4(tl,tr,bl,br)),MaxCompHalf(float4(tl,tr,bl,br)));
    }
    else
    {
        half2 tl = HeightMap[index].rg;
        half2 tr = HeightMap[index + uint2(1,0)].rg;
        half2 bl = HeightMap[index + uint2(0,1)].rg;
        half2 br = HeightMap[index + uint2(1,1)].rg;
        MinMaxMap[input.DispatchThreadID.xy] = float2(MinCompHalf(float4(tl.r,tr.r,bl.r,br.r)),
            MaxCompHalf(float4(tl.g,tr.g,bl.g,br.g)));
    }
}


CBUFFER_START(ComputeCB)
    uint   cur_lod;
    float3 camera_pos;
    float  node_width;
    float  control_factor;
    float max_height;
    Plane _frustum[6];
CBUFFER_END

ConsumeStructuredBuffer<uint2>  src_nodes;
AppendStructuredBuffer<uint2>   temp_nodes;
AppendStructuredBuffer<uint3>   final_nodes;
TEXTURE2D(_MinMaxMap)
//m
static const float kNodeWidth[6] = {64,128,256,512,1024,2048};
static const float kNodeCount[6] = {160,80,40,20,10,5};
static const uint  kSectorNum[6] = {1,2,4,8,16,32};// sector num per node
static const float2 kCenterOffset = float2(-5120,-5120);
static const uint   kSectorTexSize = 160;
static const float kFloatEpsilon = 1e-2;

bool ShouldSubdivide(float2 node_pos,float node_width,float3 camera_pos,float factor,uint lod)
{
    float3 node_center_pos = 0.0.xxx;
    node_center_pos.x = node_pos.x * node_width + node_width*0.5;
    node_center_pos.z = node_pos.y * node_width + node_width*0.5;
    node_center_pos.xz += kCenterOffset;
    half2 min_max_height = LOAD_TEXTURE2D_LOD(_MinMaxMap,node_pos,lod + 3).rg;
    node_center_pos.y = (min_max_height.r + min_max_height.g) * 0.5 * max_height;
    float dis = distance(node_center_pos,camera_pos);
    return dis / (node_width * factor) <= 1.0 + kFloatEpsilon;
}


[numthreads(1,1,1)]
void QuadTreeProcessor(CSInput input)
{
    uint2 node_pos = src_nodes.Consume();
    if(cur_lod > 0 && ShouldSubdivide((float2)node_pos,kNodeWidth[cur_lod],camera_pos,control_factor,cur_lod))
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
RWTEXTURE2D(_LODMap,uint)
RWStructuredBuffer<uint3>             _input_final_nodes;//与GenPatches共享
TEXTURE2D(Hiz)


[numthreads(1,1,1)]
void GenLODMap(CSInput input)
{
    uint3 node_pos = _input_final_nodes[input.GroupID.x];
    uint w = kSectorNum[node_pos.z];
    for (uint i = node_pos.x * w; i < (node_pos.x + 1) * w; i++)
    {
        for (uint j = node_pos.y * w; j < (node_pos.y + 1) * w; j++)
        {
            _LODMap[uint2(i,j)] = node_pos.z;
        }
    }
}

RenderPatch CreatePatch(uint3 node_pos,uint2 node_offset)
{
    RenderPatch patch = (RenderPatch)0;
    //left-bottom
    float2 node_start_pos = node_pos.xy * kNodeWidth[node_pos.z] + kCenterOffset;
    float patch_width = kNodeWidth[node_pos.z] / 8;
    patch._position.xz = patch_width * node_offset + node_start_pos + patch_width.xx * 0.5;
#if defined(_DEBUG_NODE)
    half2 min_max_height = LOAD_TEXTURE2D_LOD(_MinMaxMap,node_pos.xy,lod + 3).rg;
#else
    half2 min_max_height = LOAD_TEXTURE2D_LOD(_MinMaxMap,node_pos.xy * 8 + node_offset,node_pos.z).rg;
#endif
    patch_width *= 0.5;
    half bottom = min_max_height.x * max_height;
    half top = min_max_height.y * max_height;
    float3 aabb_min = float3(patch._position.x - patch_width,bottom,patch._position.y - patch_width);
    float3 aabb_max = float3(patch._position.x + patch_width,top,patch._position.y + patch_width);
    bool is_edge = node_offset.x == 0 || node_offset.y == 0 || node_offset.x == 7 || node_offset.y == 7;
    patch.lod = PackEdgeAndLOD(node_pos.z,is_edge);
    patch._position.y = (bottom + top) * 0.5;
    patch._aabb_ext = (aabb_max - aabb_min) * 0.5;
    patch._aabb_ext.y = max(0.5f,patch._aabb_ext.y);
    uint2 sector_idx = kSectorNum[node_pos.z] * node_pos.xy + node_offset / 8.0f * kSectorNum[node_pos.z];
    patch._sector = PackSectorIndex(sector_idx.x,sector_idx.y);
    return patch;
}
TEXTURE2D(_CameraHiZTexture)

int CalculateMipLevel(float2 uvMin, float2 uvMax, float2 hizTexSize)
{
    float2 uvSize = uvMax - uvMin;
    float maxSize = max(uvSize.x, uvSize.y);          // 归一化尺寸
    float pixelSize = maxSize * hizTexSize.x;         // 转为像素尺寸
    return floor(log2(pixelSize));
}


bool HizOcclusionCull(float3 center,float3 ext)
{
    float3 corners[8];
    // 通过组合 +ext 和 -ext 得到所有顶点
    corners[0] = center + float3(-ext.x, -ext.y, -ext.z); // 左-下-前（假设Z向前）
    corners[1] = center + float3( ext.x, -ext.y, -ext.z); // 右-下-前
    corners[2] = center + float3(-ext.x,  ext.y, -ext.z); // 左-上-前
    corners[3] = center + float3( ext.x,  ext.y, -ext.z); // 右-上-前
    corners[4] = center + float3(-ext.x, -ext.y,  ext.z); // 左-下-后
    corners[5] = center + float3( ext.x, -ext.y,  ext.z); // 右-下-后
    corners[6] = center + float3(-ext.x,  ext.y,  ext.z); // 左-上-后
    corners[7] = center + float3( ext.x,  ext.y,  ext.z); // 右-上-后
    float2 uvMin = float2(1, 1);
    float2 uvMax = float2(0, 0);
    float max_z = 0.0;
    float min_z = 1.0;
    [unroll(8)]
    for(int i = 0; i < 8; i++)
    {
        float4 clip_pos = TransformWorldToHClipNoJitter(corners[i]);
        float3 ndc = clip_pos.xyz / clip_pos.w;
        clip_pos.xyz /= clip_pos.w;
        float2 uv = clip_pos.xy * 0.5 + 0.5;
        uv.y = 1.0 - uv.y;
        uvMin = min(uvMin, uv);
        uvMax = max(uvMax, uv);
        min_z = min(min_z, ndc.z);
        max_z = max(max_z,ndc.z);
    }
    if (uvMax.x < 0 || uvMin.x > 1 || uvMax.y < 0 || uvMin.y > 1)
        return true;
    int mip = CalculateMipLevel(uvMin,uvMax,_ScreenParams.xy * 2);
    float d1 = SAMPLE_TEXTURE2D_LOD(_CameraHiZTexture,g_PointClampSampler,uvMin,mip);
    float d2 = SAMPLE_TEXTURE2D_LOD(_CameraHiZTexture,g_PointClampSampler,uvMax,mip);
    float d3 = SAMPLE_TEXTURE2D_LOD(_CameraHiZTexture,g_PointClampSampler,float2(uvMax.x,uvMin.y),mip);
    float d4 = SAMPLE_TEXTURE2D_LOD(_CameraHiZTexture,g_PointClampSampler,float2(uvMin.x,uvMax.y),mip);
    //采样的四个点都比包围盒近点更近的话，剔除
    #if defined(_REVERSED_Z)
        return (max_z < d1 && max_z < d2 && max_z < d3 && max_z < d4);
    #else
        return (min_z > d1 && min_z > d2 && min_z > d3 && min_z > d4);
    #endif
}

TEXTURE2D(_InputLODMap)
void FillEdgeInfo(inout RenderPatch patch)
{
    uint2 sector_idx = UnpackSectorIndex(patch._sector);
    uint lod;
    bool is_edge;
    UnPackEdgeAndLOD(patch.lod,lod,is_edge);
    uint4 ltrb = 0.0.xxxx;
    if (sector_idx.x > 0)
    {
        uint left = asuint(_InputLODMap[uint2(sector_idx.x - 1,sector_idx.y)]);
        ltrb.x = left > lod? left - lod : 0;
    }
    if (sector_idx.x < kSectorTexSize - 1)
    {
        uint right = asuint(_InputLODMap[uint2(sector_idx.x + 1,sector_idx.y)]);
        ltrb.z = right > lod? right - lod : 0;
    }
    if (sector_idx.y > 0)
    {
        uint top = asuint(_InputLODMap[uint2(sector_idx.x,sector_idx.y - 1)]);
        ltrb.y = top > lod? top - lod : 0;
    }
    if (sector_idx.y < kSectorTexSize - 1)
    {
        uint bottom = asuint(_InputLODMap[uint2(sector_idx.x,sector_idx.y + 1)]);
        ltrb.w = bottom > lod? bottom - lod : 0;
    }
    patch._sector = PackSectorLOD(ltrb);
}

AppendStructuredBuffer<RenderPatch>   _patch_list;
[numthreads(8,8,1)]
void GenPatches(CSInput input)
{
    uint3 node_pos = _input_final_nodes[input.GroupID.x];
    uint2 patch_offset = input.GroupThreadID.xy;
    RenderPatch p = CreatePatch(node_pos,patch_offset);
#if defined(_ENABLE_CULL)
    if (!FrustumCullAABB(p._position,p._aabb_ext * 2,_frustum) && !HizOcclusionCull(p._position,p._aabb_ext))
#endif
    {
        FillEdgeInfo(p);
        _patch_list.Append(p);
    }
}