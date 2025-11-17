//info bein
//pass begin::
//name: RayDebugDraw
//vert: VSMain
//pixel: PSMain
//Topology: Line
//Cull: Off
//Queue: Opaque
//pass end::
//info end

#include "common.hlsli"

// 最大递归深度
static const uint kMaxDepth = 9;

// 固定的每层颜色
static const float3 kDepthColors[kMaxDepth] =
{
    float3(1.0, 1.0, 1.0),   // depth = 0  (红)
    float3(0.0, 1.0, 0.0),   // depth = 1  (绿)
    float3(0.0, 0.0, 1.0),   // depth = 2  (蓝)
    float3(1.0, 1.0, 0.0),   // depth = 3  (黄)
    float3(1.0, 0.0, 1.0),   // depth = 4  (品红)
    float3(0.0, 1.0, 1.0),   // depth = 5  (青)
    float3(1.0, 0.5, 0.0),   // depth = 6  (橙)
    float3(0.5, 0.0, 1.0),    // depth = 7  (紫)
    float3(1.0, 0.0, 0.0)    // miss
};

struct VSInput
{
	uint vert_id : SV_VertexID;
};
struct DebugRay
{
    float3 pos;
    uint data;
};

StructuredBuffer<DebugRay> _ray_list;
//StructuredBuffer<uint> _ray_indices;

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color : TEXCOORD0;
};

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformWorldToHClipNoJitter(_ray_list[v.vert_id].pos);
    result.color = float4(kDepthColors[_ray_list[v.vert_id].data], 1);
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return input.color;
}