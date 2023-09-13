#include "cbuffer.hlsl"

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
};

PSInput VSMain(
	float4 position : POSITION, float4 color : COLOR)
{
	PSInput result;
	float4x4 mvp = mul(_MatrixWorld,_MatrixVP);
	result.position = mul(position, mvp);
	result.color = color;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return _Color;
}
