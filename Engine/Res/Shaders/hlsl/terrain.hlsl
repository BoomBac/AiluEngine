//info bein
//pass begin::
//name: gpu_terrain
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Fill: Wireframe
//ZWrite: On
//pass end::
//info end

#include "common.hlsli"
#include "lighting.hlsli"
#include "gpu_terrain_common.hlsli"

StructuredBuffer<RenderPatch> PatchList;
struct VSIn
{
    float4 vertex : POSITION;
    float2 uv     : TEXCOORD0;
    uint instanceID : SV_InstanceID;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
};

const static float4 kDebugLODColors[6] = {
    float4(1.0f, 0.0f, 0.0f, 1.0f),  // 鲜红色 (Red)
    float4(0.0f, 0.8f, 0.0f, 1.0f),  // 亮绿色 (Green)
    float4(0.0f, 0.5f, 1.0f, 1.0f),  // 蓝色 (Blue)
    float4(1.0f, 0.8f, 0.0f, 1.0f),  // 金黄色 (Yellow)
    float4(1.0f, 0.0f, 1.0f, 1.0f),  // 品红色 (Magenta)
    float4(0.0f, 1.0f, 1.0f, 1.0f)   // 青色 (Cyan)
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    RenderPatch patch = PatchList[v.instanceID];
    uint lod = patch.lod;
    float scale = pow(2,lod);
    v.vertex.xz *= scale;
    v.vertex.xz += patch._position;
    o.position = TransformWorldToHClipNoJitter(v.vertex.xyz);
    o.color = kDebugLODColors[lod];
    return o;
}

float4 PSMain(VSOut i) : SV_TARGET
{
    return i.color;
}