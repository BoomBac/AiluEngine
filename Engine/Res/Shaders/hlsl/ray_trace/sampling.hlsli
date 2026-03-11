#ifndef __SAMPLING_HLSLI__
#define __SAMPLING_HLSLI__
#include "../brdf.hlsli"
#include "../color_space_utils.hlsli"
#include "rt_common.hlsli"

//#define SAMPLE_BRDF_VNDF_GGX 1
//#define SAMPLE_BRDF_COSINE_HEMISPHERE 1
// n：单位法线（世界空间）
// 返回矩阵列为 (tangent, bitangent, normal)
void build_onb(float3 n, out float3 t, out float3 b)
{
    float sign = n.z >= 0 ? 1 : -1;
    float a = -1.0 / (sign + n.z);
    float b2 = n.x * n.y * a;

    t = float3(1 + sign * n.x * n.x * a, sign * b2, -sign * n.x);
    b = float3(b2, sign + n.y * n.y * a, -n.y);
}

// seed 是 uint，用于产生随机数
float3 sample_cosine_hemisphere(float3 n, float r1, float r2)
{
    float u1 = r1;
    float u2 = r2;

    float r_sqrt = sqrt(u1);
    float phi = 2.0 * 3.14159265359 * u2;
    float x = r_sqrt * cos(phi);
    float y = r_sqrt * sin(phi);
    float z = sqrt(max(0.0, 1.0 - u1)); // z 朝上（局部）

    // 局部->世界
    float3 t, b;
    build_onb(n, t, b);
    float3 sampled_dir = normalize(x * t + y * b + z * n);
    return sampled_dir;
}
//GTR2
float3 SampleGGX(float3 n, float alpha, float u1, float u2)
{
    float a2 = alpha * alpha;

    // ---- 采样 theta_h ----
    float cosTheta = sqrt((1.0-u1) / (1.0 + (a2 - 1.0) * u1));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float phi = 2.0 * PI * u2;

    float x = sinTheta * cos(phi);
    float y = sinTheta * sin(phi);
    float z = cosTheta;

    float3 t, b;
    build_onb(n, t, b);
    return normalize(x * t + y * b + z * n);
}

float3 SampleGGX_VNDF(float3 n, float3 wo, float alpha, float u1, float u2)
{
    // ---- 构建局部坐标 ----
    float3 t, b;
    build_onb(n, t, b);

    float3 V = float3(dot(wo,t), dot(wo,b), dot(wo,n));

    // ---- stretch view ----
    float3 Vh = normalize(float3(alpha * V.x, alpha * V.y, V.z));

    // ---- 构建正交基 ----
    float lensq = Vh.x*Vh.x + Vh.y*Vh.y;

    float3 T1 = lensq > 0 ? float3(-Vh.y, Vh.x, 0) * rsqrt(lensq) : float3(1,0,0);
    float3 T2 = cross(Vh, T1);

    // ---- 采样圆盘 ----
    float r = sqrt(u1);
    float phi = 2.0 * PI * u2;

    float t1 = r * cos(phi);
    float t2 = r * sin(phi);

    float s = 0.5 * (1.0 + Vh.z);
    t2 = lerp(sqrt(1.0 - t1*t1), t2, s);

    // ---- 投影回 hemisphere ----
    float3 Nh =
        t1 * T1 +
        t2 * T2 +
        sqrt(max(0.0, 1.0 - t1*t1 - t2*t2)) * Vh;

    // ---- unstretch ----
    float3 h = normalize(float3(alpha * Nh.x, alpha * Nh.y, max(0.0, Nh.z)));

    // ---- 局部转世界 ----
    return normalize(h.x * t + h.y * b + h.z * n);
}


float PdfGGX(float alpha, float dotNH, float dotVH)
{
    float nh = saturate(dotNH);
    float vh = max(saturate(dotVH), 1e-4);
    float D = D_GTR2(max(alpha, 1e-3), nh);
    // pdf_wi = D(h) * |dot(h, wo)| / 4
    return D * vh / 4.0;
}

float SmithG1GGX(float dotNV, float alpha)
{
    float a = alpha;
    float a2 = a * a;

    float b = dotNV * dotNV;

    return 2.0 * dotNV /
           (dotNV + sqrt(a2 + b - a2 * b));
}

float PdfGGX_VNDF(
    float alpha,
    float dotNH,
    float dotVH,
    float dotNV)
{
    float nh = saturate(dotNH);
    float vh = max(saturate(dotVH), 1e-4);
    float nv = max(saturate(dotNV), 1e-4);
    float a = max(alpha, 1e-3);

    float D = D_GTR2(a, nh);

    float G1 = SmithG1GGX(nv, a);

    return D * G1 / (4.0 * nv);
}

// 返回采样方向 wi 和对应的 PDF
struct SampleBRDFResult
{
    float3 wi;      // 采样方向（世界空间，指向表面外）
    float3 h;       // 微表面法线
};

float ComputeBRDFSpecularSamplingProb(float3 base_color, float metallic)
{
    float3 f0 = lerp(DIELECTRIC_SPECULAR.xxx, base_color, metallic.xxx);
    float diffuse_weight = Luminance(base_color * (1.0 - metallic));
    float specular_weight = Luminance(f0);
    float denom = max(diffuse_weight + specular_weight, 1e-5);
    return clamp(specular_weight / denom, 0.05, 0.95);
}

float PdfSpecularBRDF(float3 n, float3 wo, float3 wi, float alpha)
{
    float nv = saturate(dot(n, wo));
    float nl = saturate(dot(n, wi));
    if (nv <= 0.0 || nl <= 0.0)
        return 0.0;

    float3 h = normalize(wi + wo);
    float nh = saturate(dot(n, h));
    float vh = max(saturate(dot(wo, h)), 1e-4);

#if defined(SAMPLE_BRDF_VNDF_GGX)
    float D = D_GTR2(max(alpha, 1e-3), nh);
    float G1 = SmithG1GGX(max(nv, 1e-4), max(alpha, 1e-3));
    return D * G1 * nh / (4.0 * vh * max(nv, 1e-4) + 1e-6);
#else
    float D = D_GTR2(max(alpha, 1e-3), nh);
    return D * nh / (4.0 * vh + 1e-6);
#endif
}

float PdfDiffuseBRDF(float3 n, float3 wi)
{
    return saturate(dot(n, wi)) / PI;
}

float PdfMixedBRDF(float3 n, float3 wo, float3 wi, float alpha, float3 base_color, float metallic)
{
    float specular_prob = ComputeBRDFSpecularSamplingProb(base_color, metallic);
    float diffuse_pdf = PdfDiffuseBRDF(n, wi);
    float specular_pdf = PdfSpecularBRDF(n, wo, wi, alpha);
    return lerp(diffuse_pdf, specular_pdf, specular_prob);
}

float DielectricF0(float ior_i, float ior_t)
{
    float sum = max(ior_i + ior_t, 1e-4);
    float r = (ior_t - ior_i) / sum;
    return r * r;
}

float TransmissionJacobian(float eta, float wo_dot_h, float wi_dot_h)
{
    float denom = eta * wo_dot_h + wi_dot_h;
    return abs(wi_dot_h) / max(denom * denom, 1e-6);
}

float PdfMicrofacetNormal(float alpha, float nh)
{
    return D_GTR2(max(alpha, 1e-4), nh) * nh;
}

// SampleBRDFResult SampleSpecularBRDF_Direction(float3 n, float3 wo, float alpha, float r1, float r2)
// {
//     SampleBRDFResult result = (SampleBRDFResult)0;
// #if defined(SAMPLE_BRDF_VNDF_GGX)
//     result.h = SampleGGX_VNDF(n, wo, alpha, r1, r2);
//     result.wi = reflect(-wo, result.h);
//     if (dot(result.wi, n) <= 0.0)
//     {
//         result.pdf = 0.0;
//         return result;
//     }
//     float nh = saturate(dot(n, result.h));
//     float vh = max(saturate(dot(wo, result.h)), 1e-4);
//     float nv = max(saturate(dot(n, wo)), 1e-4);
//     float D = D_GTR2(alpha, nh);
//     float G1 = SmithG1GGX(nv, alpha);
//     result.pdf = D * G1 * nh / (4.0 * vh * nv + 1e-6);
// #else
//     result.h = SampleGGX(n, alpha, r1, r2);
//     result.wi = reflect(-wo, result.h);
//     if (dot(result.wi, n) <= 0.0)
//     {
//         return result;
//     }
// #endif
//     return result;
// }

float3 EvalBRDF(TraceContext ctx,float3 n, float3 h,float3 wo, float3 wi,out float pdf)
{
    Material mat = ctx._mat;
    pdf = 1.0;

    if (mat._is_glass)
    {
        float alpha = RoughnessToAlpha(ctx._mat._roughness);
        float nv = abs(dot(n, wo));
        float nl = abs(dot(n, wi));
        if (nv <= 1e-6 || nl <= 1e-6)
            return 0.0.xxx;

        bool is_reflection = dot(n, wo) * dot(n, wi) > 0.0;
        float3 f0 = DielectricF0(ctx._ior_i, ctx._ior_t).xxx;

        // if (is_reflection)
        // {
        //     float3 h_reflect = normalize(wi + wo);
        //     if (dot(h_reflect, n) < 0.0)
        //         h_reflect = -h_reflect;

        //     float nh = saturate(dot(n, h_reflect));
        //     float vh = max(abs(dot(wo, h_reflect)), 1e-4);
        //     float D = D_GTR2(alpha, nh);
        //     float G = G_SmithGGX(nl, nv, alpha);
        //     float3 F = F_Schlick(f0, vh);

        //     pdf = PdfMicrofacetNormal(alpha, nh) / max(4.0 * vh, 1e-6);
        //     return D * G * F / max(4.0 * nl * nv, 1e-6);
        // }

        float3 h_transmit = normalize(wi * ctx._ior_t + wo * ctx._ior_i);
        if (any(isnan(h_transmit)) || all(h_transmit == 0.0.xxx))
            return 0.0.xxx;
        if (dot(h_transmit, n) < 0.0)
            h_transmit = -h_transmit;

        float nh = saturate(dot(n, h_transmit));
        float vh = max(abs(dot(wo, h_transmit)), 1e-4);
        float wi_h = dot(wi, h_transmit);
        float wo_h = dot(wo, h_transmit);
        float D = D_GTR2(alpha, nh);
        float G = G_SmithGGX(nl, nv, alpha);
        float3 F = F_Schlick(f0, vh);

        float denom = ctx._ior_i * wi_h + ctx._ior_t * wo_h;
        float denom2 = max(denom * denom, 1e-6);
        float factor = (ctx._ior_t * ctx._ior_t) / max(ctx._ior_i * ctx._ior_i, 1e-6);
        float dwh_dwi = abs((ctx._ior_t * ctx._ior_t * wi_h) / denom2);

        pdf = PdfMicrofacetNormal(alpha, nh) * dwh_dwi;
        return (1.0 - F) * D * G * factor *
            abs(wi_h) * abs(wo_h) /
            max(nl * nv * denom2, 1e-6) * 1.5;
    }

    float nv = saturate(dot(n, wo));
    float nl = saturate(dot(n, wi));
    float nh = saturate(dot(n, h));
    float vh = max(saturate(dot(wo, h)), 1e-4);
    if (nv <= 0.0 || nl <= 0.0)
        return 0.0.xxx;

    float alpha = RoughnessToAlpha(ctx._mat._roughness);
    float D = D_GTR2(alpha, nh);
    float V = V_SmithGGXCorrelated(nv, nl, alpha);
    float3 f0 = lerp(DIELECTRIC_SPECULAR.xxx, mat._albedo, mat._metallic.xxx);
    float3 F = F_Schlick(f0, vh);
    float3 f_specular = D * V * F;
    float3 f_diffuse = mat._albedo / PI;
    float3 kd = (1 - F) * (1 - mat._metallic);
    float specular_prob = ComputeBRDFSpecularSamplingProb(mat._albedo, mat._metallic);
    float diffuse_pdf = nl / PI;
    float specular_pdf = D * nh / (4.0 * vh + 1e-6);
#if defined(SAMPLE_BRDF_VNDF_GGX)
    float G1 = SmithG1GGX(max(nv, 1e-4), alpha);
    specular_pdf = D * G1 * nh / (4.0 * vh * max(nv, 1e-4) + 1e-6);
#endif
    pdf = lerp(diffuse_pdf, specular_pdf, specular_prob);
    return kd * f_diffuse + f_specular;
}

SampleBRDFResult SampleBRDF(TraceContext ctx,float3 n,float3 wo,float r_select,float r1,float r2)
{
    SampleBRDFResult result = (SampleBRDFResult)0;
    Material mat = ctx._mat;
    float specular_prob = ComputeBRDFSpecularSamplingProb(mat._albedo, mat._metallic);
    specular_prob = mat._is_glass ? 1.0 : specular_prob;

    if (mat._is_glass)
    {
        result.h = SampleGGX(n, RoughnessToAlpha(ctx._mat._roughness), r1, r2);
        float vh = max(abs(dot(wo, result.h)), 1e-4);
        float reflect_prob = F_Schlick(DielectricF0(ctx._ior_i, ctx._ior_t).xxx, vh).x;

        // if (r_select < reflect_prob)
        // {
        //     result.wi = reflect(-wo, result.h);
        //     return result;
        // }
        if (dot(result.h, n) < 0.0)
            result.h = -result.h;
        result.wi = refract(-wo, result.h, ctx._eta);
        if (all(result.wi == 0.0.xxx))
            result.wi = reflect(-wo, result.h);
        return result;
    }

    if (r_select < specular_prob)
    {
        result.h = SampleGGX(n, RoughnessToAlpha(ctx._mat._roughness), r1, r2);
        result.wi = reflect(-wo, result.h);
        return result;
    }

    result.wi = sample_cosine_hemisphere(n, r1, r2);
    result.h = normalize(result.wi + wo);
    return result;
}

#endif// __SAMPLING_HLSLI__