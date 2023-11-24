//info bein
//name: skybox
//vert: VSMain
//pixel: PSMain
//Cull: Front
//Queue: Opaque
//info end

#include "common.hlsl"

TextureCube SkyBox : register(t0);

SamplerState g_LinearSampler : register(s0);

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

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformToClipSpace(v.position);
	//result.wnormal = normalize(mul(float4(v.normal,0.0f),_MatrixWorld).xyz);
	result.wnormal = normalize(v.position);
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(SkyBox.Sample(g_LinearSampler,input.wnormal).rgb,1.0);
}
