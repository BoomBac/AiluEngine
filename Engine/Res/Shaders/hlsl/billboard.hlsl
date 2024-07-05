//info bein
//pass begin::
//name: billboard
//vert: VSMain
//pixel: PSMain
//Cull: Off
//Blend: Src,OneMinusSrc
//Queue: Transparent
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
};
TEXTURE2D(_MainTex,0)

PSInput VSMain(VSInput v)
{
	PSInput result;
// 获取视图矩阵的旋转部分
    float3x3 viewRotation = (float3x3)_MatrixV;

    // 在世界坐标系中旋转顶点
    float3 rotatedPos = mul(v.position, viewRotation);

    // 将旋转后的顶点位置转换为世界坐标系
    float4 worldPos = float4(rotatedPos + _CameraPos.xyz, 1.0f);

    // 转换为裁剪坐标系
    worldPos = mul(worldPos, _MatrixWorld);
    float4 viewPos = mul(worldPos, _MatrixV);
    result.position = mul(viewPos, _MatrixP);
	//result.position = TransformFromWorldToClipSpace(worldPos.xyz);
    result.uv = v.uv;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return SAMPLE_TEXTURE2D(_MainTex,g_LinearClampSampler,input.uv) * 0.001 + float4(1,0,1,1);
}