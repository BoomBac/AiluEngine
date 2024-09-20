//info bein
//pass begin::
//name: forward_water
//vert: ForwardVSMain
//pixel: ForwardPSMain
//Cull: Back
//ZWrite: Off
//Queue: Transparent
//Blend: Src,OneMinusSrc
//pass end::
//pass begin::
//name: ShadowCaster
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
//pass end::
//Properties
//{
//  _DeepColor("DeepColor",Color) = (1,1,1,1)
//  _LightColor("LightColor",Color) = (1,1,1,1)
//	_NormalTex("Normal",Texture2D) = "white"
//}
//info end

#include "common.hlsli"
#include "lighting.hlsli"

PerMaterialCBufferBegin
	float4 _DeepColor;
	float4 _LightColor;
	//for shadow
	uint 	shadow_index;
	float3 _point_light_wpos;
	float   padding;
	float4 _shadow_params;
PerMaterialCBufferEnd

StandardPSInput ForwardVSMain(StandardVSInput v)
{
	StandardPSInput result;
	result.normal = v.normal;
	result.uv0 = v.uv0;
	v.tangent.xyz *= v.tangent.w;
	float3 T = TransformNormal(v.tangent.xyz);
	float3 B = TransformNormal(cross(v.tangent.xyz, v.normal));
	float3 N = TransformNormal(v.normal);
	result.btn = float3x3(T, B, N);
	result.normal = N;
	result.world_pos = TransformToWorldSpace(v.position);
	result.world_pos.y += sin(_Time.x * 0.1f + result.world_pos.x * 4) * 0.25;
	result.position = TransformFromWorldToClipSpace(result.world_pos);
	result.shadow_pos = TransformFromWorldToLightSpace(0, result.world_pos);
	return result;
}

float4 ForwardPSMain(StandardPSInput input) : SV_TARGET
{
    return _DeepColor;
}

StandardPSInput VSMain(StandardVSInput v)
{
	StandardPSInput result;
	result.normal = v.normal;
	result.uv0 = v.uv0;
	v.tangent.xyz *= v.tangent.w;
	float3 T = TransformNormal(v.tangent.xyz);
	float3 B = TransformNormal(cross(v.tangent.xyz, v.normal));
	float3 N = TransformNormal(v.normal);
	result.btn = float3x3(T, B, N);
	result.normal = N;
	result.world_pos = TransformToWorldSpace(v.position);
	result.world_pos.y += sin(_Time.x * 0.1f + result.world_pos.x * 4) * 0.25;
	result.position = TransformFromWorldToLightSpace(shadow_index,result.world_pos);
	//result.shadow_pos = TransformFromWorldToLightSpace(0, result.world_pos);
	return result;
}
void PSMain(StandardPSInput input)
{
}