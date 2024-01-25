//info bein
//name: bone_test
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
//info end

#include "common.hlsli"

struct VSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float4 bone_weight : BONEWEIGHT;
	uint4 bone_indices : BONEINDEX;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float3 wnormal : NORMAL;
	float3 color : COLOR;
};

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformToClipSpace(v.position);
	result.wnormal = normalize(v.position);
	result.color = v.bone_weight[2];
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(input.color,1.0) ; 
}