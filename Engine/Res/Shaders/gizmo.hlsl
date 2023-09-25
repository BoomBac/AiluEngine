#include "cbuffer.hlsl"

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
	result.position = mul(float4(v.position, 1.0f), _MatrixVP);
	result.color = v.color;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return input.color;
	//return float4(1.0,1.0,1.0,1.0);
}
