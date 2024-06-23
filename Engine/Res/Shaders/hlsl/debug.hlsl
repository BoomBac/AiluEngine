//info bein
//pass begin::
//name: shader_error
//vert: VSMain
//pixel: PSMainError
//Cull: Off
//Queue: Opaque
//pass end::
//pass begin::
//name: shader_compiling
//vert: VSMain
//pixel: PSMainCompiling
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
	result.position = TransformToClipSpace(v.position);
	return result;
}

float4 PSMainError(PSInput input) : SV_TARGET
{
	return float4(1.0,0.0,1.0,1.0);
}

float4 PSMainCompiling(PSInput input) : SV_TARGET
{
	return float4(0.0,1.0,1.0,1.0);
}