//info bein
//pass begin::
//name: skybox
//vert: VSMain
//pixel: PSMain
//Cull: Front
//Queue: Opaque
//pass end::
//Properties
//{
//	color("Color",Color) = (1,1,1,1)
//	expo("Exposure",Range(0,4)) = 1
//  env("Env",Texture2D) = white
//  [Toggle] _InvertColor("InvertColor",Float) = 0
//  [Enum(Red,0,Blue,1,Green,2)] _ColorOverlay("ColorOverlay",Float) = 0
//}
//#pragma multi_complie _InvertColor_ON _InvertColor_OFF
//#pragma multi_complie _ColorOverlay_Red _ColorOverlay_Blue _ColorOverlay_Green
//info end

#include "common.hlsli"
#include "constants.hlsli"

TextureCube SkyBox : register(t0);
Texture2D env : register(t1);


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

PerMaterialCBufferBegin
	float expo;
	float4 color;
PerMaterialCBufferEnd

static const float2 inv_atan = {0.1591f,0.3183f};
float2 SampleSphericalMap(float3 v)
{
	float2 uv = float2(atan2(v.x, v.z), asin(v.y));
	uv *= inv_atan;
	uv += 0.5;
	return uv;
}


PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformToClipSpace(v.position);
	result.position.z = result.position.w;
	//result.wnormal = normalize(mul(_MatrixWorld, float4(v.normal, 0.0f)).xyz);
	result.wnormal = v.position;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	//float2 uv = SampleSphericalMap(normalize(-input.wnormal)); 
	//float3 sky_color = env.Sample(g_LinearSampler,uv).rgb * expo * color.rgb;
	//return float4(sky_color,1.0);
	//return 1.0.xxxx;
	return float4(SkyBox.Sample(g_LinearClampSampler, normalize(input.wnormal)).rgb, 4.0);
}
