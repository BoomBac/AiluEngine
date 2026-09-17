#ifndef PATH_TRACER_CORE_HLSLI
#define PATH_TRACER_CORE_HLSLI

#include "path_tracer_common.hlsli"
#include "path_tracer_environment.hlsli"
#include "path_tracer_material.hlsli"

float3 EvaluatePathDirectLighting(PathHit hit, TraceContext trace_context, float3 wo, inout RandomCtx random_context)
{
    uint light_count = GetUnifiedLightCount();
    if (light_count == 0u)
        return 0.0.xxx;

    uint light_index = min((uint)(rand(random_context) * light_count), light_count - 1u);
    LightSample light_sample = SampleUnifiedLight(light_index, hit._world_position, hit._shading_normal,
                                                  random_context);
    light_sample._pdf *= GetUnifiedLightSelectionPMF(light_count);

    float cos_theta = DirectLightCosTheta(trace_context._mat, hit._shading_normal, light_sample._wi);
    if (light_sample._pdf <= 1e-6f || cos_theta <= 1e-6f || !any(light_sample._radiance > 0.0.xxx))
        return 0.0.xxx;

    float shadow_t_max = GetLightShadowRayTMax(light_sample) * 0.98f;
    float3 shadow_origin = OffsetPathRayOrigin(hit._world_position, hit._geometric_normal,
                                               light_sample._wi, hit._hit_t);
    if (IsPathOccluded(shadow_origin, light_sample._wi, shadow_t_max))
        return 0.0.xxx;

    float3 half_vector = normalize(light_sample._wi + wo);
    float bsdf_pdf = 0.0f;
    float3 bsdf = EvalBRDF(trace_context, hit._shading_normal, half_vector, wo, light_sample._wi, bsdf_pdf);
    float mis_weight = PowerHeuristic(light_sample._pdf, bsdf_pdf);
    return light_sample._radiance * bsdf * cos_theta * (mis_weight / light_sample._pdf);
}

PathBounceSample SamplePathBounce(PathHit hit, TraceContext trace_context, float3 wo, inout RandomCtx random_context)
{
    PathBounceSample bounce = (PathBounceSample)0;
    float3 random_values = float3(rand(random_context), rand(random_context), rand(random_context));
    SampleBRDFResult sample = SampleBRDF(trace_context, hit._shading_normal, wo,
                                         random_values.x, random_values.y, random_values.z);
    if (all(sample.wi == 0.0.xxx))
        return bounce;

    float bsdf_pdf = 0.0f;
    float3 bsdf = EvalBRDF(trace_context, hit._shading_normal, sample.h, wo, sample.wi, bsdf_pdf);
    float cos_theta = abs(dot(hit._shading_normal, sample.wi));
    if (bsdf_pdf <= 1e-6f || cos_theta <= 1e-6f || all(bsdf == 0.0.xxx))
        return bounce;

    bounce._valid = true;
    bounce._carry_mis = !trace_context._mat._is_glass;
    bounce._pdf = bsdf_pdf;
    bounce._throughput_weight = bsdf * (cos_theta / bsdf_pdf);
    bounce._ray_direction = sample.wi;
    bounce._ray_origin = OffsetPathRayOrigin(hit._world_position, hit._geometric_normal, sample.wi, hit._hit_t);
    return bounce;
}

bool ApplyPathRussianRoulette(uint depth, inout RandomCtx random_context, inout float3 throughput)
{
    if (depth <= 2u)
        return true;

    float survival_probability = saturate(max(throughput.r, max(throughput.g, throughput.b)));
    if (rand(random_context) > survival_probability)
        return false;
    throughput /= max(survival_probability, 1e-6f);
    return true;
}

float3 TracePath(uint2 pixel, float3 ray_origin, float3 ray_direction, uint max_depth,
                 inout RandomCtx random_context)
{
    float3 radiance = 0.0.xxx;
    float3 throughput = 1.0.xxx;
    float previous_bsdf_pdf = 0.0f;
    bool has_previous_bsdf_sample = false;

    [loop]
    for (uint depth = 0u; depth < max_depth; ++depth)
    {
        PathHit hit = TracePathRay(ray_origin, ray_direction);
        if (hit._type == kPathHitMiss)
        {
            radiance += throughput * EvaluateEnvironmentRadiance(ray_direction);
            break;
        }

        if (hit._type == kPathHitLight)
        {
            float mis_weight = has_previous_bsdf_sample ? PowerHeuristic(previous_bsdf_pdf, hit._light_pdf) : 1.0f;
            radiance += throughput * hit._light_radiance * mis_weight;
            break;
        }

        TraceContext trace_context = CreatePathTraceContext(hit._material, hit._front_face != 0u);
        float3 wo = -ray_direction;
        radiance += throughput * hit._material._emission;
        radiance += throughput * EvaluatePathDirectLighting(hit, trace_context, wo, random_context);

        PathBounceSample bounce = SamplePathBounce(hit, trace_context, wo, random_context);
        if (!bounce._valid)
            break;

        throughput *= bounce._throughput_weight;
        if (!ApplyPathRussianRoulette(depth, random_context, throughput))
            break;

        previous_bsdf_pdf = bounce._pdf;
        has_previous_bsdf_sample = bounce._carry_mis;
        ray_origin = bounce._ray_origin;
        ray_direction = bounce._ray_direction;
    }

    return max(radiance, 0.0.xxx);
}

#endif // PATH_TRACER_CORE_HLSLI
