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
//}
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

#define GROUND_COLOR float3(1,1,1)
#define SKY_COLOR float3(0.05,0.14,0.42)

float4 PSMain(PSInput input) : SV_TARGET
{
	input.wnormal = normalize(input.wnormal);
	//float2 uv = SampleSphericalMap(normalize(-input.wnormal)); 
	//float3 sky_color = env.Sample(g_LinearSampler,uv).rgb * expo * color.rgb;
	//return float4(sky_color,1.0);
	//return 1.0.xxxx;
	//return float4(SkyBox.Sample(g_LinearClampSampler, normalize(input.wnormal)).rgb, 4.0);
	return lerp(GROUND_COLOR,SKY_COLOR,lerp(0.7,1.0,input.wnormal.y)).xyzz * 2.0;
}
