//info bein
//pass begin::
//name: pick_buffer
//vert: VSMain
//pixel: PSMain
//Cull: Off
//Queue: Opaque
//pass end::
//info end

#define AL_SCENE_PRIMITIVE 1
#include "common.hlsli"
#include "primitive.hlsli"

struct VSInput
{
	float3 position : POSITION;
	uint instance_id : SV_INSTANCEID;
};

struct PSInput
{
	float4 position : SV_POSITION;
	nointerpolation uint entity_id : TEXCOORD0;
	nointerpolation uint submesh_id : TEXCOORD1;
};

PSInput VSMain(VSInput v)
{
	PSInput result;
	const PrimitiveData primitive = LoadPrimitive(v.instance_id);
	result.position = TransformPrimitiveToClipSpaceNoJitter(primitive, v.position);
	result.entity_id = primitive._entity_id;
	result.submesh_id = primitive._submesh_id;
	return result;
}

uint PSMain(PSInput input) : SV_TARGET
{
	uint packed = 0;
	packed |= (input.entity_id & 0xFFFFFF) << 8; // 使用高 24 位存储 Entity index
	packed |= (input.submesh_id & 0xFF); // 使用低 8 位存储 Submesh ID
	return packed;
}
