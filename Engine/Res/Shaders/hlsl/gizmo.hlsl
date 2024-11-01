//info bein
//pass begin::
//name: gizmo
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Topology: Line
//Queue: Opaque
//Blend: Src,OneMinusSrc
//ZTest: LEqual
//Fill: Wireframe
//pass end::
//info end

#include "cbuffer.hlsli"
#include "common.hlsli"
//这个shader中指定的pso状态不会应用，其在D3DContext line124设置。
struct VSInput
{
	float3 position : POSITION;
	float4 color : COLOR;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
};

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformFromWorldToClipSpace(v.position);
	result.color = v.color;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return saturate(input.color);
}