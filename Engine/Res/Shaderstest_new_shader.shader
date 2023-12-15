//info bein
//name: test_shader
//vert: VSMain
//pixel: PSMain
//Cull: Front
//Queue: Opaque
//Properties
//{
//	color("Color",Color) = (1,1,1,1)
//}
//info end

#include "common.hlsl"

struct VSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float3 wnormal : NORMAL;
};

CBufBegin
	float4 color;
CBufEnd

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformToClipSpace(v.position);
	result.wnormal = normalize(v.position);
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(color) ; 
}