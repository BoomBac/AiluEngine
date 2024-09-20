//info bein
//pass begin::
//name: TAA
//vert: FullscreenVSMain
//pixel: PSMain
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Always
//ZWrite: Off
//pass end::
//info end
#include "common.hlsli"
#include "fullscreen_quad.hlsli"

PSInput FullscreenVSMain(uint vertex_id : SV_VERTEXID);
float4 PSMain(PSInput IN);


TEXTURE2D(_HistoryFrameColor);
TEXTURE2D(_CurFrameColor);
TEXTURE2D(_CameraDepthTexture);
TEXTURE2D(_MotionVectorTexture);

PerMaterialCBufferBegin
    float4   _CameraDepthTexture_TexelSize;
    float4   _CurFrameColor_TexelSize;
    float4   _TAAParams;
    float4x4 _MatrixVP_PreFrame;
    float4x4 _MatrixVP_CurFrame;
PerMaterialCBufferEnd

#define _TAAJitter          _TAAParams.xy
#define _CurrentColorFactor _TAAParams.z
#define _SharpFactor        _TAAParams.w

#define _USE_YCGCO 1

float2 reprojection(float depth, float2 uv)
{
    //depth = 1.0 - depth;
    uv.y = 1 - uv.y;
    //depth = 2.0 * depth - 1.0;
    float3 world_pos = Unproject(uv,depth);
    float4 prevClipPos = mul(_MatrixVP_PreFrame, float4(world_pos,1.0f));
    float2 prevPosCS = prevClipPos.xy / prevClipPos.w;
    return prevPosCS * 0.5 + 0.5;
}

float3 RGBToYCoCg(float3 rgb) 
{
    // Coefficients for YCoCg transformation
    float3x3 RGBToYCoCgMatrix = float3x3(
        0.25,  0.5,  0.25,
        0.5,   0.0, -0.5,
        -0.25, 0.5, -0.25
    );

    // Apply the transformation
    return mul(RGBToYCoCgMatrix, rgb);
}

float3 YCoCgToRGB(float3 ycocg) 
{
    // Coefficients for inverse YCoCg transformation
    float3x3 YCoCgToRGBMatrix = float3x3(
        1.0,  1.0, -1.0,
        1.0,  0.0,  1.0,
        1.0, -1.0, -1.0
    );
    // Apply the transformation
    return mul(YCoCgToRGBMatrix, ycocg);
}

//当物体遮挡关系发生改变时，突然出现的物体不存在于历史帧，这里选择离相机最近的点来减弱这个影响
float2 GetClosestFragment(float2 uv)
{
    float2 k = _CameraDepthTexture_TexelSize.xy;
    //在上下左右四个点
    const float4 neighborhood = float4(
        SAMPLE_TEXTURE2D(_CameraDepthTexture, g_PointClampSampler, saturate(uv - k)).r,
        SAMPLE_TEXTURE2D(_CameraDepthTexture, g_PointClampSampler, saturate(uv + float2(k.x, -k.y))).r,
        SAMPLE_TEXTURE2D(_CameraDepthTexture, g_PointClampSampler, saturate(uv + float2(-k.x, k.y))).r,
        SAMPLE_TEXTURE2D(_CameraDepthTexture, g_PointClampSampler, saturate(uv + k)).r
    );
    // 获取离相机最近的点
    #if defined(UNITY_REVERSED_Z)
        #define COMPARE_DEPTH(a, b) step(b, a)
    #else
        #define COMPARE_DEPTH(a, b) step(a, b)
    #endif
    // 获取离相机最近的点，这里使用 lerp 是避免在shader中写分支判断,原实现使用点过滤
    float3 result = float3(0.0, 0.0, SAMPLE_TEXTURE2D(_CameraDepthTexture, g_LinearClampSampler, uv).r);
    result = lerp(result, float3(-1.0, -1.0, neighborhood.x), COMPARE_DEPTH(neighborhood.x, result.z));
    result = lerp(result, float3( 1.0, -1.0, neighborhood.y), COMPARE_DEPTH(neighborhood.y, result.z));
    result = lerp(result, float3(-1.0,  1.0, neighborhood.z), COMPARE_DEPTH(neighborhood.z, result.z));
    result = lerp(result, float3( 1.0,  1.0, neighborhood.w), COMPARE_DEPTH(neighborhood.w, result.z));
    return (uv + result.xy * k);
}

void GetCurColorSamples(float2 uv, out float3 samples[9])
{
    samples[0] = SAMPLE_TEXTURE2D(_CurFrameColor, g_LinearClampSampler, uv + _CurFrameColor_TexelSize.xy * float2(-1, -1));
    samples[1] = SAMPLE_TEXTURE2D(_CurFrameColor, g_LinearClampSampler, uv + _CurFrameColor_TexelSize.xy * float2( 0, -1));
    samples[2] = SAMPLE_TEXTURE2D(_CurFrameColor, g_LinearClampSampler, uv + _CurFrameColor_TexelSize.xy * float2( 1, -1));
    samples[3] = SAMPLE_TEXTURE2D(_CurFrameColor, g_LinearClampSampler, uv + _CurFrameColor_TexelSize.xy * float2(-1,  0));
    samples[4] = SAMPLE_TEXTURE2D(_CurFrameColor, g_LinearClampSampler, uv);
    samples[5] = SAMPLE_TEXTURE2D(_CurFrameColor, g_LinearClampSampler, uv + _CurFrameColor_TexelSize.xy * float2( 1,  0));
    samples[6] = SAMPLE_TEXTURE2D(_CurFrameColor, g_LinearClampSampler, uv + _CurFrameColor_TexelSize.xy * float2(-1,  1));
    samples[7] = SAMPLE_TEXTURE2D(_CurFrameColor, g_LinearClampSampler, uv + _CurFrameColor_TexelSize.xy * float2( 0,  1));
    samples[8] = SAMPLE_TEXTURE2D(_CurFrameColor, g_LinearClampSampler, uv + _CurFrameColor_TexelSize.xy * float2( 1,  1));
}

float3 Filter(float3 samples[9]) 
{
    #ifdef _TAA_UseBlurSharpenFilter
        const float k_blur0 = 0.6915221;
        const float k_blur1 = 0.07002799;
        const float k_blur2 = 0.007091487;
        float3 blur_color = (samples[0] + samples[2] + samples[6] + samples[8]) * k_blur2 +
                            (samples[1] + samples[3] + samples[5] + samples[7]) * k_blur1 +
                            samples[4] * k_blur0;
        float3 avg_color = (samples[0] + samples[1] + samples[2]
                        + samples[3] + samples[4] + samples[5]
                        + samples[6] + samples[7] + samples[8]) / 9;
        float3 sharp_color = blur_color + (blur_color - avg_color) * _SharpFactor * 3;
        return clamp(sharp_color, 0, 65472.0);
    #else
        return samples[4];
    #endif
}
void Clip(float3 samples[9],inout float4 cliped_color)
{
    float3 AABBMin, AABBMax;
    cliped_color.rgb = RGBToYCoCg(cliped_color.rgb);
    AABBMax = AABBMin = cliped_color;
    for(int i = 0; i < 9; i++)
    {
        float3 C = RGBToYCoCg(samples[i]);
        AABBMin = min(AABBMin, C);
        AABBMax = max(AABBMax, C);
    }
    cliped_color.rgb = YCoCgToRGB(clamp(cliped_color.rgb,AABBMin,AABBMax));
}

void minmax(float3 samples[9], out float3 min_color, out float3 max_color)
{
    float3 color[9];
#ifdef _USE_YCGCO
    color[0] = RGBToYCoCg(samples[0]);
    color[1] = RGBToYCoCg(samples[1]);
    color[2] = RGBToYCoCg(samples[2]);
    color[3] = RGBToYCoCg(samples[3]);
    color[4] = RGBToYCoCg(samples[4]);
    color[5] = RGBToYCoCg(samples[5]);
    color[6] = RGBToYCoCg(samples[6]);
    color[7] = RGBToYCoCg(samples[7]);
    color[8] = RGBToYCoCg(samples[8]);
#else
    color[0] = samples[0];
    color[1] = samples[1];
    color[2] = samples[2];
    color[3] = samples[3];
    color[4] = samples[4];
    color[5] = samples[5];
    color[6] = samples[6];
    color[7] = samples[7];
    color[8] = samples[8];
#endif
//使用采样点估计期望值和标准差来确定aabb的大小，避免直接使用最大最小值在亮度差异较大的区域形成一个偏大的aabb
#ifdef _TAA_UseVarianceClipping
    float3 m1 = color[0] + color[1] + color[2]
            + color[3] + color[4] + color[5]
            + color[6] + color[7] + color[8];
    float3 m2 = color[0] * color[0] + color[1] * color[1] + color[2] * color[2]
            + color[3] * color[3] + color[4] * color[4] + color[5] * color[5]
            + color[6] * color[6] + color[7] * color[7] + color[8] * color[8];
    float3 mu = m1 / 9;
    float3 sigma = sqrt(abs(m2 / 9 - mu * mu));
#define _TAA_Gamma 1.0
    min_color = mu - _TAA_Gamma * sigma;
    max_color = mu + _TAA_Gamma * sigma;
#else
    min_color = max_color = color[0];
    for(int i = 0; i < 9; i++)
    {
        min_color = min(color[i],min_color);
        max_color = max(color[i],max_color);
    }
    // min_color = min(color[0], min(color[1], min(color[2], min(color[3], min(color[4], min(color[5], min(color[6], min(color[7], color[8]))))))));
    // max_color = max(color[0], max(color[1], max(color[2], max(color[3], max(color[4], max(color[5], max(color[6], max(color[7], color[8]))))))));
    // float3 min_color5 = min(color[1], min(color[3], min(color[4], min(color[5], color[7]))));
    // float3 max_color5 = max(color[1], max(color[3], max(color[4], max(color[5], color[7]))));
    // min_color = 0.5 * (min_color + min_color5);
    // max_color = 0.5 * (max_color + max_color5);
#endif
}
//clamp和clip，想象一个二维空间将点p限制到一个aabb，前者去aabb 四个顶点的其中一个，后者则表示p到aabb中心的连线与aabb边的交点
float3 ClipColor(float3 min_color, float3 max_color, float3 color)
{
#if _TAA_UseClipAABB
    float3 p_clip = 0.5 * (max_color + min_color);
    float3 e_clip = 0.5 * (max_color - min_color) + FLT_EPS;

    float3 v_clip = color - p_clip;
    float3 v_unit = v_clip / e_clip;
    float3 a_unit = abs(v_unit);
    float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0)
        return p_clip + v_clip / ma_unit;
    else
        return color;
#else
    return clamp(color, min_color, max_color);
#endif
}

float4 PSMain(PSInput IN) : SV_TARGET
{
    float2 uv = IN.uv;
#ifdef _USE_MV
    uv -= _TAAJitter;
    #ifdef _USE_DILATION
        float2 mv = SAMPLE_TEXTURE2D(_MotionVectorTexture, g_LinearClampSampler, GetClosestFragment(uv)).xy;
    #else
        float2 mv = SAMPLE_TEXTURE2D(_MotionVectorTexture, g_LinearClampSampler, uv).xy;
    #endif//_USE_DILATION
    float2 prev_uv = uv - mv;
    mv = uv - prev_uv;
    uv += _TAAJitter;
#else
    float depth = SAMPLE_TEXTURE2D(_CameraDepthTexture, g_PointClampSampler, uv).x;
    float2 prev_uv = reprojection(depth, uv);
    float2 mv = uv - prev_uv;
#endif//_USE_MV
    float4 history_color = SAMPLE_TEXTURE2D(_HistoryFrameColor, g_PointClampSampler, prev_uv);
    float3 samples[9];
    float3 aabb_min,aabb_max;
    GetCurColorSamples(uv,samples);
    minmax(samples,aabb_min,aabb_max);
#ifdef _USE_YCGCO
    history_color.rgb = YCoCgToRGB(ClipColor(aabb_min, aabb_max, RGBToYCoCg(history_color.rgb)));
#else
    history_color.rgb = ClipColor(aabb_min, aabb_max,history_color.rgb);
#endif//_USE_YCGCO

    float4 current_color = SAMPLE_TEXTURE2D(_CurFrameColor, g_PointClampSampler, uv);
    float final_current_color_factor = saturate(0.05f + length(mv) * 100);
    return lerp(history_color,current_color,0.05f);
}