#ifndef __FULLSCREEN_QUAD__
#define __FULLSCREEN_QUAD__

struct VSInput
{
	float3 position : POSITION;
	float2 uv : TEXCOORD;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

const static float4 fullscreen_triangle[3] = {
	float4(-1,-1,0,1),
	float4(-1,3,0,1),
	float4(3,-1,0,1)
};
const static float2 uvs[3] = {
	float2(0,1),
	float2(0,-1),
	float2(2,1)
};

PSInput FullscreenVSMain(uint vertex_id : SV_VERTEXID)
{
	PSInput result;
	result.position = fullscreen_triangle[vertex_id];
	result.uv = uvs[vertex_id];
	return result;
}

#endif//__FULLSCREEN_QUAD__