//info bein
//pass begin::
//name: terrain
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Fill: Solid
//ZWrite: On
//multi_compile _ DEBUG_LOD
//pass end::
//pass begin::
//name: debug_patch
//vert: VSMainPatchDebug
//pixel: PSMainPatchDebug
//Cull: Back
//Fill: Wireframe
//ZWrite: On
//pass end::
//info end

#include "common.hlsli"
#include "lighting.hlsli"
#include "gpu_terrain_common.hlsli"

//两张纹理在不同阶段使用，自动生成的slot共用一个，所以手动指定
Texture2D _BaseColor   : register(t0);
Texture2D _HeightMap   : register(t1);



PerMaterialCBufferBegin
    float4 _params;
PerMaterialCBufferEnd

#define MAX_HEIGHT _params.x
#define WORLD_SIZE _params.zw

#define MESH_WIDTH 8.0f
#define MESH_SECTION 16

StructuredBuffer<RenderPatch> PatchList;
struct VSIn
{
    float3 vertex : POSITION;
    float2 uv     : TEXCOORD0;
    uint instanceID : SV_InstanceID;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation float4 color : TEXCOORD1;
};

const static float4 kDebugLODColors[6] = {
    {1.0f, 0.0f, 0.0f, 1.0f}, // 明亮红色
    {0.0f, 1.0f, 0.0f, 1.0f}, // 明亮绿色
    {0.0f, 0.6f, 1.0f, 1.0f}, // 天蓝色
    {1.0f, 1.0f, 0.0f, 1.0f}, // 黄色
    {1.0f, 0.0f, 1.0f, 1.0f}, // 品红
    {1.0f, 0.5f, 0.0f, 1.0f}  // 橙色
};
//meter
const static float kCellSize[6] = {0.5,1,2,4,8,16};

uint2 CalculateVertexIndex(float2 pos, uint section, float width)
{
    float cell_size = width / section;
    float2 grid_index = pos / cell_size + float2(MESH_SECTION >> 1,MESH_SECTION >> 1);
    grid_index = clamp(grid_index, 0.0, (float)section);
    uint2 uidx = (uint2)(grid_index + 0.5);
    uidx.y = section - uidx.y;
    return uidx;
}

void FixLODSeam(uint packed, uint2 vert_index, uint lod, inout float3 world_pos)
{
    // ltrb: left, top, right, bottom
    uint4 lbrt = UnpackSectorLOD(packed);
    float2 world_uv;
    // LEFT
    if (vert_index.x == 0 && vert_index.y % 2 != 0 && lbrt.x == 1)
    {
        float2 pos = world_pos.xz;
        pos.y += kCellSize[lod];
        world_uv = float2(pos / WORLD_SIZE) + 0.5;
        float top_height = SAMPLE_TEXTURE2D_LOD(_HeightMap, g_LinearWrapSampler, world_uv, 0).r * MAX_HEIGHT;
        pos.y -= 2.0 * kCellSize[lod];
        world_uv = float2(pos / WORLD_SIZE) + 0.5;
        float bottom_height = SAMPLE_TEXTURE2D_LOD(_HeightMap, g_LinearWrapSampler, world_uv, 0).r * MAX_HEIGHT;
        world_pos.y = 0.5 * (top_height + bottom_height);
    }
    // TOP
    if (vert_index.y == 0 && vert_index.x % 2 != 0 && lbrt.w == 1)
    {
        float2 pos = world_pos.xz;
        pos.x += kCellSize[lod];
        world_uv = float2(pos / WORLD_SIZE) + 0.5;
        float right_height = SAMPLE_TEXTURE2D_LOD(_HeightMap, g_LinearWrapSampler, world_uv, 0).r * MAX_HEIGHT;
        pos.x -= 2.0 * kCellSize[lod];
        world_uv = float2(pos / WORLD_SIZE) + 0.5;
        float left_height = SAMPLE_TEXTURE2D_LOD(_HeightMap, g_LinearWrapSampler, world_uv, 0).r * MAX_HEIGHT;
        world_pos.y = 0.5 * (right_height + left_height);
    }
    // RIGHT
    if (vert_index.x == MESH_SECTION && vert_index.y % 2 != 0 && lbrt.z == 1)
    {
        float2 pos = world_pos.xz;
        pos.y += kCellSize[lod];
        world_uv = float2(pos / WORLD_SIZE) + 0.5;
        float top_height = SAMPLE_TEXTURE2D_LOD(_HeightMap, g_LinearWrapSampler, world_uv, 0).r * MAX_HEIGHT;
        pos.y -= 2.0 * kCellSize[lod];
        world_uv = float2(pos / WORLD_SIZE) + 0.5;
        float bottom_height = SAMPLE_TEXTURE2D_LOD(_HeightMap, g_LinearWrapSampler, world_uv, 0).r * MAX_HEIGHT;
        world_pos.y = 0.5 * (top_height + bottom_height);
    }
    // BOTTOM
    if (vert_index.y == MESH_SECTION && vert_index.x % 2 != 0 && lbrt.y == 1)
    {
        float2 pos = world_pos.xz;
        pos.x += kCellSize[lod];
        world_uv = float2(pos / WORLD_SIZE) + 0.5;
        float right_height = SAMPLE_TEXTURE2D_LOD(_HeightMap, g_LinearWrapSampler, world_uv, 0).r * MAX_HEIGHT;
        pos.x -= 2.0 * kCellSize[lod];
        world_uv = float2(pos / WORLD_SIZE) + 0.5;
        float left_height = SAMPLE_TEXTURE2D_LOD(_HeightMap, g_LinearWrapSampler, world_uv, 0).r * MAX_HEIGHT;
        world_pos.y = 0.5 * (right_height + left_height);
    }
}


VSOut VSMain(VSIn v)
{
    VSOut o;
    RenderPatch patch = PatchList[v.instanceID];
    uint lod;
    bool is_edge;
    UnPackEdgeAndLOD(patch.lod,lod,is_edge);
    float3 world_pos = v.vertex.xyz;
    world_pos.xz *= pow(2,lod);
    world_pos.xz += patch._position.xz;
    float2 world_uv = float2(world_pos.xz / WORLD_SIZE) + 0.5;
    world_pos.y = SAMPLE_TEXTURE2D_LOD(_HeightMap,g_LinearWrapSampler,world_uv,0).r * MAX_HEIGHT;
    //seam fix,flip y, y<->w
    uint4 lbrt = UnpackSectorLOD(patch._sector);
    uint2 vert_index = CalculateVertexIndex(v.vertex.xz,MESH_SECTION,MESH_WIDTH);
    FixLODSeam(patch._sector,vert_index,lod,world_pos);
    o.position = TransformWorldToHClipNoJitter(world_pos);
    o.uv = world_uv;
    o.color = kDebugLODColors[lod];
    return o;
}

float4 PSMain(VSOut i) : SV_TARGET
{
#if defined(DEBUG_LOD)
    float4 color = SAMPLE_TEXTURE2D_LOD(_BaseColor,g_LinearWrapSampler,i.uv,0);
    color = pow(color,2.2f) * i.color;
#else
    float4 color = SAMPLE_TEXTURE2D_LOD(_BaseColor,g_LinearWrapSampler,i.uv,0);
    color = pow(color,2.2f);
#endif
    return color; 
}

VSOut VSMainPatchDebug(VSIn v)
{
    VSOut o; 
    RenderPatch patch = PatchList[v.instanceID];
    v.vertex.xyz *= patch._aabb_ext;
    v.vertex.xz *= 0.95;
    v.vertex += patch._position;
    //v.vertex.y += SAMPLE_TEXTURE2D_LOD(_HeightMap,g_LinearWrapSampler,UnpackHalf2(patch._padding0),0).g * MAX_HEIGHT;
    o.position = TransformWorldToHClipNoJitter(v.vertex.xyz);
    o.uv = 0.0.xx;
    o.color = kDebugLODColors[patch.lod]; 
    return o; 
}

float4 PSMainPatchDebug(VSOut i) : SV_TARGET
{
    return i.color;
}