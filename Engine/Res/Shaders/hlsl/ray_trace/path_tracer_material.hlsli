#ifndef PATH_TRACER_MATERIAL_HLSLI
#define PATH_TRACER_MATERIAL_HLSLI

#include "rt_common.hlsli"
#include "../bindless.hlsli"
#include "../sampler.hlsli"

float4 SamplePathTexture2D(uint texture_handle, float2 uv)
{
    if (!valid_bindless_handle(texture_handle))
        return 1.0.xxxx;
    return SAMPLE_TEXTURE2D_LOD(g_bindless_texture2d[texture_handle], g_LinearClampSampler, uv, 0.0f);
}

bool IsPathMaterialTransmissive(MaterialData material_data)
{
    return material_data._transmission > 0.0f && material_data._metallic < 1.0f;
}

Material CreatePathMaterial(MaterialData material_data, float2 uv)
{
    Material material = (Material)0;
    material._albedo = max(material_data._base_color * SamplePathTexture2D(material_data._base_color_tex, uv).rgb,
                           0.0f.xxx);

    material._roughness = max(material_data._roughness, 0.04f);
    material._metallic = saturate(material_data._metallic);
    if (valid_bindless_handle(material_data._metallic_roughness_tex))
    {
        float2 roughness_metallic = SamplePathTexture2D(material_data._metallic_roughness_tex, uv).rg;
        material._roughness = max(roughness_metallic.r, 0.04f);
        material._metallic = saturate(roughness_metallic.g);
    }

    material._anisotropy = material_data._anisotropy;
    material._ior = max(material_data._ior, 1.0001f);
    material._transmission = saturate(material_data._transmission);

    float3 emission = material_data._emission;
    if (valid_bindless_handle(material_data._emission_tex))
        emission = SamplePathTexture2D(material_data._emission_tex, uv).rgb;
    material._emission = max(emission * max(material_data._emission_strength, 0.0f), 0.0f.xxx);
    material._is_glass = IsPathMaterialTransmissive(material_data);
    return material;
}

TraceContext CreatePathTraceContext(Material material, bool front_face)
{
    TraceContext trace_context = (TraceContext)0;
    trace_context._mat = material;
    trace_context._ior_i = front_face ? 1.0f : material._ior;
    trace_context._ior_t = front_face ? material._ior : 1.0f;
    trace_context._eta = trace_context._ior_i / max(trace_context._ior_t, 1e-6f);
    return trace_context;
}

float3 OffsetPathRayOrigin(float3 position, float3 geometric_normal, float3 ray_direction, float hit_t)
{
    float offset = max(1e-4f, hit_t * 1e-4f);
    return position + (dot(ray_direction, geometric_normal) > 0.0f ? geometric_normal : -geometric_normal) * offset;
}

#endif // PATH_TRACER_MATERIAL_HLSLI
