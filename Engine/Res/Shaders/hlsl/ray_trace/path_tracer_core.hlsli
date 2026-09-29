#ifndef PATH_TRACER_CORE_HLSLI
#define PATH_TRACER_CORE_HLSLI

#include "path_tracer_common.hlsli"
#include "path_tracer_environment.hlsli"
#include "path_tracer_material.hlsli"
#include "path_tracer_manifold.hlsli"


float3 EvaluatePathDirectLighting(PathHit hit, TraceContext trace_context, float3 wo, inout RandomCtx random_context)
{
    float3 direct_lighting = 0.0.xxx;
    // Ordinary NEE evaluates only continuous lobes. Delta emission hits are handled by TracePath.
    if (!HasContinuousPathBSDF(hit._material))
        return direct_lighting;
    uint light_count = GetUnifiedLightCount();
    if (light_count > 0u)
    {
        float light_selection_pmf = 0.0f;
        uint light_index = SamplePathUnifiedLightIndex(rand(random_context), light_count, light_selection_pmf);
        LightSample light_sample = SampleUnifiedLight(light_index, hit._world_position, hit._shading_normal,
                                                      random_context);
        const float light_pdf = light_sample._pdf * light_selection_pmf;
        float cos_theta = DirectLightCosTheta(trace_context._mat, hit._shading_normal, light_sample._wi);
        if (light_pdf > 1e-6f && any(light_sample._radiance > 0.0.xxx) &&
            (hit._material._is_glass || cos_theta > 1e-6f))
        {
            const float shadow_t_max = GetLightShadowRayTMax(light_sample) * 0.98f;
            const float3 shadow_origin = OffsetPathRayOrigin(hit._world_position, hit._geometric_normal,
                                                            light_sample._wi, hit._hit_t);
#if defined(PATH_TRACER_MNEE)
            const PathHit blocker = TracePathSegment(shadow_origin, light_sample._wi, shadow_t_max);
            if (blocker._type == kPathHitMiss)
            {
#else
            if (!IsPathOccluded(shadow_origin, light_sample._wi, shadow_t_max))
            {
#endif
                if (cos_theta > 1e-6f)
                {
                    float bsdf_pdf = 0.0f;
                    const float3 bsdf = EvalBRDF(trace_context, hit._shading_normal, wo, light_sample._wi, bsdf_pdf);
                    const float mis_weight = PowerHeuristic(light_pdf, bsdf_pdf);
                    direct_lighting += light_sample._radiance * bsdf * cos_theta * (mis_weight / light_pdf);
                }
            }
#if defined(PATH_TRACER_MNEE)
            else if (blocker._type == kPathHitSurface && blocker._material._is_smooth_dielectric && blocker._front_face != 0u)
            {
                direct_lighting += EvaluateManifoldDirectLighting(hit, trace_context, wo, light_index,
                                                                  light_selection_pmf, light_sample, blocker);
            }
#endif
        }
    }

    if (_enable_environment_nee)
    {
        EnvironmentSample environment_sample = SampleEnvironment(random_context);
        float environment_cos_theta = DirectLightCosTheta(trace_context._mat, hit._shading_normal,
                                                           environment_sample._direction);
        if (environment_sample._pdf > 1e-8f && any(environment_sample._radiance > 0.0.xxx) &&
            (hit._material._is_glass || environment_cos_theta > 1e-6f))
        {
            const float3 shadow_origin = OffsetPathRayOrigin(hit._world_position, hit._geometric_normal,
                                                            environment_sample._direction, hit._hit_t);
            if (!IsPathOccluded(shadow_origin, environment_sample._direction, kPathMaxRayT))
            {
                float bsdf_pdf = 0.0f;
                const float3 bsdf = EvalBRDF(trace_context, hit._shading_normal, wo,
                                             environment_sample._direction, bsdf_pdf);
                const float mis_weight = PowerHeuristic(environment_sample._pdf, bsdf_pdf);
                direct_lighting += environment_sample._radiance * bsdf * environment_cos_theta *
                                   (mis_weight / environment_sample._pdf);
            }
        }
    }
    return direct_lighting;
}

PathBounceSample MakeOpaqueDeltaBounce(PathHit hit, SampleBRDFResult sample)
{
    PathBounceSample bounce = (PathBounceSample)0;
    bounce._valid = sample._delta_pdf > 0.0f && dot(hit._shading_normal, sample.wi) > 0.0f;
    bounce._is_delta = true;
    bounce._carry_mis = false;
    bounce._pdf = sample._delta_pdf;
    bounce._throughput_weight = sample._delta_weight;
    bounce._ray_direction = sample.wi;
    bounce._ray_origin = OffsetPathRayOrigin(hit._world_position, hit._geometric_normal, sample.wi, hit._hit_t);
    return bounce;
}

PathBounceSample SamplePathBounce(PathHit hit, TraceContext trace_context, float3 wo, inout RandomCtx random_context)
{
    PathBounceSample bounce = (PathBounceSample)0;
    if (hit._material._is_smooth_dielectric)
    {
        const float transmission = saturate(hit._material._transmission);
        const float lobe_sample = rand(random_context);
        if (lobe_sample < transmission)
        {
            bounce._is_delta = true;
            const float3 normal = hit._geometric_normal;
            const float fresnel = FresnelDielectric(dot(wo, normal), trace_context._ior_i, trace_context._ior_t);
            const float event_sample = rand(random_context);
            if (event_sample < fresnel)
            {
                bounce._ray_direction = normalize(reflect(-wo, normal));
                bounce._pdf = transmission * fresnel;
                bounce._throughput_weight = 1.0.xxx;
            }
            else
            {
                const float3 refracted = refract(-wo, normal, trace_context._eta);
                if (dot(refracted, refracted) <= 1e-12f)
                {
                    bounce._ray_direction = normalize(reflect(-wo, normal));
                    bounce._pdf = transmission;
                    bounce._throughput_weight = 1.0.xxx;
                }
                else
                {
                    bounce._ray_direction = normalize(refracted);
                    bounce._pdf = transmission * (1.0f - fresnel);
                    // Radiance transport scales transmission by (eta_i / eta_t)^2.
                    bounce._throughput_weight = (trace_context._eta * trace_context._eta).xxx;
                    bounce._is_transmission = true;
                }
            }
            bounce._valid = bounce._pdf > 1e-6f;
            bounce._carry_mis = false;
            bounce._ray_origin = OffsetPathRayOrigin(hit._world_position, normal, bounce._ray_direction, hit._hit_t);
            return bounce;
        }

        const float3 random_values = float3(rand(random_context), rand(random_context), rand(random_context));
        SampleBRDFResult opaque_sample = SampleBRDF(trace_context, hit._shading_normal, wo,
                                                    transmission + (1.0f - transmission) * random_values.x,
                                                    random_values.y, random_values.z, rand(random_context));
        if (all(opaque_sample.wi == 0.0.xxx))
            return bounce;
        if (opaque_sample._is_delta)
            return MakeOpaqueDeltaBounce(hit, opaque_sample);

        float opaque_pdf = 0.0f;
        trace_context._lobe = opaque_sample._lobe;
        trace_context._sampled_h = opaque_sample._h;
        const float3 opaque_bsdf = EvalBRDF(trace_context, hit._shading_normal, wo, opaque_sample.wi, opaque_pdf);
        const float opaque_cos_theta = abs(dot(hit._shading_normal, opaque_sample.wi));
        if (opaque_pdf <= 1e-6f || opaque_cos_theta <= 1e-6f || all(opaque_bsdf == 0.0.xxx))
            return bounce;

        bounce._valid = true;
        bounce._carry_mis = true;
        bounce._pdf = opaque_pdf;
        bounce._throughput_weight = opaque_bsdf * (opaque_cos_theta / opaque_pdf);
        bounce._ray_direction = opaque_sample.wi;
        bounce._ray_origin = OffsetPathRayOrigin(hit._world_position, hit._geometric_normal,
                                                 opaque_sample.wi, hit._hit_t);
        return bounce;
    }

    float3 random_values = float3(rand(random_context), rand(random_context), rand(random_context));
    SampleBRDFResult sample = SampleBRDF(trace_context, hit._shading_normal, wo,
                                         random_values.x, random_values.y, random_values.z, rand(random_context));
    if (all(sample.wi == 0.0.xxx))
        return bounce;
    if (sample._is_delta)
        return MakeOpaqueDeltaBounce(hit, sample);

    float bsdf_pdf = 0.0f;
    // 关键：把采样端选中的瓣和采样到的半程向量回填进 trace_context 再求值。
    // 之前这里漏了回填，EvalBRDF 只能从 (wo, wi) 反推半程向量，
    // 近镜面折射下反推出的 h 与 n 近乎垂直、符号由浮点噪声决定，
    // 方向与 pdf 会被算错，玻璃因此系统性偏暗。
    trace_context._lobe = sample._lobe;
    trace_context._sampled_h = sample._h;
    float3 bsdf = EvalBRDF(trace_context, hit._shading_normal, wo, sample.wi, bsdf_pdf);
    float cos_theta = abs(dot(hit._shading_normal, sample.wi));
    if (bsdf_pdf <= 1e-6f || cos_theta <= 1e-6f || all(bsdf == 0.0.xxx))
        return bounce;

    // delta 瓣方向精确，pdf 无意义，不参与光源 MIS
    bounce._valid = true;
    bounce._is_delta = sample._is_delta;
    bounce._carry_mis = !sample._is_delta;
    bounce._is_transmission = sample._is_transmission;
    bounce._pdf = bsdf_pdf;
    bounce._throughput_weight = bsdf * (cos_theta / bsdf_pdf);
    bounce._ray_direction = sample.wi;
    // 统一用几何法线做偏移：它指向射线来的那一侧，能满足「沿射线方向离开当前表面」的语义；
    // shading normal 在背面（折射进入介质后）朝向与几何法线不一致，会把偏移推到表面内部
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
#if defined(PATH_TRACER_MNEE)
    bool has_manifold_receiver = false;
    float manifold_receiver_bsdf_pdf = 0.0f;
    float3 manifold_receiver_position = 0.0.xxx;
    ManifoldPath manifold_path = (ManifoldPath)0;
#endif
    // 当前所在介质每单位长度的吸收系数。0 = 在空气里（或无损介质）。
    float3 medium_sigma_a = 0.0.xxx;

    [loop]
    // Trace the terminal segment too: last-bounce NEE and BSDF environment hits need the same depth limit.
    for (uint depth = 0u; depth <= max_depth; ++depth)
    {
        PathHit hit = TracePathRay(ray_origin, ray_direction);
        if (hit._type == kPathHitMiss)
        {
            // 离开介质前的最后一段也要衰减
            throughput *= ApplyMediumAbsorption(medium_sigma_a, hit._hit_t);
            float environment_mis_weight = 1.0f;
            if (_enable_environment_nee && has_previous_bsdf_sample)
                environment_mis_weight = PowerHeuristic(previous_bsdf_pdf, PdfEnvironment(ray_direction));
            radiance += throughput * EvaluateEnvironmentRadiance(ray_direction) * environment_mis_weight;
            radiance += throughput * EvaluateDirectionalLightMiss(GetUnifiedLightCount(), ray_direction, previous_bsdf_pdf, has_previous_bsdf_sample);
            break;
        }

        if (hit._type == kPathHitLight)
        {
            throughput *= ApplyMediumAbsorption(medium_sigma_a, hit._hit_t);
            float mis_weight = 1.0f;
#if defined(PATH_TRACER_MNEE)
            if (has_manifold_receiver && manifold_path._vertex_count > 0u)
            {
                const uint light_count = GetUnifiedLightCount();
                mis_weight = EvaluateManifoldHitMIS(manifold_path, manifold_receiver_position, hit._light_index,
                                                    manifold_receiver_bsdf_pdf, hit._world_position,
                                                    GetUnifiedLightSelectionPMF(light_count));
            }
            else if (has_previous_bsdf_sample)
#else
            if (has_previous_bsdf_sample)
#endif
            {
                mis_weight = PowerHeuristic(previous_bsdf_pdf, hit._light_pdf);
            }
            radiance += throughput * hit._light_radiance * mis_weight;
            break;
        }

        if (depth == max_depth)
            break;

        // 表面之前的那一段按当前介质衰减（光线离开介质时，这一段仍是介质内的行程）
        throughput *= ApplyMediumAbsorption(medium_sigma_a, hit._hit_t);

        TraceContext trace_context = CreatePathTraceContext(hit._material, hit._front_face != 0u);
        float3 wo = -ray_direction;
        radiance += throughput * hit._material._emission;

        // 临时诊断开关：定义后只统计穿透/反射到几何体或环境的能量，跳过直接光与光源采样，
        // 用来区分「黑边来自几何/遍历」还是「来自光照估计」。排查完应保持注释状态。
        // #define PROBE_GEOMETRY_ONLY 1
#if defined(PROBE_GEOMETRY_ONLY)
        radiance += 0.0.xxx;
#else
        radiance += throughput * EvaluatePathDirectLighting(hit, trace_context, wo, random_context);
#endif

        PathBounceSample bounce = SamplePathBounce(hit, trace_context, wo, random_context);
        if (!bounce._valid)
            break;

#if defined(PATH_TRACER_MNEE)
        if (bounce._is_delta)
        {
            if (has_manifold_receiver && bounce._is_transmission &&
                manifold_path._vertex_count < kMaxManifoldVertices && AppendManifoldVertex(manifold_path, hit))
            {
                manifold_path._event_pdf *= bounce._pdf;
            }
            else
            {
                has_manifold_receiver = false;
            }
        }
        else
        {
            has_manifold_receiver = true;
            manifold_receiver_bsdf_pdf = bounce._pdf;
            manifold_receiver_position = hit._world_position;
            manifold_path = (ManifoldPath)0;
            manifold_path._event_pdf = 1.0f;
        }
#endif

        // 法线已朝向入射侧，折射方向在进出两种情况下都与它反向；用几何面的正背面区分进出。
        if (bounce._is_transmission)
            medium_sigma_a = hit._front_face != 0u ? hit._material._sigma_a : 0.0.xxx;

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
