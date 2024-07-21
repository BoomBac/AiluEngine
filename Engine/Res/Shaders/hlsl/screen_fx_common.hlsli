#ifndef __SCREEN_FX_COMMON__
#define __SCREEN_FX_COMMON__

#include "common.hlsli"


TEXTURE2D(_CameraDepthTexture,0)
TEXTURE2D(_CameraNormalsTexture,1)


#define UNITY_REVERSED_Z 1
#define DEFERED_RENDERING 1

//generate a random dir based on uv
float3 Rand3D(float2 uv)
{
    float3 noise = float3
    (
        frac(sin(dot(uv.xy, float2(12.9898,78.233))) * 43758.5453),
        frac(sin(dot(uv.xy, float2(12.9898,78.233)*2.0)) * 43758.5453),
        0.5f
    );
    return noise * 2.0 - 1.0;
}

//return random value 0~1
float Random(float2 p) 
{
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453123);
}

float3 GetPosViewSpace(float2 screen_pos)
{
    float2 uv = screen_pos;
    float depth = SAMPLE_TEXTURE2D_LOD(_CameraDepthTexture, g_LinearWrapSampler, uv,0).x;
#if !UNITY_REVERSED_Z
    depth = depth * 2 - 1;
#endif

#if UNITY_REVERSED_Z
    uv.y = 1 - uv.y;
#endif
    float4 clipPos = float4(2.0f * uv - 1.0f, depth, 1.0);//
    float4 worldSpacePos = mul(_MatrixIVP, clipPos);
    worldSpacePos /= worldSpacePos.w;
    float4 hviewPos = mul(_MatrixV,worldSpacePos);
    hviewPos.z *= -1.0;
    float3 viewPos = hviewPos.xyz / hviewPos.w;
    //return worldSpacePos.xyz;
    
    return viewPos;
}
float3 GetPosViewSpace(float2 screen_pos,float depth)
{
    float2 uv = screen_pos;
#if !UNITY_REVERSED_Z
    depth = depth * 2 - 1;
#endif

#if UNITY_REVERSED_Z
    uv.y = 1 - uv.y;
#endif
    float4 clipPos = float4(2.0f * uv - 1.0f, depth, 1.0);//   
    float4 worldSpacePos = mul(_MatrixIVP, clipPos);
    worldSpacePos /= worldSpacePos.w;
    float4 hviewPos = mul(_MatrixV,worldSpacePos);
    hviewPos.z *= -1.0;
    float3 viewPos = hviewPos.xyz / hviewPos.w;
    return viewPos;
}

float3 GetNormalViewSpace(float2 screen_pos)
{
#ifdef DEFERED_RENDERING
    float2 packed_n = SAMPLE_TEXTURE2D(_CameraNormalsTexture, g_LinearClampSampler, screen_pos).xy;
    float3 n = UnpackNormal(packed_n);
#else
    float3 n = SAMPLE_TEXTURE2D(_CameraNormalsTexture, g_LinearClampSampler, screen_pos).xyz;
    n = n * 2 - 1;
#endif
    n = TransformWorldToViewDir(n);
    n.z *= -1.0f;
    return n;
}
float3 GetNormalViewSpace(float2 screen_pos,float3 n)
{
    n = TransformWorldToViewDir(n);
    n.z *= -1.0f;
    return n;
}

float3 GetNormal(float2 screen_pos)
{
#ifdef DEFERED_RENDERING 
    float2 packed_n = SAMPLE_TEXTURE2D(_CameraNormalsTexture, g_LinearClampSampler, screen_pos).xy;
    float3 n = UnpackNormal(packed_n);
#else
    float3 n = SAMPLE_TEXTURE2D(_CameraNormalsTexture, g_LinearClampSampler, screen_pos).xyz;
    n = n * 2 - 1;
#endif
    return n;
}

float GetRawDepth(float2 screen_pos)
{
    return SAMPLE_TEXTURE2D(_CameraDepthTexture, g_LinearClampSampler, screen_pos).x;
}

// half3 GaussianBlur(float2 uv, float sigma)
// {
//     float weight_sum = 0;
//     float3 color_sum = 0;

//     half3 color = 0;
    
//     //此处就是基本的高斯公式的计算，作为理解学习，不使用两个pass分别模糊。
//     for(int i = -_KernelSize; i < _KernelSize; i++)
//     {
//         for(int j = -_KernelSize; j < _KernelSize; j++)
//         {
//             float2 varible = uv + float2(i * _MainTex_TexelSize.x, j * _MainTex_TexelSize.y);
//             float factor = i * i + j * j;
//             factor = (-factor) / (2 * sigma * sigma);
//             float weight = 1/(sigma * sigma * 2 * PI) * exp(factor);
//             //权重累积
//             weight_sum += weight;
//             half3 color = SAMPLE_TEXTURE2D(_MainTex, g_LinearClampSampler, varible) * weight;
//             color_sum += color;
//         }
//     }

//     if(weight_sum > 0)
//     {
//         color = color_sum / weight_sum;
//     }

//     return color;
// }

#endif //__SCREEN_FX_COMMON__