//info bein
//pass begin::
//name: pick_buffer
//vert: VSMain
//pixel: PSMain
//Cull: Off
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

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformToClipSpaceNoJitter(v.position);
	return result;
}

uint PSMain(PSInput input) : SV_TARGET
{
	uint packed = 0;
	packed |= (_ObjectID & 0xFFFFFF) << 8; // 使用高 24 位存储 Entity ID
	packed |= (_SubmeshID & 0xFF); // 使用低 8 位存储 Submesh ID
	return packed;
}