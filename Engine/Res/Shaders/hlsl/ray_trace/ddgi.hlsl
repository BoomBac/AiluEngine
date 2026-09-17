#pragma kernel ProbeTrace
#pragma multi_compile ProbeTrace AL_RAY_QUERY_DXR_INLINE

#include "ddgi_common.hlsli"
#include "ray_surface.hlsli"
#include "path_tracer_light_common.hlsli"

StructuredBuffer<MaterialData> g_material_buffer;
StructuredBuffer<DDGIProbeData> _probe_data;
RWStructuredBuffer<DDGIProbeRayData> _probe_ray_data;

CBUFFER_START(ComputeCB)
    float4x4 _volume_local_to_world;
    float4 _probe_spacing_and_distance;
    uint _probe_count_x;
    uint _probe_count_y;
    uint _probe_count_z;
    uint _rays_per_probe;
    uint _frame_index;
    float4 _environment_radiance_intensity;
    uint _freeze_ray_rotation;
    uint _unified_light_count;
    uint _use_probe_relocation;
    uint _ddgi_padding;
CBUFFER_END

float3 DDGIReadMaterialAlbedo(MaterialData material, float2 uv)
{
    float3 albedo = material._base_color;
    if (valid_bindless_handle(material._base_color_tex))
    {
        if (material._material_id == AL_MATERIAL_ID_CHECKER)
        {
            // The checker material binds a channel packed mask (R = dither noise, G/B = checker, A = grid)
            // instead of a color map, and derives its albedo from the mask in InitSurfaceDataCheckboard.
            // Sampling the mask as a color would turn it into red/cyan albedo.  Mirror the checker term
            // only (uv scale 10, wrapped); the raster camera fade term is meaningless for a probe ray.
            const float checker = SAMPLE_TEXTURE2D_LOD(
                g_bindless_texture2d[material._base_color_tex], g_LinearWrapSampler, uv * 10.0f, 0.0f).b;
            return lerp(0.4f, 1.0f, checker).xxx;
        }
        albedo *= SAMPLE_TEXTURE2D_LOD(
            g_bindless_texture2d[material._base_color_tex], g_LinearClampSampler, uv, 0.0f).rgb;
    }
    return max(albedo, 0.0f.xxx);
}

float3 DDGIReadMaterialEmission(MaterialData material, float2 uv)
{
    float3 emission = material._emission;
    if (valid_bindless_handle(material._emission_tex))
    {
        emission = SAMPLE_TEXTURE2D_LOD(
            g_bindless_texture2d[material._emission_tex], g_LinearClampSampler, uv, 0.0f).rgb;
    }
    return max(emission * material._emission_strength, 0.0f.xxx);
}

float DDGIReadMaterialMetallic(MaterialData material, float2 uv)
{
    float metallic = material._metallic;
    if (valid_bindless_handle(material._metallic_roughness_tex))
    {
        const float2 roughness_metallic = SAMPLE_TEXTURE2D_LOD(
            g_bindless_texture2d[material._metallic_roughness_tex], g_LinearClampSampler, uv, 0.0f).rg;
        metallic = roughness_metallic.g;
    }
    return saturate(metallic);
}

bool DDGIIsDirectLightOccluded(TraceSurface surface, uint light_index, LightSample light_sample, float hit_distance)
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

float3 DDGIEvaluateDirectLighting(TraceSurface surface, MaterialData material, uint probe_index, uint ray_index, float hit_distance)
{
    if (_unified_light_count == 0u)
        return 0.0f.xxx;

    RandomCtx random_ctx = (RandomCtx)0;
    InitSeed(random_ctx, uint2(probe_index, ray_index), _frame_index);

    const float select_random = rand(random_ctx);
    const uint light_index = min(
        (uint)(select_random * _unified_light_count), _unified_light_count - 1u);
    LightSample light_sample = SampleUnifiedLight(
        light_index, surface._world_position, surface._shading_normal, random_ctx);
    light_sample._pdf *= GetUnifiedLightSelectionPMF(_unified_light_count);

    if (light_sample._pdf <= 1e-6f || !any(light_sample._radiance > 0.0f.xxx))
        return 0.0f.xxx;

    const float n_dot_l = saturate(dot(surface._shading_normal, light_sample._wi));
    if (n_dot_l <= 1e-6f)
        return 0.0f.xxx;

    if (DDGIIsDirectLightOccluded(surface, light_index, light_sample, hit_distance))
        return 0.0f.xxx;

    const float3 albedo = DDGIReadMaterialAlbedo(material, surface._uv);
    const float metallic = DDGIReadMaterialMetallic(material, surface._uv);
    const float3 diffuse_brdf = Diffuse_Lambert(albedo * (1.0f - metallic));
    return light_sample._radiance * diffuse_brdf * n_dot_l / light_sample._pdf;
}

float3 DDGIEvaluateHitRadiance(TraceSurface surface, uint probe_index, uint ray_index, float hit_distance)
{
    const MaterialData material = g_material_buffer[surface._material_index];
    float3 radiance = DDGIReadMaterialEmission(material, surface._uv);
    radiance += DDGIEvaluateDirectLighting(surface, material, probe_index, ray_index, hit_distance);
    return max(radiance, 0.0f.xxx);
}

[numthreads(128, 1, 1)]
void ProbeTrace(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID)
{
    const uint3 probe_count = uint3(_probe_count_x, _probe_count_y, _probe_count_z);
    const uint probe_index = group_id.x;
    const uint ray_index = group_thread_id.x;
    const uint probe_count_total = probe_count.x * probe_count.y * probe_count.z;
    if (probe_index >= probe_count_total || ray_index >= kRaysPerProbe)
    {
        return;
    }

    const float max_ray_distance = max(_probe_spacing_and_distance.w, 0.001f);
    const float3 base_local_position = DDGIProbeLocalPosition(probe_index, probe_count,
                                                                _probe_spacing_and_distance.xyz);
    const float3 relocation_offset = _use_probe_relocation != 0u
        ? _probe_data[probe_index]._relocation_offset : 0.0f.xxx;
    const float3 local_position = base_local_position + relocation_offset;
    const float3 probe_position = mul(_volume_local_to_world, float4(local_position, 1.0f)).xyz;
    const float3x3 ray_rotation = DDGIGetRayRotation(_frame_index, _freeze_ray_rotation, probe_index);
    const bool fixed_relocation_ray = DDGIIsFixedRelocationRay(ray_index, _use_probe_relocation);
    const float3 ray_direction = DDGIGetProbeRayDirection(ray_index, ray_rotation, _use_probe_relocation);

    TraceRayDesc ray;
    ray._origin = probe_position;
    ray._direction = ray_direction;
    ray._t_min = 0.001f;
    ray._t_max = max_ray_distance;
    ray._mask = 0xffu;

    DDGIProbeRayData ray_data = (DDGIProbeRayData)0;
    ray_data._radiance = fixed_relocation_ray
        ? 0.0f.xxx : _environment_radiance_intensity.xyz * _environment_radiance_intensity.w;
    ray_data._distance = max_ray_distance;

    TraceHit hit;
    if (TraceClosest(ray, hit))
    {
        const float hit_distance = min(max(hit._t, 0.0f), max_ray_distance);
        if (hit._front_face == 0u)
        {
            ray_data._radiance = 0.0f.xxx;
            ray_data._distance = -hit_distance;
        }
        else
        {
            if (!fixed_relocation_ray)
            {
                TraceSurface surface;
                if (LoadTraceSurface(hit, surface))
                {
                    ray_data._radiance = DDGIEvaluateHitRadiance(surface, probe_index, ray_index, hit_distance);
                }
            }
            ray_data._distance = hit_distance;
        }
    }

    _probe_ray_data[DDGIRayDataIndex(probe_index, ray_index)] = ray_data;
}
