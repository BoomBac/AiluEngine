#ifndef AIL_GI_RAY_LIGHTING_HLSLI
#define AIL_GI_RAY_LIGHTING_HLSLI

#include "path_tracer_light_common.hlsli"

float3 GIReadMaterialAlbedo(MaterialData material, float2 uv)
{
    float3 albedo = material._base_color;
    if (valid_bindless_handle(material._base_color_tex))
    {
        if (material._material_id == AL_MATERIAL_ID_CHECKER)
        {
            const float checker = SAMPLE_TEXTURE2D_LOD(
                g_bindless_texture2d[material._base_color_tex], g_LinearWrapSampler, uv * 10.0f, 0.0f).b;
            return lerp(0.4f, 1.0f, checker).xxx;
        }
        albedo *= SAMPLE_TEXTURE2D_LOD(
            g_bindless_texture2d[material._base_color_tex], g_LinearClampSampler, uv, 0.0f).rgb;
    }
    return max(albedo, 0.0f.xxx);
}

float3 GIReadMaterialEmission(MaterialData material, float2 uv)
{
    float3 emission = material._emission;
    if (valid_bindless_handle(material._emission_tex))
    {
        emission = SAMPLE_TEXTURE2D_LOD(
            g_bindless_texture2d[material._emission_tex], g_LinearClampSampler, uv, 0.0f).rgb;
    }
    return max(emission * material._emission_strength, 0.0f.xxx);
}

float GIReadMaterialMetallic(MaterialData material, float2 uv)
{
    float metallic = material._metallic;
    if (valid_bindless_handle(material._metallic_tex))
        metallic *= SAMPLE_TEXTURE2D_LOD(
            g_bindless_texture2d[NonUniformResourceIndex(material._metallic_tex)], g_LinearClampSampler, uv, 0.0f).r;
    return saturate(metallic);
}

bool GIIsDirectLightOccluded(TraceSurface surface, uint light_index, LightSample light_sample, float hit_distance)
{
    if (!UnifiedLightCastsShadow(light_index))
        return false;

    const float shadow_t_max = GetLightShadowRayTMax(light_sample) * 0.98f;
    if (shadow_t_max <= 0.001f)
        return true;

    TraceRayDesc shadow_ray = (TraceRayDesc)0;
    shadow_ray._origin = OffsetRayOrigin(
        surface._world_position, surface._geometric_normal, light_sample._wi, hit_distance);
    shadow_ray._direction = light_sample._wi;
    shadow_ray._t_min = 0.001f;
    shadow_ray._t_max = shadow_t_max;
    shadow_ray._mask = 0xffu;
    return TraceAny(shadow_ray);
}

float3 GIEvaluateDirectLighting(TraceSurface surface, MaterialData material, uint sample_id_x, uint sample_id_y,
                               uint frame_index, uint unified_light_count, float hit_distance)
{
    if (unified_light_count == 0u)
        return 0.0f.xxx;

    RandomCtx random_ctx = (RandomCtx)0;
    InitSeed(random_ctx, uint2(sample_id_x, sample_id_y), frame_index);

    const float select_random = rand(random_ctx);
    const uint light_index = min((uint)(select_random * unified_light_count), unified_light_count - 1u);
    LightSample light_sample = SampleUnifiedLight(
        light_index, surface._world_position, surface._shading_normal, random_ctx);
    light_sample._pdf *= GetUnifiedLightSelectionPMF(unified_light_count);

    if (light_sample._pdf <= 1e-6f || !any(light_sample._radiance > 0.0f.xxx))
        return 0.0f.xxx;

    const float n_dot_l = saturate(dot(surface._shading_normal, light_sample._wi));
    if (n_dot_l <= 1e-6f)
        return 0.0f.xxx;

    if (GIIsDirectLightOccluded(surface, light_index, light_sample, hit_distance))
        return 0.0f.xxx;

    const float3 albedo = GIReadMaterialAlbedo(material, surface._uv);
    const float metallic = GIReadMaterialMetallic(material, surface._uv);
    const float3 diffuse_brdf = Diffuse_Lambert(albedo * (1.0f - metallic));
    return light_sample._radiance * diffuse_brdf * n_dot_l / light_sample._pdf;
}

float3 GIEvaluateHitRadiance(TraceSurface surface, MaterialData material, uint sample_id_x, uint sample_id_y,
                            uint frame_index, uint unified_light_count, float hit_distance)
{
    float3 radiance = GIReadMaterialEmission(material, surface._uv);
    radiance += GIEvaluateDirectLighting(surface, material, sample_id_x, sample_id_y, frame_index,
                                         unified_light_count, hit_distance);
    return max(radiance, 0.0f.xxx);
}

#endif // AIL_GI_RAY_LIGHTING_HLSLI
