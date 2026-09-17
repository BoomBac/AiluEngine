#pragma kernel ProbeRadiance

// Diagnostic (leave at 0 for normal rendering).  Set to 1 and reload to tint the probe irradiance by the
// temporal state - R = _use_temporal, G = _history_valid, B = (_hysteresis > 0.1) - so a single viewport
// frame shows whether the history blend is applied: an unchanged image means all three are 1.
#define AL_DDGI_TEMPORAL_DIAGNOSTIC 0

#include "ddgi_common.hlsli"

StructuredBuffer<DDGIProbeRayData> _probe_ray_data;
RWStructuredBuffer<DDGIProbeData> _probe_data;
Texture2D<float4> _radiance_history_texture;
RWTexture2D<float4> _radiance_texture;

CBUFFER_START(ComputeCB)
    uint _probe_count_x;
    uint _probe_count_y;
    uint _probe_count_z;
    uint _frame_index;
    uint _freeze_ray_rotation;
    uint _use_temporal;
    uint _history_valid;
    float _hysteresis;
    float _history_step_ratio;
    uint _use_probe_relocation;
    float _max_ray_distance;
    float _random_ray_backface_threshold;
    uint2 _ddgi_padding;
CBUFFER_END

// Dark end of the update slew limit, in irradiance luminance: a texel whose history is almost black must
// still be able to converge, so the allowed step never drops below this absolute value.
static const float DDGI_HistoryStepMinLuminance = 0.005f;
static const float3 kLuminanceWeights = float3(0.2126f, 0.7152f, 0.0722f);

// 持续偏差闸门的参数（详见 ProbeRadiance 里的注释）。
static const float kChangeRiseRate = 0.08f;   // 相对偏差把 trend 推上去的速率：约 9 帧越过阈值
static const float kChangeFallRate = 0.25f;   // 偏差消失后 trend 回落速率：约 6 帧关上
static const float kChangeThreshold = 0.5f;   // 闸门阈值（trend 是 0..1 的持续偏差）
static const float kChangeSharpness = 6.0f;   // 闸门斜率
static const float kFastHysteresis = 0.7f;    // 变化期的滞回值（约 3 帧跟上）
static const float kQuietStepRatio = 0.01f;   // 静态期每帧最多移动历史亮度的比例

// numthreads 必须等于 kRadianceInteriorResolution（一个线程写一个 interior texel）。
[numthreads(14, 14, 1)]
void ProbeRadiance(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID)
{
    const uint3 probe_count = uint3(_probe_count_x, _probe_count_y, _probe_count_z);
    const uint probe_count_total = probe_count.x * probe_count.y * probe_count.z;
    const uint probe_index = group_id.x;
    if (probe_index >= probe_count_total)
    {
        return;
    }

    // The texel center is decoded into one normal direction of the 6x6 octahedral interior.
    const float2 oct_uv = ((float2)group_thread_id.xy + 0.5f) / (float)kRadianceInteriorResolution;
    const float3 normal = DDGIOctDecode(oct_uv);
    const float3x3 ray_rotation = DDGIGetRayRotation(_frame_index, _freeze_ray_rotation, probe_index);
    const DDGIProbeData initial_probe_data = _probe_data[probe_index];
    const bool reset_history = DDGIProbeNeedsHistoryReset(initial_probe_data._state);

    float3 weighted_radiance = 0.0f.xxx;
    float weight_sum = 0.0f;
    uint backface_count = 0u;
    uint lighting_ray_count = 0u;
    [unroll]
    for (uint ray_index = 0u; ray_index < kRaysPerProbe; ++ray_index)
    {
        if (DDGIIsFixedRelocationRay(ray_index, _use_probe_relocation))
            continue;
        ++lighting_ray_count;
        const DDGIProbeRayData ray_data = _probe_ray_data[DDGIRayDataIndex(probe_index, ray_index)];
        const bool is_backface = DDGIIsBackFaceHit(ray_data._distance, max(_max_ray_distance, 0.001f));
        if (is_backface)
            ++backface_count;
        // Back face hits carry a negative distance and no radiance.  They are dropped entirely
        // (weight included), otherwise they would bias the probe towards black.
        if (is_backface || ray_data._distance <= 0.0f)
            continue;

        const float3 ray_direction = DDGIGetProbeRayDirection(ray_index, ray_rotation, _use_probe_relocation);
        const float cosine = saturate(dot(normal, ray_direction));
        weighted_radiance += max(ray_data._radiance, 0.0f.xxx) * cosine;
        weight_sum += cosine;
    }

    const float backface_ratio = (float)backface_count / max((float)lighting_ray_count, 1.0f);
    const float backface_threshold = saturate(_random_ray_backface_threshold);
    const bool reject_sample = backface_ratio > backface_threshold;
    const bool backface_near_threshold = !reject_sample && backface_threshold > 0.0f &&
        backface_ratio >= backface_threshold * 0.75f;

    // Self normalized estimator: dividing by the accumulated cosine weight instead of the ray
    // count removes the variance that the cosine weights themselves would contribute, and keeps
    // the texel value the irradiance of the texel's octahedral direction.
    const float3 current_irradiance = weighted_radiance * (PI / max(weight_sum, 1e-5f));

    const uint3 probe_coord = DDGIProbeCoord(probe_index, probe_count);
    const uint2 tile_origin = uint2(probe_coord.x, probe_coord.y + probe_coord.z * probe_count.y) *
                              kRadiancePhysicalResolution;
    const uint2 atlas_pixel = tile_origin + kRadianceBorderSize + group_thread_id.xy;
    float4 history_sample = 0.0f.xxxx;
    const bool has_valid_history = _history_valid != 0u && !reset_history;
    if (has_valid_history)
        history_sample = _radiance_history_texture.Load(int3(atlas_pixel, 0));
    const float3 history = max(history_sample.rgb, 0.0f.xxx);
    float3 irradiance = reject_sample ? (has_valid_history ? history : 0.0f.xxx) : current_irradiance;
    float trend = 0.0f;
    if (reject_sample)
    {
        // Do not renormalize a bad sample from its surviving rays.  Keep a valid history stable;
        // a probe without history contributes no new energy until a trustworthy sample arrives.
        trend = has_valid_history && isfinite(history_sample.a) ? saturate(history_sample.a) : 0.0f;
    }
    else if (_use_temporal != 0u && has_valid_history)
    {
        const float3 delta = irradiance - history;
        const float history_luminance = max(dot(history, kLuminanceWeights), 0.0f);

        // 时间累积的老问题是"要么跳变、要么等十秒"：静态压噪声要求每帧只挪一点点，而这会同样
        // 压住真实的光照变化（原来是 (1-h)*ratio = 0.75%/帧，而且按历史亮度比例收敛，是对数级的，
        // 开/关一盏灯要十几秒）。这里用一个"持续偏差"把两者分开：
        //   * 单帧的相对偏差被夹到 1，所以一条射线偶然打中很小的强发光体最多把 trend 推动
        //     kChangeRiseRate，远达不到 kChangeThreshold —— 闸门不开，行为和原来一样安静；
        //   * 光照真变了时偏差连续同号，trend 约 9 帧越过阈值，闸门打开：滞回降到 kFastHysteresis、
        //     每帧允许的移动量放大到 _history_step_ratio，收敛时间降到约 0.3s；
        //   * 变化结束后相对偏差变小，trend 按 kChangeFallRate 回落（约 6 帧），自动回到安静状态。
        // trend 存在图集的 alpha 通道里（RGBA16F，.rgb 之外本来是空的，读写都不受影响）。
        const float relative_delta = abs(dot(delta, kLuminanceWeights)) /
                                     max(history_luminance, DDGI_HistoryStepMinLuminance);
        const float trend_target = min(relative_delta, 1.0f);
        trend = isfinite(history_sample.a) ? saturate(history_sample.a) : 0.0f;
        trend += (trend_target - trend) * (trend_target > trend ? kChangeRiseRate : kChangeFallRate);
        const float changing = saturate((trend - kChangeThreshold) * kChangeSharpness);

        const float hysteresis = lerp(saturate(_hysteresis), kFastHysteresis, changing);
        float3 step = (1.0f - hysteresis) * delta;

        // 每帧允许的移动量：静态时 kQuietStepRatio（一帧尖峰最多挪动 1% 历史亮度），
        // 闸门打开后放大到 _history_step_ratio（默认 0.25）。0 = 变化期不限速。
        const float fast_step_ratio = _history_step_ratio > 0.0f ? _history_step_ratio : 1.0e6f;
        const float step_limit = max(lerp(kQuietStepRatio, fast_step_ratio, changing) * history_luminance,
                                     DDGI_HistoryStepMinLuminance);
        const float step_luminance = dot(abs(step), kLuminanceWeights);
        if (step_luminance > step_limit)
            step *= step_limit / step_luminance;

        irradiance = history + step;
    }

    if (group_thread_id.x == 0u && group_thread_id.y == 0u)
    {
        DDGIProbeData probe_data = initial_probe_data;
        const bool is_relocated = dot(probe_data._relocation_offset, probe_data._relocation_offset) > 1e-8f;
        const float state = reject_sample ? kDDGIProbeStateBackfaceRejected
            : backface_near_threshold ? kDDGIProbeStateBackfaceNear
            : is_relocated ? kDDGIProbeStateRelocated : kDDGIProbeStateNormal;
        probe_data._state = state + (reset_history ? kDDGIProbeStateHistoryReset : 0.0f);
        _probe_data[probe_index] = probe_data;
    }

    _radiance_texture[atlas_pixel] = float4(irradiance, trend);

#if AL_DDGI_TEMPORAL_DIAGNOSTIC
    {
        const float3 temporal_state = float3(_use_temporal != 0u ? 1.0f : 0.0f,
                                             _history_valid != 0u ? 1.0f : 0.0f,
                                             _hysteresis > 0.1f ? 1.0f : 0.0f);
        _radiance_texture[atlas_pixel] = float4(irradiance * temporal_state, 1.0f);
    }
#endif
}
