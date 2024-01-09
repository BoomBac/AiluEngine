//info bein
//name: skybox
//vert: VSMain
//pixel: PSMain
//Cull: Front
//Queue: Opaque
//Properties
//{
//	color("Color",Color) = (1,1,1,1)
//	expo("Exposure",Range(0,4)) = 1
//  TestTex("TestTex",Texture2D) = "white"
//  [Toggle] _InvertColor("InvertColor",Float) = 0
//  [Enum(Red,0,Blue,1,Green,2)] _ColorOverlay("ColorOverlay",Float) = 0
//}
//#pragma multi_complie _InvertColor_ON _InvertColor_OFF
//#pragma multi_complie _ColorOverlay_Red _ColorOverlay_Blue _ColorOverlay_Green
//info end

#include "common.hlsli"

TextureCube SkyBox : register(t0);
Texture2D TestTex : register(t1);

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

CBufBegin
	float expo;
	float4 color;
CBufEnd

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
	return float4(SkyBox.Sample(g_LinearSampler,input.wnormal).rgb * expo * color.rgb + TestTex.Sample(g_LinearSampler,input.wnormal.xy).rgb,1.0);
	//return float4(SkyBox.Sample(g_LinearSampler,input.wnormal).rgb * 2.0f,1.0) ; 
}
