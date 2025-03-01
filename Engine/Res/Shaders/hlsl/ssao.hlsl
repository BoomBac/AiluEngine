#ifndef __SSAO_H__
#define __SSAO_H__
//info bein
//pass begin::
//name: SSAO Gen
//vert: FullscreenVSMain
//pixel: SSAOGenPSMain
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Always
//ZWrite: Off
//pass end::
//pass begin::
//name: SSAO BlurX
//vert: FullscreenVSMain
//pixel: SSAOBlurXPSMain
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Always
//ZWrite: Off
//pass end::
//pass begin::
//name: SSAO BlurY
//vert: FullscreenVSMain
//pixel: SSAOBlurYPSMain
//Cull: Back
//Queue: Opaque
//Fill: Solid
//ZTest: Always
//ZWrite: Off
//pass end::
//info end

#include "fullscreen_quad.hlsli"
#include "screen_fx_common.hlsli"

#define QUALITY_HIGH

PerMaterialCBufferBegin
    float4 _HBAOParams;
    float4 _AOScreenParams;
    float _KernelSize;
    float _Space_Sigma;
    float _Range_Sigma;
    float _padding;
PerMaterialCBufferEnd


#define INTENSITY _HBAOParams.x
#define RADIUS _HBAOParams.y
#define MAXRADIUSPIXEL _HBAOParams.z
#define ANGLEBIAS _HBAOParams.w

static const float R = 1;
static const float R2 = R * R;
static const float NegInvR2 = - 1.0 / (R*R);
static const float TanBias = tan(30.0 * PI / 180.0);
static const float MaxRadiusPixels = 50.0;

#if defined(QUALITY_HIGH)
#define DIRECTION_COUNT 8
#define STEP_COUNT 6
static const float2 kDirections[8] = {
    float2(0, 1),
    float2(0, -1),
    float2(-1, 0),
    float2(1, 0),
    float2(-0.7071068, 0.7071068), // 45 degrees
    float2(0.7071068, -0.7071068), // 225 degrees
    float2(-0.7071068, -0.7071068), // 315 degrees
    float2(0.7071068, 0.7071068) // 135 degrees
};
#elif defined(QUALITY_MEDIUM)
#define DIRECTION_COUNT 6
#define STEP_COUNT 4
static const float2 kDirections[6] = {
    float2(0, 1),
    float2(0, -1),
    float2(-0.8660254, 0.5), // 60 degrees
    float2(0.8660254, -0.5), // 240 degrees
    float2(-0.8660254, -0.5), // 300 degrees
    float2(0.8660254, 0.5) // 120 degrees
};

#elif defined(QUALITY_LOW)
#define DIRECTION_COUNT 4
#define STEP_COUNT 2
static const float2 kDirections[4] = {
    float2(0,1),
    float2(0,-1),
    float2(-1,0),
    float2(1,0)
};
#endif

float2x2 BuildRotationMatrix(float2 uv)
{
    float r = Random(uv) * DoublePI;
    float cosAngle = cos(r);
    float sinAngle = sin(r);
    return float2x2(cosAngle, -sinAngle, sinAngle, cosAngle);
}

float3 MinDiff(float3 P, float3 Pr, float3 Pl)
{
    float3 V1 = Pr - P;
    float3 V2 = P - Pl;
    return (length(V1) < length(V2)) ? V1 : V2;
}


float TanToSin(float x)
{
	return x * rsqrt(x*x + 1.0);
}

float InvLength(float2 V)
{
	return rsqrt(dot(V,V));
}

float Tangent(float3 V)
{
	return V.z * InvLength(V.xy);
}

float BiasedTangent(float3 V)
{
	return V.z * InvLength(V.xy) + TanBias;
}

float Tangent(float3 P, float3 S)
{
    return -(P.z - S.z) * InvLength(S.xy - P.xy);
}

float2 SnapUVOffset(float2 uv,float2 screen_size)
{
    return round(uv * screen_size) * 1.0 / screen_size;
}

float Falloff(float d2)
{
	return d2 * NegInvR2 + 1.0f;
}


FullScreenPSInput FullscreenVSMain(FullScreenVSInput v);

float4 SSAOGenPSMain(FullScreenPSInput i) : SV_TARGET
{
    float2 AORes = _AOScreenParams.zw;
    float2 InvAORes = _AOScreenParams.xy;
    float2 uv = i.uv;//float2(i.uv.x,1-i.uv.y);

    float3 vpos = GetPosViewSpace(uv);
    float3 vnormal = GetNormalViewSpace(uv);
    //vnormal.z *= -1;
    float2x2 rot = BuildRotationMatrix(uv);
    float2 noise = float2(Random(uv.xy),Random(uv.yx));
    //float stride = min(RADIUS / -vpos.z, MAXRADIUSPIXEL) / (STEP_COUNT + 1.0);
    float stride = (0.1f * RADIUS / -vpos.z) / (STEP_COUNT + 1.0);
   // if (stride < 1) return 1.0;
    half ao = 0.0;
    float3 pr = GetPosViewSpace(uv + float2(InvAORes.x,0.0));
    float3 pl = GetPosViewSpace(uv + float2(-InvAORes.x,0.0));
    float3 pt = GetPosViewSpace(uv + float2(0.0,InvAORes.y));
    float3 pb = GetPosViewSpace(uv + float2(0.0,-InvAORes.y));
    float3 dPdu = MinDiff(vpos, pr, pl);
    float3 dPdv = MinDiff(vpos, pt, pb) * (AORes.y * InvAORes.x);
    for (int d = 0; d < DIRECTION_COUNT; d++) 
    {
        float2 ray_dir = mul(rot,kDirections[d]);
        float ray_length = frac(noise.y) * stride + 1.0;
        float2 deltaUV = ray_dir * ray_length * InvAORes;
        // Offset the first coord with some noise
        float2 cur_uv = uv;
        deltaUV = SnapUVOffset(deltaUV,AORes);
        float3 T = deltaUV.x * dPdu + deltaUV.y * dPdv;
        float tanH = BiasedTangent(T);
        float sinH = TanToSin(tanH);
        float tanS;
        float d2;
        float3 S;
        for (int s = 0; s < STEP_COUNT; s++) 
        {
            cur_uv += deltaUV;
            S = GetPosViewSpace(cur_uv);
            tanS = Tangent(vpos, S);
            d2 = length(S - vpos);
            if(d2 < R2 && tanS > tanH)
            {
                float sinS = TanToSin(tanS);
                // Apply falloff based on the distance
                ao += Falloff(d2) * (sinS - sinH);
                tanH = tanS;
                sinH = sinS;
            }
        }
    }
    ao = saturate(1.0 - ao / DIRECTION_COUNT * INTENSITY);
    return ao.xxxx;
    //return pr.xyzz;
}


TEXTURE2D(_SourceTex)

half3 BilateralBlur(float2 uv,float2 offset,float space_sigma, float range_sigma)
{
    float weight_sum = 0;
    float3 color_sum = 0;
    float3 normal_origin = SAMPLE_TEXTURE2D(_CameraNormalsTexture, g_LinearClampSampler, uv);
    half3 color = 0;
    for(int i = -_KernelSize; i < _KernelSize; i++)
    {
        //空域高斯
        float2 varible = uv + float2(i * _AOScreenParams.x * offset.x, i * _AOScreenParams.y * offset.y);
        float space_factor = i * i;
        space_factor = (-space_factor) / (2 * space_sigma * space_sigma);
        float space_weight = 1/(space_sigma * space_sigma * 2 * PI) * exp(space_factor);

        //值域高斯
        float3 normal_neighbor = SAMPLE_TEXTURE2D(_CameraNormalsTexture, g_LinearClampSampler, varible);
        float3 normal_distance = (normal_neighbor - normal_origin);
        float value_factor = normal_distance.r * normal_distance.r ;
        value_factor = (-value_factor) / (2 * range_sigma * range_sigma);
        float value_weight = saturate(1 / (2 * PI * range_sigma)) * exp(value_factor);

        weight_sum += space_weight * value_weight;
        color_sum += SAMPLE_TEXTURE2D(_SourceTex, g_LinearClampSampler, varible) * space_weight * value_weight;
    }
    if(weight_sum > 0)
    {
        color = color_sum / weight_sum;
    }
    return color;
}

float4 SSAOBlurYPSMain(FullScreenPSInput i): SV_TARGET
{
    half3 c = BilateralBlur(i.uv,float2(0.0,1.0),_Space_Sigma,_Range_Sigma);
    return float4(c,1.0);
}
float4 SSAOBlurXPSMain(FullScreenPSInput i): SV_TARGET
{
    half3 c = BilateralBlur(i.uv,float2(1.0,0.0),_Space_Sigma,_Range_Sigma);
    return float4(c,1.0);
}
// half3 BilateralBlur(float2 uv,float space_sigma, float range_sigma)
// {
//     float3 v = BilateralBlur(uv,float2(0.0,1.0),space_sigma,range_sigma);
//     float3 h = BilateralBlur(uv,float2(1.0,0.0),space_sigma,range_sigma);
//     return (v + h) / 2;
// }

// 

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
//     s += SAMPLE_TEXTURE2D(_MainTex, g_LinearClampSampler, uv)  * w0;
//     s += SAMPLE_TEXTURE2D(_MainTex, g_LinearClampSampler, uv - delta * 1.3846153846) * w1a;
//     s += SAMPLE_TEXTURE2D(_MainTex, g_LinearClampSampler, uv + delta * 1.3846153846) * w1b;
//     s += SAMPLE_TEXTURE2D(_MainTex, g_LinearClampSampler, uv - delta * 3.2307692308) * w2a;
//     s += SAMPLE_TEXTURE2D(_MainTex, g_LinearClampSampler, uv + delta * 3.2307692308) * w2b;
//     s *= rcp(float(w0 + w1a + w1b + w2a + w2b));
//     return s.xxxx;
// }

#endif//__SSAO_H__