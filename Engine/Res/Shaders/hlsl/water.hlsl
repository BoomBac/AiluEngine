//info begin
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
//multi_compile _ CAST_POINT_SHADOW
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
#include "volumetric_fog_common.hlsli"

PerMaterialCBufferBegin
	float4 _DeepColor;
	float4 _LightColor;
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
	result.world_pos = TransformObjectToWorld(v.position);
	result.world_pos.y += sin(_Time.x*100 + result.world_pos.x * 4) * 0.25;
	result.position = TransformWorldToHClip(result.world_pos);
	return result;
}

float4 ForwardPSMain(StandardPSInput input) : SV_TARGET
{
    float3 color = _DeepColor.rgb;
    float view_depth = mul(_MatrixV, float4(input.world_pos, 1.0f)).z;
    color = ApplyVolumetricFogStraight(color, input.position.xy * _ScreenParams.xy, view_depth);
    return float4(color * GetPreExposure(), _DeepColor.a);
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
	result.world_pos = TransformObjectToWorld(v.position);
	result.world_pos.y += sin(_Time.x*100 + result.world_pos.x * 4) * 0.25;
	result.position = TransformWorldToHClip(result.world_pos);
	return result;
}
float PSMain(StandardPSInput input) : SV_Depth
{
#ifdef CAST_POINT_SHADOW
	float dis = distance(input.world_pos,_CameraPos.xyz);
	float linear_z = saturate((dis - _ZBufferParams.x) / (_ZBufferParams.y - _ZBufferParams.x));
	return linear_z;
#else
	return input.position.z;
#endif
}
