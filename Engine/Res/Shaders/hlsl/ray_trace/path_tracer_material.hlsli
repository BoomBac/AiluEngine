#ifndef PATH_TRACER_MATERIAL_HLSLI
#define PATH_TRACER_MATERIAL_HLSLI

#include "rt_common.hlsli"
#include "../bindless.hlsli"
#include "../sampler.hlsli"

float4 SamplePathTexture2D(uint texture_handle, float2 uv)
{
    if (!valid_bindless_handle(texture_handle))
        return 1.0.xxxx;
    return SAMPLE_TEXTURE2D_LOD(g_bindless_texture2d[NonUniformResourceIndex(texture_handle)],
                                g_LinearClampSampler, uv, 0.0f);
}

bool IsPathMaterialTransmissive(MaterialData material_data)
{
    return material_data._transmission > 0.0f &&
           (valid_bindless_handle(material_data._metallic_tex) || material_data._metallic < 1.0f);
}

Material CreatePathMaterial(MaterialData material_data, float2 uv)
{
    Material material = (Material)0;
    float4 base_sample = SamplePathTexture2D(material_data._base_color_tex, uv);
    material._albedo = max(material_data._base_color * base_sample.rgb, 0.0f.xxx);
    material._opacity = (material_data._flags & AL_MATERIAL_FLAG_ALPHA_BLEND) != 0u
        ? saturate(material_data._opacity * base_sample.a) : 1.0f;

    material._roughness = clamp(
        material_data._roughness * SamplePathTexture2D(material_data._roughness_tex, uv).r, 0.0f, 1.0f);
    material._metallic = saturate(material_data._metallic * SamplePathTexture2D(material_data._metallic_tex, uv).r);

    material._anisotropy = material_data._anisotropy;
    material._ior = max(material_data._ior, 1.0001f);
    material._transmission = saturate(material_data._transmission);
    material._sigma_a = max(material_data._sigma_a, 0.0f.xxx);

    float3 emission = material_data._emission;
    if (valid_bindless_handle(material_data._emission_tex))
        emission = SamplePathTexture2D(material_data._emission_tex, uv).rgb;
    material._emission = max(emission * max(material_data._emission_strength, 0.0f), 0.0f.xxx);
    material._is_glass = material._transmission > 0.0f && material._metallic < 1.0f;
    material._is_smooth_dielectric = material._is_glass && material._roughness <= 1e-3f;
    return material;
}

TraceContext CreatePathTraceContext(Material material, bool front_face)
{
    TraceContext trace_context = (TraceContext)0;
    trace_context._mat = material;
    trace_context._ior_i = front_face ? 1.0f : material._ior;
    trace_context._ior_t = front_face ? material._ior : 1.0f;
    trace_context._eta = trace_context._ior_i / max(trace_context._ior_t, 1e-6f);
    trace_context._lobe = kBSDFLobeAnalytic;
    return trace_context;
}

bool HasContinuousPathBSDF(Material material)
{
    if (material._roughness > 1e-3f)
        return true;
    return material._metallic < 1.0f && (!material._is_glass || material._transmission < 1.0f);
}

// 介质内传播的 Beer-Lambert 衰减。
// absorption 是每单位长度的吸收系数 (1/世界单位)，distance 是这一段在介质里走的距离。
// absorption 为 0 时严格返回 1（exp(-0) == 1），所以无损介质的行为与改动前逐像素一致。
// 这是「厚度感」的来源：全反射陷阱里的光行程远大于直穿光，会自然衰减，
// 薄处亮、厚处暗；没有它，无损介质里被困住的光永远不会损失。
//
// 注意量级：系数是「每世界单位」，所以对直径 0.88 的球，0.01 只损失 0.88%，
// 要有明显厚度感需要 1~10 这个量级。范围不该由 UI 滑条限定。
float3 ApplyMediumAbsorption(float3 sigma_a, float distance)
{
    float3 a = max(sigma_a, 0.0.xxx);
    // 防御 NaN / Inf：材质属性来自用户输入，脏值会一路污染整条路径。
    // 这里必须用 select：对 float3 用三目运算符会被 dxc 判为
    // 「condition for short-circuiting ternary operator must be scalar」。
    a = select(a != a, 0.0.xxx, a);
    a = min(a, 1e6.xxx);
    float d = max(distance, 0.0f);
    d = d != d ? 0.0f : min(d, 1e6f);
    return exp(-a * d);
}

// 沿几何法线把射线起点推出当前表面，避免自相交。
//
// 关键：偏移量必须是「表面的局部尺度」，绝不能随 hit_t 按比例放大。
// 之前写成 min(..., hit_t * 0.01)，远处地面的偏移会变成几十上百个单位，
// 于是阴影/环境光射线从离目标很远的地方出发，远处区域被整片误判为遮挡（只有近处有光照）；
// IsPathOccluded 还会把这个虚高的偏移从 remaining_t 里减掉，进一步提前终止遍历。
//
// 掠射时射线在表面附近的穿行距离正比于 1/|n·d|，所以按这个因子放大；
// 同时用 1/64 世界单位的绝对上限兜底，保证偏移始终是小量。
static const float kPathOriginEpsilon = 1.0f / 65536.0f;

float3 OffsetPathRayOrigin(float3 position, float3 geometric_normal, float3 ray_direction, float hit_t)
{
    float cos_theta = max(abs(dot(ray_direction, geometric_normal)), 1e-3);
    float offset = min(kPathOriginEpsilon / cos_theta, 1.0f / 64.0f);
    // 新射线必须从出射侧开始，避免折射或掠射时再次命中刚离开的表面。
    return position + geometric_normal * (dot(ray_direction, geometric_normal) >= 0.0f ? offset : -offset);
}

#endif // PATH_TRACER_MATERIAL_HLSLI
