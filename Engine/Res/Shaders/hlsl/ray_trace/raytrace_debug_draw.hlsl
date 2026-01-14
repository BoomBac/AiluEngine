//info bein
//pass begin::
//name: RayDebugDraw
//vert: VSMain
//pixel: PSMain
//Topology: Line
//Cull: Off
//Queue: Transparent
//ZTest: Always
//Blend: Src,OneMinusSrc
//pass end::
//info end

#include "rt_common.hlsli"
#include "../common.hlsli"
struct VSInput
{
	uint vert_id : SV_VertexID;
};

StructuredBuffer<DebugRay> _ray_list;

PerMaterialCBufferBegin
    uint _debug_hit_box_idx;
PerMaterialCBufferEnd

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color : TEXCOORD0;
};

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformWorldToHClipNoJitter(_ray_list[v.vert_id].pos);
    float4 c = UnpackFloat4(_ray_list[v.vert_id].color);
    result.color = c;
	int idx = _ray_list[v.vert_id].aabb_idx;
	if (idx >= 0)
	{
		if (idx == _debug_hit_box_idx)
		{
			result.color = float4(1,0,0,1);
		}
		else
		{
			result.color = float4(0,1,0,0.0);
		}
	}
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return input.color;
}