//info bein
//pass begin::
//name: transform_gizmo
//vert: VSMain
//pixel: PSMain
//Cull: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
//ZTest: Always
//ZWrite: Off
//multi_compile _ _CIRCLE
//pass end::
//info end

#include "cbuffer.hlsli"
#include "common.hlsli"

struct VSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
};
PerMaterialCBufferBegin
    float4 _color;
	float _radius;
PerMaterialCBufferEnd

struct PSInput
{
	float4 position : SV_POSITION;
    float3 normal : NORMAL;
#if defined(_CIRCLE)
	float3 world_pos : TEXCOORD0;
	float3 center_pos : TEXCOORD1;
#endif
};

PSInput VSMain(VSInput v)
{
	PSInput result;
	float3 world_pos = TransformObjectToWorld(v.position).xyz;
	result.position = TransformWorldToHClipNoJitter(world_pos);
	result.normal = TransformNormal(v.normal);
#if defined(_CIRCLE)
	result.center_pos = float3(_MatrixWorld[0].w,_MatrixWorld[1].w,_MatrixWorld[2].w);
	result.world_pos = world_pos;
#endif
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
#if defined(_CIRCLE)
	float d = distance(input.world_pos,input.center_pos);
	float r = _radius;
	float w = _radius * 0.05;
	float outerAlpha = smoothstep(r - w, r, d);
	float innerAlpha = smoothstep(r, r + w, d);
	float alpha = pow(saturate(outerAlpha * (1.0 - innerAlpha)),2);
	return float4(_color.rgb, _color.a * alpha);
#else
	return _color;
#endif
}