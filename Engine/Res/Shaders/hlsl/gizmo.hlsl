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
//pass begin::
//name: gizmo_texture
//vert: VSMainTex
//pixel: PSMainTex
//Cull: Back
//Queue: Opaque
//Blend: Src,OneMinusSrc
//ZTest: Always
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
	result.position = TransformWorldToHClip(v.position);
	result.color = v.color;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return saturate(input.color);
}

struct PSInputTex
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};
static const float2 uv_table[6] = {
    float2(0, 0), // vert_id 0
    float2(1, 0), // vert_id 1
    float2(0, 1), // vert_id 2
    float2(1, 0), // vert_id 3
    float2(1, 1), // vert_id 4
    float2(0, 1)  // vert_id 5
};


TEXTURE2D(_MainTex)
PSInputTex VSMainTex(float2 position : POSITION,uint vert_id : SV_VertexID)
{
	PSInputTex result;
	result.position = float4(position,1.0f,1.0f);
	//float x = float(((uint(vert_id) + 2u) / 3u)%2u);
    //float y = float(((uint(vert_id) + 1u) / 3u)%2u);
	result.uv = uv_table[vert_id];
	return result;
}
float4 PSMainTex(PSInputTex input) : SV_TARGET
{
	float4 c = SAMPLE_TEXTURE2D(_MainTex,g_LinearClampSampler,input.uv);
	return c;
}