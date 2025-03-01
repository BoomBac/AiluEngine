#pragma kernel CSMain

#include "cs_common.hlsli"
#include "../color_space_utils.hlsli"
#define GROUP_SIZE 8

CBUFFER_START(TAA_CB)
    float4 _SourceTex_TexelSize;
    float4 _Jitter;
    float _ClampQuality;
    float _HistoryQuality;
    float _MotionQuality;
    float _VarianceClampScale;
    float _Sharpness;
    float _HistoryFactor;
CBUFFER_END

TEXTURE2D(_CurrentColor)
TEXTURE2D(_HistoryColor)
TEXTURE2D(_CameraDepthTexture)
TEXTURE2D(_MotionVectorTexture)
RWTEXTURE2D(_CurrentTarget,float3)

#define _JitterPrev _Jitter.xy
#define _JitterCur _Jitter.zw

//#define TAA_YCOCG 1
#define _USE_DILATION 1
#define _FILTER_CENTER 1

half4 CAS_Sharpen(half4 color, half4 minColor, half4 maxColor, float sharpness) 
{
    return saturate(lerp(minColor, maxColor, sharpness) * color);
}

//urp TemporalAA.hlsl
half4 PostFxSpaceToLinear(float4 src)
{
// gamma 2.0 is a good enough approximation
#if TAA_GAMMA_SPACE_POST
    return half4(src.xyz * src.xyz, src.w);
#else
    return src;
#endif
}

half4 LinearToPostFxSpace(float4 src)
{
#if TAA_GAMMA_SPACE_POST
    return half4(sqrt(src.xyz), src.w);
#else
    return src;
#endif
}

// Working Space: The color space that we will do the calculation in.
// Scene: The incoming/outgoing scene color. Either linear or gamma space
half4 SceneToWorkingSpace(half4 src)
{
    half4 linColor = PostFxSpaceToLinear(src);
#if TAA_YCOCG
    half4 dst = half4(RGBToYCoCg(linColor.xyz), linColor.w);
#else
    half4 dst = src;
#endif
    return dst;
}

half4 WorkingSpaceToScene(half4 src)
{
#if TAA_YCOCG
    half4 linColor = half4(YCoCgToRGB(src.xyz), src.w);
#else
    half4 linColor = src;
#endif

    half4 dst = LinearToPostFxSpace(linColor);
    return dst;
}

float MitchellFilter(float x, float a = 0.3333) 
{
    x = abs(x);
    float x2 = x * x;
    float x3 = x2 * x;

    if (x < 1.0)
        return (a + 2.0) * x3 - (a + 3.0) * x2 + 1.0;
    else if (x < 2.0)
        return a * x3 - 5.0 * a * x2 + 8.0 * a * x - 4.0 * a;
    return 0.0;
}

//https://www.gdcvault.com/play/1022970/Temporal-Reprojection-Anti-Aliasing-in EdgeMotion
//当物体遮挡关系发生改变时，突然出现的物体不存在于历史帧，这里选择离相机最近的点来减弱这个影响;Inside中使用3x3的范围
float2 GetClosestFragment(float2 uv)
{
    float2 k = _SourceTex_TexelSize.xy;
    //在上下左右四个点
    const float4 neighborhood = float4(
        SAMPLE_TEXTURE2D_LOD(_CameraDepthTexture, g_PointClampSampler, saturate(uv - k),0).r,
        SAMPLE_TEXTURE2D_LOD(_CameraDepthTexture, g_PointClampSampler, saturate(uv + float2(k.x, -k.y)),0).r,
        SAMPLE_TEXTURE2D_LOD(_CameraDepthTexture, g_PointClampSampler, saturate(uv + float2(-k.x, k.y)),0).r,
        SAMPLE_TEXTURE2D_LOD(_CameraDepthTexture, g_PointClampSampler, saturate(uv + k),0).r
    );
    // 获取离相机最近的点
    #if defined(_REVERSED_Z)
        #define COMPARE_DEPTH(a, b) step(b, a)
    #else
        #define COMPARE_DEPTH(a, b) step(a, b)
    #endif
    // 获取离相机最近的点，这里使用 lerp 是避免在shader中写分支判断,原实现使用点过滤
    float3 result = float3(0.0, 0.0, SAMPLE_TEXTURE2D_LOD(_CameraDepthTexture, g_PointClampSampler, uv,0).r);
    result = lerp(result, float3(-1.0, -1.0, neighborhood.x), COMPARE_DEPTH(neighborhood.x, result.z));
    result = lerp(result, float3( 1.0, -1.0, neighborhood.y), COMPARE_DEPTH(neighborhood.y, result.z));
    result = lerp(result, float3(-1.0,  1.0, neighborhood.z), COMPARE_DEPTH(neighborhood.z, result.z));
    result = lerp(result, float3( 1.0,  1.0, neighborhood.w), COMPARE_DEPTH(neighborhood.w, result.z));
    return (uv + result.xy * k);
}

half4 GetCurColorSamples(float2 uv, out half4 samples[9],bool filter_center = false)
{
    const static float2 kOffsets[9] = 
    {
        float2(-1, -1), float2( 0, -1), float2( 1, -1),
        float2(-1,  0), float2( 0,  0), float2( 1,  0),
        float2(-1,  1), float2( 0,  1), float2( 1,  1)
    };
    for(int i = 0; i < 9; i++)
    {
        samples[i] = SceneToWorkingSpace(SAMPLE_TEXTURE2D_LOD(_CurrentColor, g_PointClampSampler, uv + _SourceTex_TexelSize.xy * kOffsets[i],0));
    }
    if (filter_center)
    {
        half4 center = 0.0.xxxx;
        for(int i = 0; i < 9; i++)
        {
            center += samples[i] * MitchellFilter(length(kOffsets[i]));
        }
        return center;
    }
    return samples[4];
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

void MinMax(half4 samples[9], out half4 min_color, out half4 max_color,float clamp_quality)
{
    half4 color[9];
    color[0] = SceneToWorkingSpace(samples[0]);
    color[1] = SceneToWorkingSpace(samples[1]);
    color[2] = SceneToWorkingSpace(samples[2]);
    color[3] = SceneToWorkingSpace(samples[3]);
    color[4] = SceneToWorkingSpace(samples[4]);
    color[5] = SceneToWorkingSpace(samples[5]);
    color[6] = SceneToWorkingSpace(samples[6]);
    color[7] = SceneToWorkingSpace(samples[7]);
    color[8] = SceneToWorkingSpace(samples[8]);
    min_color = max_color = color[0];
    for(int i = 0; i < 9; i++)
    {
        min_color = min(color[i],min_color);
        max_color = max(color[i],max_color);
    }
    //使用采样点估计期望值和标准差来确定aabb的大小，避免直接使用最大最小值在亮度差异较大的区域形成一个偏大的aabb
    if (clamp_quality >= 1)
    {
        half4 m1 = color[0] + color[1] + color[2]
                + color[3] + color[4] + color[5]
                + color[6] + color[7] + color[8];
        half4 m2 = color[0] * color[0] + color[1] * color[1] + color[2] * color[2]
                + color[3] * color[3] + color[4] * color[4] + color[5] * color[5]
                + color[6] * color[6] + color[7] * color[7] + color[8] * color[8];
        half per_sample = 1 / half(9);
        half4 mean = m1 * per_sample;
        half4 std_dev = sqrt(abs(m2 * per_sample - mean * mean));
        half4 dev_min = mean - _VarianceClampScale * std_dev;
        half4 dev_max = mean + _VarianceClampScale * std_dev;
        min_color = min(min_color,dev_min);
        max_color = max(max_color,dev_max);
    }
}
//clamp和clip，想象一个二维空间将点p限制到一个aabb，前者去aabb 四个顶点的其中一个，后者则表示p到aabb中心的连线与aabb边的交点
half4 ClipColor(half4 color,half4 min_color, half4 max_color)
{
    half4 p_clip = 0.5 * (max_color + min_color);
    half4 e_clip = 0.5 * (max_color - min_color) + FLOAT_EPSILON;

    half4 v_clip = color - p_clip;
    half4 v_unit = v_clip / e_clip;
    half4 a_unit = abs(v_unit);
    half ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0)
        return p_clip + v_clip / ma_unit;
    else
        return color;
}



// From Filmic SMAA presentation[Jimenez 2016]
// A bit more verbose that it needs to be, but makes it a bit better at latency hiding
// (half version based on HDRP impl)
half4 SampleBicubic5TapHalf(Texture2D sourceTexture, float2 UV, float4 sourceTexture_TexelSize)
{
    const float2 sourceTextureSize = sourceTexture_TexelSize.zw;
    const float2 sourceTexelSize = sourceTexture_TexelSize.xy;

    float2 samplePos = UV * sourceTextureSize;
    float2 tc1 = floor(samplePos - 0.5) + 0.5;
    half2 f = samplePos - tc1;
    half2 f2 = f * f;
    half2 f3 = f * f2;

    half c = 0.5;

    half2 w0 = -c         * f3 +  2.0 * c         * f2 - c * f;
    half2 w1 =  (2.0 - c) * f3 - (3.0 - c)        * f2          + 1.0;
    half2 w2 = -(2.0 - c) * f3 + (3.0 - 2.0 * c)  * f2 + c * f;
    half2 w3 = c          * f3 - c                * f2;

    half2 w12 = w1 + w2;
    float2 tc0 = sourceTexelSize  * (tc1 - 1.0);
    float2 tc3 = sourceTexelSize  * (tc1 + 2.0);
    float2 tc12 = sourceTexelSize * (tc1 + w2 / w12);

    half4 s0 = SceneToWorkingSpace(SAMPLE_TEXTURE2D_LOD(sourceTexture, g_LinearClampSampler, float2(tc12.x, tc0.y),0));
    half4 s1 = SceneToWorkingSpace(SAMPLE_TEXTURE2D_LOD(sourceTexture, g_LinearClampSampler, float2(tc0.x, tc12.y),0));
    half4 s2 = SceneToWorkingSpace(SAMPLE_TEXTURE2D_LOD(sourceTexture, g_LinearClampSampler, float2(tc12.x, tc12.y),0));
    half4 s3 = SceneToWorkingSpace(SAMPLE_TEXTURE2D_LOD(sourceTexture, g_LinearClampSampler, float2(tc3.x, tc12.y),0));
    half4 s4 = SceneToWorkingSpace(SAMPLE_TEXTURE2D_LOD(sourceTexture, g_LinearClampSampler, float2(tc12.x, tc3.y),0));

    half cw0 = (w12.x * w0.y);
    half cw1 = (w0.x * w12.y);
    half cw2 = (w12.x * w12.y);
    half cw3 = (w3.x * w12.y);
    half cw4 = (w12.x *  w3.y);

    s0 *= cw0;
    s1 *= cw1;
    s2 *= cw2;
    s3 *= cw3;
    s4 *= cw4;

    half4 historyFiltered = s0 + s1 + s2 + s3 + s4;
    half weightSum = cw0 + cw1 + cw2 + cw3 + cw4;

    half4 filteredVal = historyFiltered / weightSum;//* rcp(weightSum); make hlsl tool happy

    return filteredVal;
}

[numthreads(GROUP_SIZE,GROUP_SIZE,1)]
void CSMain(CSInput input)
{
    VALID_INDEX(_SourceTex_TexelSize.zw)
    int2 pixel_index = input.DispatchThreadID.xy;
    float2 uv = (input.DispatchThreadID.xy + 0.5.xx) * _SourceTex_TexelSize.xy;
#ifdef _USE_DILATION
    float2 mv = SAMPLE_TEXTURE2D_LOD(_MotionVectorTexture, g_PointClampSampler, GetClosestFragment(uv),0).xy;
#else
    float2 mv = SAMPLE_TEXTURE2D_LOD(_MotionVectorTexture, g_PointClampSampler, uv,0).xy;
#endif//_USE_DILATION
    half4 cur_color = SceneToWorkingSpace(_CurrentColor[pixel_index]);
    float2 prev_uv = uv - mv;
    if (any(prev_uv != saturate(prev_uv)))
    {
        _CurrentTarget[pixel_index] = WorkingSpaceToScene(cur_color).rgb;
        return;
    }
    half4 samples[9];
    half4 aabb_min,aabb_max;
#if defined(_FILTER_CENTER)
    cur_color = SceneToWorkingSpace(GetCurColorSamples(uv,samples,true));
#else
    GetCurColorSamples(uv,samples);
#endif
    MinMax(samples,aabb_min,aabb_max,_ClampQuality);
    //cur_color = CAS_Sharpen(cur_color,aabb_min,aabb_max,_Sharpness);
    half4 history_color = _HistoryQuality>= 2? SampleBicubic5TapHalf(_HistoryColor,prev_uv,_SourceTex_TexelSize) : SceneToWorkingSpace(SAMPLE_TEXTURE2D_LOD(_HistoryColor, g_LinearClampSampler, prev_uv,0));
    half4 clamped_history = _ClampQuality>= 2? ClipColor(history_color,aabb_min,aabb_max) : clamp(history_color,aabb_min,aabb_max);
    //half4 clamped_history = ClipColor(history_color,aabb_min,aabb_max);
    half4 final_color = lerp(cur_color,clamped_history,0.95);
    //final_color = CAS_Sharpen(final_color,aabb_min,aabb_max,_Sharpness);
    _CurrentTarget[pixel_index] = WorkingSpaceToScene(final_color).xyz;
}
