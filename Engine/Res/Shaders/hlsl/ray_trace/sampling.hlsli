#ifndef __SAMPLING_HLSLI__
#define __SAMPLING_HLSLI__
#include "../brdf.hlsli"
#include "../color_space_utils.hlsli"
#include "rt_common.hlsli"

struct RandomCtx
{
    uint4 seed;
    uint2 pixel;
};
void InitSeed(inout RandomCtx ctx,uint2 pixel, uint frame, uint bounce = 0)
{
    ctx.seed.x = pixel.x * 1973 + pixel.y * 9277 + frame * 26699 + bounce * 104729;
    ctx.pixel = pixel;
}

void pcg_hash(inout uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    seed = (word >> 22u) ^ word;
}

float rand(inout RandomCtx ctx)
{
    pcg_hash(ctx.seed.x);
    return float(ctx.seed.x) / 4294967296.0;
}

float2 rand2(inout RandomCtx ctx)
{
    return float2(rand(ctx), rand(ctx));
}

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
    // Compute both components independently; 1 - cos(theta)^2 loses small GGX slopes.
    float denominator = (1.0 - u1) + a2 * u1;
    float cosTheta = sqrt(max(0.0, (1.0 - u1) / denominator));
    float sinTheta = sqrt(max(0.0, a2 * u1 / denominator));

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


// The cross product retains small slopes when dot(n, h) rounds to one.
float D_PathGGX(float alpha, float3 n, float3 h)
{
    float a2 = alpha * alpha;
    float nh = dot(n, h);
    float3 tangent_h = cross(n, h);
    float denominator = dot(tangent_h, tangent_h) + a2 * nh * nh;
    return a2 / (PI * denominator * denominator);
}

float PdfGGX(float alpha, float dotNH, float dotVH)
{
    float nh = saturate(dotNH);
    float vh = max(saturate(dotVH), 1e-4);
    float D = D_GTR2(max(alpha, 1e-6), nh);
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
    float a = max(alpha, 1e-6);

    float D = D_GTR2(a, nh);

    float G1 = SmithG1GGX(nv, a);

    return D * G1 / (4.0 * nv);
}

// 返回采样方向 wi 和对应的 PDF
struct SampleBRDFResult
{
    float3 wi;      // 采样方向（世界空间，指向表面外）
    float3 _delta_weight;
    float _delta_pdf;
    bool _is_delta; // 只有真正的离散镜面方向才可跳过光源 MIS；GGX 玻璃不是 delta
    // 这一步是折射（穿透）还是反射。体积吸收据此判断介质归属。
    bool _is_transmission;
    // 采样端实际选中的瓣和采样到的微表面法线。
    // 求值端必须原样使用这两个值，不能自己从 (wo, wi) 反推：
    // 近镜面时反推出来的 h 与 n 近乎垂直，反射解和折射解在数值上无法区分，
    // 反推会把方向与 pdf 算错（甚至把方向判掉），玻璃因此系统性偏暗。
    uint _lobe;
    float3 _h;
};

float ComputeBRDFSpecularSamplingProb(float3 base_color, float metallic)
{
    float3 f0 = lerp(DIELECTRIC_SPECULAR.xxx, base_color, metallic.xxx);
    float diffuse_weight = Luminance(base_color * (1.0 - metallic));
    float specular_weight = Luminance(f0);
    float denom = max(diffuse_weight + specular_weight, 1e-5);
    return diffuse_weight <= 0.0f ? 1.0f : clamp(specular_weight / denom, 0.05, 0.95);
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
    float D = D_PathGGX(alpha, n, h);
    float G1 = SmithG1GGX(max(nv, 1e-4), max(alpha, 1e-6));
    return D * G1 * nh / (4.0 * vh * max(nv, 1e-4) + 1e-6);
#else
    float D = D_PathGGX(alpha, n, h);
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

float PdfMicrofacetNormal(float alpha, float3 n, float3 h)
{
    return D_PathGGX(alpha, n, h) * saturate(abs(dot(n, h)));
}

float3 BuildReflectionHalfVector(float3 wo, float3 wi)
{
    float3 h = wo + wi;
    float len2 = dot(h, h);
    return len2 > 1e-12 ? h * rsqrt(len2) : 0.0.xxx;
}

// 折射瓣使用的半程向量。约定：wo 指回观察侧、wi 是出射方向，两者都朝表面之外。
//
// 对称配方 h = normalize(wi + wo) 对折射瓣不成立（约束两侧系数不同）。
// 正确解是系数交叉的加权和：
//   h = normalize(wi * ior_t + wo * ior_i)        （再做一次定向，使 dot(h, wo) > 0）
// 与折射采样端使用同一个微表面法线；后续 Jacobian 的分母必须按这两个 IOR 的系数构造。
//
// 注意两个容易踩的坑：
//   1) 系数不能写成 wi*ior_i + wo*ior_t（那是差值形式的等价物，大角度下完全错）；
//   2) 分子是「和」不是「差」——折射角小时 wi 与 wo 近乎反向，差值会灾难性相消。
// 该构造在透明（ior_i < ior_t）与出射（ior_i > ior_t）两种情况下都成立。
float3 BuildTransmissionHalfVector(float3 wo, float3 wi, float ior_i, float ior_t)
{
    float3 h = wi * ior_t + wo * ior_i;
    float len2 = dot(h, h);
    if (len2 <= 1e-12)
        return 0.0.xxx;
    h *= rsqrt(len2);
    return dot(h, wo) < 0.0 ? -h : h;
}

float FresnelDielectric(float cos_i, float ior_i, float ior_t)
{
    cos_i = saturate(abs(cos_i));
    const float eta = ior_i / max(ior_t, 1e-6f);
    const float sin_t2 = eta * eta * max(0.0f, 1.0f - cos_i * cos_i);
    if (sin_t2 >= 1.0f)
        return 1.0f;

    const float cos_t = sqrt(max(0.0f, 1.0f - sin_t2));
    const float rs = (ior_i * cos_i - ior_t * cos_t) / max(ior_i * cos_i + ior_t * cos_t, 1e-6f);
    const float rp = (ior_t * cos_i - ior_i * cos_t) / max(ior_t * cos_i + ior_i * cos_t, 1e-6f);
    return saturate(0.5f * (rs * rs + rp * rp));
}

// 采到该半程向量后选择反射（而非折射）的概率。采样与求值必须共用同一个函数，
// 否则 BSDF 采样分布和 NEE 的 pdf 对不上。
float GlassReflectionSelectProb(TraceContext ctx, float3 wo, float3 h)
{
    return FresnelDielectric(dot(wo, h), ctx._ior_i, ctx._ior_t);
}

// 反射瓣：f = D * G * F / (4 |n·wo| |n·wi|)，pdf = D * nh / (4 |wo·h|)
//
// pdf 里那个 1/(4|wo·h|) 不能省：它是「半程向量分布 -> 出射方向分布」的 Jacobian。
// 少了它 pdf 会偏大 4 倍，反射能量被压到 1/4（法线入射实测 0.013 而非 0.052）。
float3 EvalGlassReflectionLobe(float3 n, float3 wo, float3 wi, float3 h,
                               float alpha, float fresnel, out float pdf)
{
    pdf = 0.0;
    if (all(h == 0.0.xxx))
        return 0.0.xxx;

    float nl = abs(dot(n, wi));
    float nv = abs(dot(n, wo));
    float vh = abs(dot(wo, h));
    if (nl <= 1e-6 || nv <= 1e-6 || vh <= 1e-6)
        return 0.0.xxx;

    float nh = saturate(abs(dot(n, h)));
    float D = D_PathGGX(alpha, n, h);
    float G = 4.0f * nl * nv * V_SmithGGXCorrelated(nv, nl, alpha);
    pdf = PdfMicrofacetNormal(alpha, n, h) / max(4.0 * vh, 1e-6);
    return D * G * fresnel / max(4.0 * nl * nv, 1e-6);
}

// Walter 的粗糙介质 BTDF，按 radiance transport 加上 (ior_i/ior_t)^2。
// eta = ior_t/ior_i；denom = wi·h + wo·h/eta，与 h = normalize(wo*ior_i + wi*ior_t) 一致。
float3 EvalGlassTransmissionLobe(float3 n, float3 wo, float3 wi, float3 h,
                                 float alpha, float fresnel, float ior_i, float ior_t,
                                 out float pdf)
{
    pdf = 0.0;
    if (all(h == 0.0.xxx))
        return 0.0.xxx;

    float nl = abs(dot(n, wi));
    float nv = abs(dot(n, wo));
    float vh = abs(dot(wo, h));
    if (nl <= 1e-6 || nv <= 1e-6 || vh <= 1e-6)
        return 0.0.xxx;

    float nh = saturate(abs(dot(n, h)));
    float wi_h = dot(wi, h);
    float wo_h = dot(wo, h);

    float eta = ior_t / max(ior_i, 1e-6f);
    float denom = wi_h + wo_h / eta;
    if (abs(denom) <= 1e-6)
        return 0.0.xxx;

    float D = D_PathGGX(alpha, n, h);
    float G = 4.0f * nl * nv * V_SmithGGXCorrelated(nv, nl, alpha);
    float denom2 = denom * denom;
    pdf = PdfMicrofacetNormal(alpha, n, h) * abs(wi_h) / denom2;
    return (1.0f - fresnel) * D * G * abs(wi_h * wo_h) /
           (max(nl * nv, 1e-6f) * denom2 * eta * eta);
}

// 不透明表面的 BRDF（GGX 高光 + Lambert 漫反射）。抽出来是为了让玻璃分支在
// `_transmission < 1` 时能把「非透射的那部分」按不透明表面着色。
float3 EvalOpaqueBRDF(TraceContext ctx, float3 n, float3 wo, float3 wi, out float pdf)
{
    Material mat = ctx._mat;
    pdf = 0.0;

    float3 h = !mat._is_glass && ctx._lobe == kBSDFLobeReflection && any(ctx._sampled_h != 0.0.xxx)
        ? ctx._sampled_h : BuildReflectionHalfVector(wo, wi);
    if (all(h == 0.0.xxx))
        return 0.0.xxx;

    float nv = saturate(dot(n, wo));
    float nl = saturate(dot(n, wi));
    float nh = saturate(dot(n, h));
    float vh = max(saturate(dot(wo, h)), 1e-4);
    if (nv <= 0.0 || nl <= 0.0)
        return 0.0.xxx;

    float alpha = RoughnessToAlpha(mat._roughness);
    float D = mat._roughness <= 1e-3f ? 0.0f : D_PathGGX(alpha, n, h);
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

// 求值单个玻璃瓣。采样端与求值端共用这一份公式，任何一侧改了都必须同步。
//   reflect_prob：采样端选反射瓣的概率；只乘进 pdf，不能再乘进已包含 Fresnel 的 BSDF。
//   is_reflection：本次求值算的是反射瓣还是折射瓣。
float3 EvalGlassLobe(TraceContext ctx, float3 n, float3 wo, float3 wi, float3 h,
                     bool is_reflection, float reflect_prob, out float pdf)
{
    const float alpha = RoughnessToAlpha(ctx._mat._roughness);
    if (is_reflection)
    {
        float3 f = EvalGlassReflectionLobe(n, wo, wi, h, alpha, reflect_prob, pdf);
        pdf *= reflect_prob;
        return f;
    }
    // 接近全反射时 p_R 趋近 1，折射瓣的 pdf 恰好趋近 0，采样端也永远不会选到它
    float3 f = EvalGlassTransmissionLobe(n, wo, wi, h, alpha, reflect_prob,
                                          ctx._ior_i, ctx._ior_t, pdf);
    pdf *= 1.0 - reflect_prob;
    return f;
}

// 完整的粗糙玻璃 BSDF：反射 + 折射，按 Fresnel 分瓣，
// 再按 `_transmission` 与不透明表面着色混合：
//   f = transmission * (f_R + f_T) + (1 - transmission) * f_opaque
// transmission = 1 时退化为纯玻璃，= 0 时退化为标准 PBR。
//
// BSDF 采样路径必须把「采样端选中的瓣 + 采样到的半程向量」传进来（lobe / sampled_h）。
// 不要试图从 (wo, wi) 反推 h 来判断瓣：近镜面玻璃下 h 与 n 近乎垂直，
// 反射解和折射解的 dot(n, h) 都趋近于 0、符号由浮点噪声决定，反推必然出错。
float3 EvalGlassBSDFAnalytic(TraceContext ctx, float3 n, float3 wo, float3 wi, out float pdf);

float3 EvalGlassBSDF(TraceContext ctx, float3 n, float3 wo, float3 wi,
                     uint lobe, float3 sampled_h, out float pdf)
{
    pdf = 0.0;

    const float tr = saturate(ctx._mat._transmission);
    const float opaque_weight = 1.0 - tr;

    if (lobe == kBSDFLobeOpaque)
        return EvalGlassBSDFAnalytic(ctx, n, wo, wi, pdf);

    float pdf_opaque = 0.0;
    float3 f_opaque = opaque_weight > 0.0
        ? opaque_weight * EvalOpaqueBRDF(ctx, n, wo, wi, pdf_opaque)
        : 0.0.xxx;

    // 选瓣概率必须和采样端 SampleBRDF 完全一致：都是基于采样到的那个半程向量
    const float reflect_prob = GlassReflectionSelectProb(ctx, wo, sampled_h);

    float pdf_lobe = 0.0;
    float3 f_lobe = EvalGlassLobe(ctx, n, wo, wi, sampled_h,
                                  lobe == kBSDFLobeReflection, reflect_prob, pdf_lobe);

    pdf = tr * pdf_lobe + opaque_weight * pdf_opaque;
    return tr * f_lobe + f_opaque;
}

// 任意方向求值（NEE / MIS）。
// 这里没有采样端上下文，只能由 (wo, wi) 解析反推对应瓣的半程向量。
// 近镜面时反推的权重不可靠，但这条路径只用于光源采样方向的 MIS 权重，
// 而近镜面玻璃采到的是 delta 瓣（不做光源 MIS），影响可以忽略。
float3 EvalGlassBSDFAnalytic(TraceContext ctx, float3 n, float3 wo, float3 wi, out float pdf)
{
    pdf = 0.0;

    const float tr = saturate(ctx._mat._transmission);
    const float opaque_weight = 1.0 - tr;
    const float alpha = RoughnessToAlpha(ctx._mat._roughness);
    float pdf_opaque = 0.0;
    float3 f_opaque = opaque_weight > 0.0
        ? opaque_weight * EvalOpaqueBRDF(ctx, n, wo, wi, pdf_opaque)
        : 0.0.xxx;

    if (ctx._mat._is_smooth_dielectric)
    {
        pdf = opaque_weight * pdf_opaque;
        return f_opaque;
    }

    float3 h_reflect = BuildReflectionHalfVector(wo, wi);
    if (dot(h_reflect, wo) < 0.0)
        h_reflect = -h_reflect;

    const bool is_reflection = dot(n, wo) * dot(n, wi) > 0.0f;
    float3 h_transmit = BuildTransmissionHalfVector(wo, wi, ctx._ior_i, ctx._ior_t);
    if (all(h_transmit == 0.0.xxx))
    {
        if (!is_reflection)
        {
            pdf = opaque_weight * pdf_opaque;
            return f_opaque;
        }
        float pdf_reflect_only = 0.0;
        float reflect_only_prob = GlassReflectionSelectProb(ctx, wo, h_reflect);
        float3 f_only = EvalGlassReflectionLobe(n, wo, wi, h_reflect, alpha, reflect_only_prob,
                                                 pdf_reflect_only);
        pdf = tr * reflect_only_prob * pdf_reflect_only + opaque_weight * pdf_opaque;
        return tr * f_only + f_opaque;
    }

    const float reflect_prob = GlassReflectionSelectProb(ctx, wo, h_reflect);
    const float transmit_prob = 1.0f - GlassReflectionSelectProb(ctx, wo, h_transmit);

    float pdf_reflect = 0.0;
    float pdf_transmit = 0.0;
    float3 f_reflect = is_reflection && reflect_prob > 0.0f
        ? EvalGlassReflectionLobe(n, wo, wi, h_reflect, alpha, reflect_prob, pdf_reflect)
        : 0.0.xxx;
    float3 f_transmit = !is_reflection && transmit_prob > 0.0f
        ? EvalGlassTransmissionLobe(n, wo, wi, h_transmit, alpha, 1.0f - transmit_prob,
                                    ctx._ior_i, ctx._ior_t, pdf_transmit)
        : 0.0.xxx;

    // 反射/折射方向分属法线两侧；选瓣概率只进入对应方向的 pdf。
    pdf = tr * (reflect_prob * pdf_reflect + transmit_prob * pdf_transmit) + opaque_weight * pdf_opaque;
    return tr * (f_reflect + f_transmit) + f_opaque;
}

float PdfAreaFromSolidAngle(float pdf_omega, float dist2, float cos_theta_l)
{
    if (pdf_omega <= 0.0 || dist2 <= 1e-6 || cos_theta_l <= 0.0)
        return 0.0;
    return pdf_omega * cos_theta_l / dist2;
}

float PdfSolidAngleFromArea(float pdf_area, float dist2, float cos_theta_l)
{
    if (pdf_area <= 0.0 || dist2 <= 1e-6 || cos_theta_l <= 0.0)
        return 0.0;
    return pdf_area * dist2 / cos_theta_l;
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
//     float D = D_PathGGX(alpha, n, h);
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

// lobe / sampled_h 只在「求值的是刚刚采样出来的方向」时有效；
// 求值任意方向（NEE、MIS）时传 kBSDFLobeAnalytic，函数内部按解析反推处理。
static const uint kBSDFLobeAnalytic = 2u;

float3 EvalBRDF(TraceContext ctx,float3 n,float3 wo, float3 wi,out float pdf)
{
    Material mat = ctx._mat;
    pdf = 1.0;

    if (mat._is_glass)
    {
        // 微表面法线由 lobe 决定：反射 h = normalize(wi + wo)，折射 h = normalize(wi * ior_t + wo * ior_i)。
        // 求值端绝不能只按折射反推 h，否则反射方向的返回值与 pdf 都是错的。
        if (ctx._lobe == kBSDFLobeAnalytic)
            return EvalGlassBSDFAnalytic(ctx, n, wo, wi, pdf);
        return EvalGlassBSDF(ctx, n, wo, wi, ctx._lobe, ctx._sampled_h, pdf);
    }

    return EvalOpaqueBRDF(ctx, n, wo, wi, pdf);
}

// 采样端用它来「先采半程向量，再按 Fresnel 决定这一票是反射还是折射」。
// 这与 EvalGlassLobe 的混合 pdf 是同一套权重，因此估计量无偏。
bool SampleGlassLobeDirection(TraceContext ctx, float3 n, float3 wo, float r1, float r2, float r_lobe,
                              out uint lobe, out float3 h, out float3 wi)
{
    lobe = kBSDFLobeReflection;
    h = SampleGGX(n, RoughnessToAlpha(ctx._mat._roughness), r1, r2);
    if (dot(h, n) < 0.0)
        h = -h;

    // 微表面法线上的 Fresnel 反射概率，与 EvalGlassLobe 用的是同一个函数
    const float reflect_prob = GlassReflectionSelectProb(ctx, wo, h);

    // 反射方向必须留在入射侧。
    // 判据不能写成 dot(wi, n) > 0：介质内部命中时法线已经被翻到外侧，
    // 此时出射方向与法线点乘正是负的，那样写会把内部所有反射样本全部丢掉。
    const float wo_side = dot(wo, n);

    // 选瓣使用独立随机数，不能复用采样 h 的 r1/r2。
    if (r_lobe < reflect_prob)
    {
        wi = reflect(-wo, h);
        return dot(wi, n) * wo_side > 0.0;
    }

    // 折射。全反射时 refract 返回 0，此时正确做法是回退到反射解（全反射本来就是反射），
    // 而不是丢掉这条路径。
    const float3 refracted = refract(-wo, h, ctx._eta);
    if (dot(refracted, refracted) < 1e-12)
    {
        wi = reflect(-wo, h);
        return dot(wi, n) * wo_side > 0.0;
    }

    lobe = kBSDFLobeTransmission;
    wi = refracted;
    return true;
}

SampleBRDFResult SampleOpaqueBRDF(TraceContext ctx, float3 n, float3 wo, float r_select, float r1, float r2)
{
    SampleBRDFResult result = (SampleBRDFResult)0;
    const Material mat = ctx._mat;
    result._lobe = mat._is_glass ? kBSDFLobeOpaque : kBSDFLobeAnalytic;
    const float specular_prob = ComputeBRDFSpecularSamplingProb(mat._albedo, mat._metallic);
    if (r_select < specular_prob)
    {
        if (mat._roughness <= 1e-3f)
        {
            result._h = n;
            result.wi = normalize(reflect(-wo, n));
            result._is_delta = true;
            float3 f0 = lerp(DIELECTRIC_SPECULAR.xxx, mat._albedo, mat._metallic.xxx);
            result._delta_weight = F_Schlick(f0, saturate(dot(n, wo))) / specular_prob;
            result._delta_pdf = (mat._is_glass ? 1.0f - saturate(mat._transmission) : 1.0f) * specular_prob;
            return result;
        }
#if defined(SAMPLE_BRDF_VNDF_GGX)
        result._h = SampleGGX_VNDF(n, wo, RoughnessToAlpha(mat._roughness), r1, r2);
#else
        result._h = SampleGGX(n, RoughnessToAlpha(mat._roughness), r1, r2);
#endif
        result.wi = reflect(-wo, result._h);
        if (!mat._is_glass)
            result._lobe = kBSDFLobeReflection;
    }
    else
        result.wi = sample_cosine_hemisphere(n, r1, r2);
    return result;
}

// 采样一个出射方向。四个随机数严格分工，不复用：
//   r_select：在「不透明瓣 / 玻璃瓣」之间二选一（权重 1-tr / tr）
//   r1, r2  ：用于采样微表面法线（或余弦半球）
//   r_lobe  ：在玻璃内部选反射/折射，或在不透明部分选高光/漫反射
// 复用采样方向的随机数选瓣会让两者相关，估计量有偏。
SampleBRDFResult SampleBRDF(TraceContext ctx, float3 n, float3 wo, float r_select, float r1, float r2,
                            float r_lobe)
{
    SampleBRDFResult result = (SampleBRDFResult)0;
    const Material mat = ctx._mat;

    if (mat._is_glass)
    {
        // 先按 `_transmission` 决定这一票是「玻璃」还是「不透明表面」。
        // 采样分布必须和 EvalGlassBSDF 里的混合 pdf 完全一致，否则估计量有偏。
        const float tr = saturate(mat._transmission);
        if (r_select >= tr)
            return SampleOpaqueBRDF(ctx, n, wo, r_lobe, r1, r2);

        result._lobe = kBSDFLobeReflection;
        if (!SampleGlassLobeDirection(ctx, n, wo, r1, r2, r_lobe, result._lobe, result._h, result.wi))
        {
            result.wi = 0.0.xxx;
            return result;
        }
        // 粗糙度虽低，采样仍来自有限宽度的 GGX 分布，不能作为 delta 跳过 MIS。
        result._is_delta = false;
        result._is_transmission = result._lobe == kBSDFLobeTransmission;
        return result;
    }

    return SampleOpaqueBRDF(ctx, n, wo, r_select, r1, r2);
}

float PointLightDistanceAttenuation(float3 light_pos, float light_range, float3 x)
{
    float atten_radius = max(light_range, 1e-6);
    float distance_to_center = distance(light_pos, x);
    float inv_sqr_atten = atten_radius * atten_radius / (distance_to_center * distance_to_center + 1.0);
    float window_func = Pow2(saturate(1.0 - pow(distance_to_center / atten_radius, 4.0)));
    return inv_sqr_atten * window_func;
}

float PointLightSolidAngle(float3 light_pos, float light_radius, float3 x)
{
    float3 to_center = light_pos - x;
    float dist2 = dot(to_center, to_center);
    float radius = max(light_radius, 1e-6);
    if (dist2 <= radius * radius)
        return 4.0 * PI;

    float sin_theta_max2 = saturate(radius * radius / dist2);
    float cos_theta_max = sqrt(max(0.0, 1.0 - sin_theta_max2));
    return 2.0 * PI * (1.0 - cos_theta_max);
}

float SpotLightAngleAttenuation(float3 light_pos, float3 light_dir, float light_angle_scale, float light_angle_offset, float3 x)
{
    float3 light_to_surface = normalize(x - light_pos);
    float cd = dot(normalize(light_dir), light_to_surface);
    float attenuation = saturate(cd * light_angle_scale + light_angle_offset);
    return attenuation * attenuation;
}

LightSample SamplePointLight(SampleLightData light, float3 x, float2 u)
{
    LightSample result = (LightSample)0;
    float3 center = light._position;
    float radius = light._radius_or_sun_angle;

    float3 wc = center - x;
    float dist2 = dot(wc, wc);
    float dist = sqrt(dist2);

    // inside sphere fallback
    if (dist <= radius)
    {
        float z = 1.0 - 2.0 * u.x;
        float xy = sqrt(max(0.0, 1.0 - z * z));
        float phi = 2.0 * PI * u.y;

        // Keep point-light PDFs in solid-angle measure for MIS.
        float3 wi = float3(xy * cos(phi), xy * sin(phi), z);
        float3 oc = x - center;
        float b = dot(wi, oc);
        float c = dot(oc, oc) - radius * radius;
        float h = b * b - c;
        if (h < 0.0)
            return result;

        float t = -b + sqrt(h);
        if (t <= 1e-6)
            return result;

        float3 y = x + t * wi;
        float3 light_normal = normalize(y - center);

        result._pdf = 1.0 / (4.0 * PI);
        result._wi = wi;
        result._t = t;
        result._normal = light_normal;
        result._position = y;
        result._pdf_area = result._pdf * abs(dot(light_normal, -wi)) / max(t * t, 1e-6f);
        float irradiance = PointLightDistanceAttenuation(light._position, light._range, x);
        float solid_angle = PointLightSolidAngle(light._position, light._radius_or_sun_angle, x);
        result._radiance = light._color * irradiance / max(solid_angle, 1e-6);
        return result;
    }

    // ===== solid angle sampling =====

    float3 w = wc / dist;
    float sin_theta_max2 = radius * radius / dist2;
    float cos_theta_max = sqrt(max(0.0, 1.0 - sin_theta_max2));

    float cos_theta = 1.0 - u.x * (1.0 - cos_theta_max);
    float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
    float phi = 2.0 * PI * u.y;
    float3 local_dir = float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);

    float3 up = abs(w.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, w));
    float3 bitangent = cross(w, tangent);
    float3 wi = normalize(local_dir.x * tangent + local_dir.y * bitangent + local_dir.z * w);

    float3 oc = x - center;
    float b = dot(wi, oc);
    float c = dot(oc, oc) - radius * radius;
    float h = b * b - c;
    if (h < 0.0)
        return result;

    float t = -b - sqrt(h);
    float3 y = x + t * wi;
    float3 light_normal = normalize(y - center);
    float cos_theta_l = dot(light_normal, -wi);
    if (cos_theta_l <= 0.0)
        return result;

    result._pdf = 1.0 / (2.0 * PI * (1.0 - cos_theta_max));
    result._wi = wi;
    result._t = t;
    result._normal = light_normal;
    result._position = y;
    result._pdf_area = result._pdf * cos_theta_l / max(t * t, 1e-6f);
    float irradiance = PointLightDistanceAttenuation(light._position, light._range, x);
    float solid_angle = PointLightSolidAngle(light._position, light._radius_or_sun_angle, x);
    result._radiance = light._color * irradiance / max(solid_angle, 1e-6);
    return result;
}

LightSample SampleSpotLight(SampleLightData light, float3 x, float2 u)
{
    LightSample result = SamplePointLight(light, x, u);
    if (result._pdf <= 0.0)
        return result;

    float attenuation = SpotLightAngleAttenuation(
        light._position, light._direction, light._light_u.x, light._light_u.y, x);
    if (attenuation <= 0.0)
    {
        result._radiance = 0.0.xxx;
        return result;
    }

    result._radiance *= attenuation;
    return result;
}

//返回指向光源的入射方向wi，pdf为该方向的概率密度
float DirectionalLightAngularRadius(SampleLightData light)
{
    return max(light._radius_or_sun_angle, 1e-4f) * (PI / 180.0f);
}

float DirectionalLightOneMinusCos(SampleLightData light)
{
    float sine = sin(0.5f * DirectionalLightAngularRadius(light));
    return 2.0f * sine * sine;
}

float DirectionalLightSolidAngle(SampleLightData light)
{
    return 2.0f * PI * DirectionalLightOneMinusCos(light);
}

LightSample SampleLight(SampleLightData light, float3 x, float3 n, inout RandomCtx ctx)
{
    float2 u = rand2(ctx);
    LightSample result = (LightSample)0;
    if (light._type == LIGHT_TYPE_DIRECTIONAL)
    {
        float3 light_dir = normalize(light._direction);
        float one_minus_cos_theta = u.x * DirectionalLightOneMinusCos(light);
        float cos_theta = 1.0f - one_minus_cos_theta;
        float sin_theta = sqrt(max(0.0f, one_minus_cos_theta * (2.0f - one_minus_cos_theta)));
        float phi = 2.0 * PI * u.y;

        float3 tangent;
        float3 bitangent;
        build_onb(light_dir, tangent, bitangent);

        float3 wi = normalize(
            cos(phi) * sin_theta * tangent +
            sin(phi) * sin_theta * bitangent +
            cos_theta * light_dir);

        float sun_solid_angle = DirectionalLightSolidAngle(light);
        result._pdf = 1.0f / sun_solid_angle;
        result._wi = wi;
        result._t = 1e6; // directional light 没有具体位置，设置一个很大的距离
        result._radiance = light._color / sun_solid_angle;
        return result;
    }
    else if (light._type == LIGHT_TYPE_RECT_AREA)
    {
        float3 light_u = light._light_u;
        float3 light_v = light._light_v;
        float3 light_normal = -normalize(cross(light_u, light_v));
        u -= 0.5;
        float3 y = light._position + u.x * light_u + u.y * light_v;
        float3 wi = y - x;
        float r2 = dot(wi, wi);
        float r = sqrt(r2);
        wi = normalize(wi);
        float cos_theta_l = dot(light_normal, -wi);
        float area = length(cross(light_u, light_v));
        result._pdf_area = rcp(max(area, 1e-6f));
        result._position = y;
        result._normal = light_normal;
        result._radiance = light._color;
        if (light._is_double_sided)
            cos_theta_l = abs(cos_theta_l);
        if (cos_theta_l <= 0)
            return result;

        result._pdf = PdfSolidAngleFromArea(result._pdf_area, r2, cos_theta_l);
        result._wi = wi;
        result._t = r;
        return result;
    }
    else if (light._type == LIGHT_TYPE_SPOT)
    {
        return SampleSpotLight(light, x, u);
    }
    else if (light._type == LIGHT_TYPE_POINT)
    {
        return SamplePointLight(light, x, u);
    }
    return result;
}

LightSample SampleLight(SampleLightData light, float3 x, float3 n, float2 u)
{
    LightSample result = (LightSample)0;
    if (light._type == LIGHT_TYPE_DIRECTIONAL)
    {
        float3 light_dir = normalize(light._direction);
        float one_minus_cos_theta = u.x * DirectionalLightOneMinusCos(light);
        float cos_theta = 1.0f - one_minus_cos_theta;
        float sin_theta = sqrt(max(0.0f, one_minus_cos_theta * (2.0f - one_minus_cos_theta)));
        float phi = 2.0 * PI * u.y;

        float3 tangent;
        float3 bitangent;
        build_onb(light_dir, tangent, bitangent);

        float3 wi = normalize(
            cos(phi) * sin_theta * tangent +
            sin(phi) * sin_theta * bitangent +
            cos_theta * light_dir);

        float sun_solid_angle = DirectionalLightSolidAngle(light);
        result._pdf = 1.0f / sun_solid_angle;
        result._wi = wi;
        result._t = 1e6; // directional light 没有具体位置，设置一个很大的距离
        result._radiance = light._color / sun_solid_angle;
        return result;
    }
    else if (light._type == LIGHT_TYPE_RECT_AREA)
    {
        float3 light_u = light._light_u;
        float3 light_v = light._light_v;
        float3 light_normal = -normalize(cross(light_u, light_v));
        u -= 0.5;
        float3 y = light._position + u.x * light_u + u.y * light_v;
        float3 wi = y - x;
        float r2 = dot(wi, wi);
        float r = sqrt(r2);
        wi = normalize(wi);
        float cos_theta_l = dot(light_normal, -wi);
        float area = length(cross(light_u, light_v));
        result._pdf_area = rcp(max(area, 1e-6f));
        result._position = y;
        result._normal = light_normal;
        result._radiance = light._color;
        if (light._is_double_sided)
            cos_theta_l = abs(cos_theta_l);
        if (cos_theta_l <= 0)
            return result;

        result._pdf = PdfSolidAngleFromArea(result._pdf_area, r2, cos_theta_l);
        result._wi = wi;
        result._t = r;
        return result;
    }
    else if (light._type == LIGHT_TYPE_SPOT)
    {
        return SampleSpotLight(light, x, u);
    }
    else if (light._type == LIGHT_TYPE_POINT)
    {
        return SamplePointLight(light, x, u);
    }
    return result;
}

#endif// __SAMPLING_HLSLI__
