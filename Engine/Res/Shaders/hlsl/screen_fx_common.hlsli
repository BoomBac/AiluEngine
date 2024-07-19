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

// half3 BilateralBlur(float2 uv,float2 offset,float space_sigma, float range_sigma)
// {
//     float weight_sum = 0;
//     float3 color_sum = 0;
//     float3 normal_origin = SAMPLE_TEXTURE2D(_CameraNormalsTexture, sampler_CameraNormalsTexture, uv);
//     half3 color = 0;
//     for(int i = -_KernelSize; i < _KernelSize; i++)
//     {
//         //空域高斯
//         float2 varible = uv + float2(i * _MainTex_TexelSize.x * offset.x, i * _MainTex_TexelSize.y * offset.y);
//         float space_factor = i * i;
//         space_factor = (-space_factor) / (2 * space_sigma * space_sigma);
//         float space_weight = 1/(space_sigma * space_sigma * 2 * PI) * exp(space_factor);

//         //值域高斯
//         float3 normal_neighbor = SAMPLE_TEXTURE2D(_CameraNormalsTexture, sampler_CameraNormalsTexture, varible);
//         float3 normal_distance = (normal_neighbor - normal_origin);
//         float value_factor = normal_distance.r * normal_distance.r ;
//         value_factor = (-value_factor) / (2 * range_sigma * range_sigma);
//         float value_weight = saturate(1 / (2 * PI * range_sigma)) * exp(value_factor);

//         weight_sum += space_weight * value_weight;
//         color_sum += SAMPLE_TEXTURE2D(_MainTex, sampler_MainTex, varible) * space_weight * value_weight;
//     }
//     if(weight_sum > 0)
//     {
//         color = color_sum / weight_sum;
//     }
//     return color;
// }

// half3 BilateralBlur(float2 uv,float space_sigma, float range_sigma)
// {
//     float3 v = BilateralBlur(uv,float2(0.0,1.0),space_sigma,range_sigma);
//     float3 h = BilateralBlur(uv,float2(1.0,0.0),space_sigma,range_sigma);
//     return (v + h) / 2;
// }

// static const half kGeometryCoeff = half(0.8);
// half CompareNormal(half3 d1, half3 d2)
// {
//     return smoothstep(kGeometryCoeff, half(1.0), dot(d1, d2));
// }

// // Geometry-aware separable bilateral filter
// half4 Blur(float2 uv, float2 delta)
// {
//     half3 p0 =  GetNormal(uv                 );
//     half3 p1a = GetNormal(uv - delta * 1.3846153846);
//     half3 p1b = GetNormal(uv + delta * 1.3846153846);
//     half3 p2a = GetNormal(uv - delta * 3.2307692308);
//     half3 p2b = GetNormal(uv + delta * 3.2307692308);

//     half3 n0 = GetNormal(uv);

//     half w0  =                              half(0.2270270270);
//     half w1a = CompareNormal(n0, p1a.xyz) * half(0.3162162162);
//     half w1b = CompareNormal(n0, p1b.xyz) * half(0.3162162162);
//     half w2a = CompareNormal(n0, p2a.xyz) * half(0.0702702703);
//     half w2b = CompareNormal(n0, p2b.xyz) * half(0.0702702703);

//     half s = half(0.0);
//     s += SAMPLE_TEXTURE2D(_MainTex, sampler_MainTex, uv)  * w0;
//     s += SAMPLE_TEXTURE2D(_MainTex, sampler_MainTex, uv - delta * 1.3846153846) * w1a;
//     s += SAMPLE_TEXTURE2D(_MainTex, sampler_MainTex, uv + delta * 1.3846153846) * w1b;
//     s += SAMPLE_TEXTURE2D(_MainTex, sampler_MainTex, uv - delta * 3.2307692308) * w2a;
//     s += SAMPLE_TEXTURE2D(_MainTex, sampler_MainTex, uv + delta * 3.2307692308) * w2b;
//     s *= rcp(w0 + w1a + w1b + w2a + w2b);

//     return s.xxxx;
// }
#endif //__SCREEN_FX_COMMON__