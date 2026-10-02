//info begin
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
#include "volumetric_fog_common.hlsli"

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
    float view_depth : TEXCOORD2;
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
    result.view_depth = view_pos.z;
    result.uv = v.uv;
    result.world_pos = GetObjectWorldPos();
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float alpha = SAMPLE_TEXTURE2D(_MainTex,g_LinearClampSampler,input.uv).a;
    float dis_attenuation = distance(input.world_pos,GetCameraPositionWS()) / 100.0f;
    float final_alpha = alpha * lerp(0.0f,1.0f,dis_attenuation);
    float3 color = ApplyVolumetricFogStraight(float3(1.0f, 1.0f, 1.0f),
                                               input.position.xy * _ScreenParams.xy, input.view_depth);
    return float4(color, final_alpha);
}

uint PSMainPickBuffer(PSInput input) : SV_TARGET
{
	uint packed = 0;
	packed |= (_ObjectID & 0xFFFFFF) << 8; // 使用高 24 位存储 Entity ID
	packed |= (_SubmeshID & 0xFF); // 使用低 8 位存储 Submesh ID
	return packed;
}
