//info bein
//pass begin::
//name: cubemap_debug
//vert: VSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
//multi_compile _ DEBUG_REFLECT
//pass end::
//Properties
//{
//}
//info end

#include "common.hlsli"
#include "constants.hlsli"

//TEXTURECUBE(_SrcCubeMap,0)
TextureCube _SrcCubeMap : register(t0);

struct VSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float3 wnormal : NORMAL;
	float3 world_pos : TEXCOORD0;
};


PerMaterialCBufferBegin
	float _mipmap;
	float3 _padding;
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
	//result.position.z = result.position.w;
	result.wnormal = v.position;
	result.world_pos = TransformToWorldSpace(v.position);
	return result;
}


float4 PSMain(PSInput input) : SV_TARGET
{
	input.wnormal = normalize(input.wnormal);
	float3 uvw = input.wnormal;
#ifdef DEBUG_REFLECT
	uvw = -uvw;//(input.wnormal,input.world_pos - GetCameraPositionWS());
#endif
	return float4( SAMPLE_TEXTURECUBE_LOD(_SrcCubeMap,g_LinearWrapSampler,uvw,_mipmap).rgb, 1.0);
	//float2 uv = SampleSphericalMap(normalize(-input.wnormal)); 
	//float3 sky_color = env.Sample(g_LinearSampler,uv).rgb * expo * color.rgb;
	//return float4(sky_color,1.0);
	//return 1.0.xxxx;
	//return lerp(GROUND_COLOR,SKY_COLOR,lerp(0.7,1.0,input.wnormal.y)).xyzz * 2.0;
}
