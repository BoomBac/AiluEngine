//info bein
//pass begin::
//name: DepthOnly
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
//pass end::
//pass begin::
//name: DepthOnlyLinearZ
//vert: VSMainLinearZ
//pixel: LinearZPSMain
//Cull: Back
//Queue: Opaque
//pass end::
//info end

#include "common.hlsli"

struct VSInput
{
	float3 position : POSITION;
};

struct PSInput
{
	float4 position : SV_POSITION;
};

// Values used to linearize the Z buffer (http://www.humus.name/temp/Linearize%20depth.txt)
// x = 1-far/near
// y = far/near
// z = x/far
// w = y/far
// or in case of a reversed depth buffer (UNITY_REVERSED_Z is 1)
// x = -1+far/near
// y = 1
// z = x/far
// w = 1/far
//float4 _ZBufferParams;

PerMaterialCBufferBegin
	uint 	shadow_index;
	float3 _point_light_wpos;
	float   padding;
	float4 _shadow_params;
PerMaterialCBufferEnd



PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformToLightSpace(shadow_index,v.position);
	return result;
}

void PSMain(PSInput input)
{

}

#define ZBufferNear _shadow_params.x
#define ZBufferFar  _shadow_params.y

struct LinearZPSInput
{
	float4 position : SV_POSITION;
	float3 world_pos : POSITION;
};


LinearZPSInput VSMainLinearZ(VSInput v)
{
	LinearZPSInput result;
	result.position = TransformToLightSpace(shadow_index,v.position);
	//result.position.xyz /= result.position.w;
	//float dis = distance(TransformToWorldSpace(v.position).xyz,_point_light_wpos);
	// result.position.z = (dis - ZBufferNear) / (ZBufferFar - ZBufferNear);
	// result.position.z = saturate(result.position.z);
	// result.position.z *= result.position.w;
	result.world_pos = TransformToWorldSpace(v.position).xyz;
	return result;
}

float LinearZPSMain(LinearZPSInput input) : SV_DEPTH
{
	float dis = distance(input.world_pos,_point_light_wpos);
	float linear_z = (dis - ZBufferNear) / (ZBufferFar - ZBufferNear);
	return linear_z;
}