//info bein
//pass begin::
//name: billboard
//vert: VSMain
//pixel: PSMain
//Cull: Off
//Blend: Src,OneMinusSrc
//Queue: Transparent
//ZWrite: Off
//pass end::
//pass begin::
//name: billboard_pickbuffer
//vert: VSMain
//pixel: PSMainPickBuffer
//Cull: Off
//pass end::
//Properties
//{
//  _MainTex("MainTex",Texture2D) = white
//}
//info end

#include "common.hlsli"

struct VSInput
{
	float3 position : POSITION;
    float2 uv : TEXCOORD;
};

struct PSInput
{
	float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 world_pos : TEXCOORD1;
};
TEXTURE2D(_MainTex)

PSInput VSMain(VSInput v)
{
	PSInput result;
    //_MatrixWorld[3][0] 
    float3 view_pos = TransformToViewSpace(GetObjectWorldPos());
    float3 scaleRotatePos = mul((float3x3)_MatrixWorld, v.position);
    view_pos += float3(scaleRotatePos.xy, -scaleRotatePos.z);
    float4x4 proj = _MatrixP;
    proj[0][2] -= _matrix_jitter_x;
    proj[1][2] -= _matrix_jitter_y;
    result.position = mul(proj,float4(view_pos,1));
    result.uv = v.uv;
    result.world_pos = GetObjectWorldPos();
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float alpha = SAMPLE_TEXTURE2D(_MainTex,g_LinearClampSampler,input.uv).a;
    return float4(1.0,1.0,1.0,alpha * lerp(0.0f,1.0f,distance(input.world_pos,GetCameraPositionWS()) / 100.0f));
}

uint PSMainPickBuffer(PSInput input) : SV_TARGET
{
    float alpha = SAMPLE_TEXTURE2D(_MainTex,g_LinearClampSampler,input.uv).a;
    clip(alpha - 0.5f);
	return uint(_ObjectID);
}