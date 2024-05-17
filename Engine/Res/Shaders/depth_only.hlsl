//info bein
//name: DepthOnly
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
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

CBufBegin
	uint shadow_index;
CBufEnd

PSInput VSMain(VSInput v)
{
	PSInput result;
	result.position = TransformToLightSpace(shadow_index,v.position);
	return result;
}

void PSMain(PSInput input)
{

}
