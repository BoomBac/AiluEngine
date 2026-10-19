//info begin
//pass begin::
//name: deferred_lighting
//vert: FullscreenVSMain
//pixel: DeferredLightingPSMain
//ZTest: Always
//Cull: Back
//Stencil: {Ref:0,Comp:NotEqual,Pass:Keep}
//Queue: Opaque
//multi_compile _ DEBUG_NORMAL DEBUG_ALBEDO DEBUG_WORLDPOS DEBUG_GI
//multi_compile _ STATIC_DIFFUSE_PROBE_GPU
//pass end::
//Properties
//{
//}
//info end

#include "common.hlsli"
#include "input.hlsli"
#include "cbuffer.hlsli"
#include "lighting.hlsli"
#include "shadow.hlsli"
#include "fullscreen_quad.hlsli"
#include "static_diffuse_probe.hlsli"
#include "ray_trace/ddgi_common.hlsli"
#include "surfel_common.hlsli"


TEXTURE2D(_GBuffer0)
TEXTURE2D(_GBuffer1)
TEXTURE2D(_GBuffer2)
TEXTURE2D(_GBuffer3)
TEXTURE2D(_CameraDepthTexture)

Texture2D _DDGI_IrradianceAtlas : register(t22);
Texture2D<float2> _DDGI_DistanceAtlas : register(t23);
StructuredBuffer<DDGIProbeData> _DDGI_ProbeData : register(t24);
StructuredBuffer<SurfelData> _SurfelPool;
StructuredBuffer<SurfelLightingData> _SurfelLighting;
StructuredBuffer<uint> _SurfelState;
StructuredBuffer<uint2> _SurfelQueryGridCells;
StructuredBuffer<uint> _SurfelQueryGridIndices;
RWStructuredBuffer<uint> _SurfelQueryDebugCounters;

static const float kDDGIWeightCrushThreshold = 0.2f;

PerMaterialCBufferBegin
    float4x4 _DDGI_LocalToWorld;
    float4x4 _DDGI_WorldToLocal;
    float4 _DDGI_ProbeSpacing;
    float4 _DDGI_ProbeCount;
    float4 _DDGI_AtlasInfo;
    float4 _DDGI_DistanceAtlasInfo;
    float _DDGI_UseVisibility;
    float _DDGI_ProbeNormalBias;
    float _DDGI_ProbeViewBias;
    float _DDGI_UseProbeRelocation;
    float _DDGI_DebugMode;
    float _DDGI_Enabled;
    uint _SurfelQueryEnabled;
    uint _SurfelGridResolution;
    uint _SurfelCascadeCount;
    uint _SurfelMaxCount;
    float _SurfelBaseCellSize;
    float _SurfelTanHalfFov;
    float _SurfelTargetPixelRadius;
    uint _SurfelQueryDebugEnabled;
PerMaterialCBufferEnd

float4 SurfelGetGridInfo(uint level)
{
    return SurfelComputeGridInfo(_CameraPos.xyz, level, _SurfelGridResolution, _SurfelBaseCellSize);
}

uint SurfelGetCellIndex(uint level, int3 cell)
{
    const uint resolution = _SurfelGridResolution;
    return ((level * resolution + (uint)cell.z) * resolution + (uint)cell.y) * resolution + (uint)cell.x;
}

void SampleSurfelCascade(float3 world_position, float3 surface_normal, uint cascade, float4 grid_info,
                         float support_scale, uint high_watermark, out float3 irradiance_sum, out float weight_sum,
                         out float geometry_confidence, out uint candidate_count)
{
    irradiance_sum = 0.0f.xxx;
    weight_sum = 0.0f;
    geometry_confidence = 0.0f;
    candidate_count = 0u;
    // Placement caps the indexed radius at 0.7 cell; retain the original world-space support.
    const float max_support_radius = max(support_scale * 0.7f * grid_info.w, 2.0f * _SurfelBaseCellSize);
    const int3 cell_min = max((int3)floor((world_position - max_support_radius - grid_info.xyz) / grid_info.w), 0);
    const int3 cell_max = min((int3)floor((world_position + max_support_radius - grid_info.xyz) / grid_info.w),
                             (int)_SurfelGridResolution - 1);
    if (any(cell_min > cell_max))
        return;

    [loop]
    for (int z = cell_min.z; z <= cell_max.z; ++z)
    {
        const uint2 slab_first = _SurfelQueryGridCells[SurfelGetCellIndex(cascade, int3(cell_min.xy, z))];
        const uint2 slab_last = _SurfelQueryGridCells[SurfelGetCellIndex(cascade, int3(cell_max.xy, z))];
        if (slab_first.x == slab_last.x + slab_last.y)
            continue;

        [loop]
        for (int y = cell_min.y; y <= cell_max.y; ++y)
        {
            // Bound this row by the support sphere, using the closest point in its YZ slab.
            // Keep a small conservative margin at cell boundaries to avoid rounding out a sample.
            const float2 row_min = grid_info.yz + float2(y, z) * grid_info.w;
            const float2 row_delta = max(max(row_min - world_position.yz,
                                            world_position.yz - row_min - grid_info.w), 0.0f);
            const float row_radius_sq = max_support_radius * max_support_radius - dot(row_delta, row_delta);
            if (row_radius_sq < 0.0f)
                continue;
            const float row_radius = sqrt(row_radius_sq) + grid_info.w * 1e-4f;
            const int row_min_x = max(cell_min.x,
                (int)floor((world_position.x - row_radius - grid_info.x) / grid_info.w));
            const int row_max_x = min(cell_max.x,
                (int)floor((world_position.x + row_radius - grid_info.x) / grid_info.w));
            // X cells are contiguous in the prefix index. Two reads cover the entire row,
            // including empty cells; no per-pixel walk through each empty X cell is needed.
            if (row_min_x > row_max_x)
                continue;
            const uint2 row_first = _SurfelQueryGridCells[SurfelGetCellIndex(cascade, int3(row_min_x, y, z))];
            const uint2 row_last = _SurfelQueryGridCells[SurfelGetCellIndex(cascade, int3(row_max_x, y, z))];
            const uint row_end = row_last.x + row_last.y;
            [loop]
            for (uint i = row_first.x; i < row_end; ++i)
            {
                const uint surfel_index = _SurfelQueryGridIndices[i];
                ++candidate_count;
                if (surfel_index >= high_watermark)
                    continue;
                const SurfelData surfel = _SurfelPool[surfel_index];
                if ((surfel._flags & kSurfelValid) == 0u || surfel._cascade_level != cascade || surfel._radius <= 0.0f)
                    continue;
                const int3 surfel_cell = (int3)floor((surfel._position - grid_info.xyz) / grid_info.w);
                if (surfel_cell.y != y || surfel_cell.z != z ||
                    surfel_cell.x < cell_min.x || surfel_cell.x > cell_max.x)
                    continue;

                const float3 delta = world_position - surfel._position;
                const float support_radius = max(support_scale * surfel._radius, 2.0f * _SurfelBaseCellSize);
                const float distance_sq = dot(delta, delta);
                if (distance_sq >= support_radius * support_radius)
                    continue;
                const float contribution = SurfelContributionWeight(surfel, _FrameIndex);
                if (contribution <= 0.0f)
                    continue;
                const float3 surfel_normal = normalize(surfel._normal);
                const float normal_weight = saturate((dot(surface_normal, surfel_normal) - 0.25f) / 0.75f);
                if (normal_weight <= 0.0f)
                    continue;
                const SurfelLightingData lighting = _SurfelLighting[surfel_index];
                if ((lighting._flags & kSurfelLightingValid) == 0u)
                    continue;

                const float relative_distance = sqrt(distance_sq) / support_radius;
                const float falloff = saturate(1.0f - relative_distance);
                const float radial_weight = falloff * falloff * falloff * falloff *
                                            (1.0f + 4.0f * relative_distance);
                const float plane_distance = abs(dot(delta, surfel_normal));
                const float plane_scale = max(surfel._radius * 0.35f, 0.001f);
                const float plane_weight = exp(-0.5f * plane_distance * plane_distance / (plane_scale * plane_scale));
                const float maturity = (lighting._flags & kSurfelLightingInherited) != 0u
                    ? 1.0f : SurfelLightingMaturity(lighting._sample_count);
                const float lighting_weight = maturity * contribution;
                const float weight = radial_weight * plane_weight * normal_weight * lighting_weight;
                irradiance_sum += lighting._irradiance * weight;
                weight_sum += weight;
                const float support_coverage = smoothstep(0.0f, 0.25f, falloff);
                geometry_confidence = max(geometry_confidence, support_coverage * plane_weight * normal_weight);
            }
        }
    }
}

static const float kSurfelFallbackConfidence = 0.35f;

void AccumulateSurfelCascade(float3 world_position, float3 surface_normal, uint cascade, float support_scale,
                             uint high_watermark, inout float3 irradiance_sum, inout float weight_sum,
                             inout float geometry_confidence, inout uint cascade_query_count,
                             inout uint candidate_count)
{
    if (cascade >= min(_SurfelCascadeCount, 5u))
        return;

    ++cascade_query_count;

    const float4 grid_info = SurfelGetGridInfo(cascade);
    const float3 grid_max = grid_info.xyz + grid_info.www * (float)_SurfelGridResolution;
    const float max_support_radius = max(support_scale * 0.7f * grid_info.w, 2.0f * _SurfelBaseCellSize);
    if (any(world_position < grid_info.xyz - max_support_radius) ||
        any(world_position >= grid_max + max_support_radius))
        return;

    float3 level_sum;
    float level_weight;
    float level_geometry_confidence;
    uint level_candidate_count;
    SampleSurfelCascade(world_position, surface_normal, cascade, grid_info, support_scale, high_watermark,
                        level_sum, level_weight, level_geometry_confidence, level_candidate_count);
    irradiance_sum += level_sum;
    weight_sum += level_weight;
    geometry_confidence = max(geometry_confidence, level_geometry_confidence);
    candidate_count += level_candidate_count;
}

void SampleSurfelField(float3 world_position, float3 surface_normal, float support_scale, uint high_watermark,
                       out float3 irradiance_sum, out float weight_sum, out float geometry_confidence,
                       out uint desired_cascade, out uint cascade_query_count, out uint candidate_count)
{
    irradiance_sum = 0.0f.xxx;
    weight_sum = 0.0f;
    geometry_confidence = 0.0f;
    cascade_query_count = 0u;
    candidate_count = 0u;
    const uint cascade_count = min(_SurfelCascadeCount, 5u);
    const float raw_radius = SurfelComputeRawDesiredRadius(world_position, _CameraPos.xyz, _SurfelTanHalfFov,
                                                            _ScreenParams.w, _SurfelTargetPixelRadius);
    bool is_in_grid;
    desired_cascade = SurfelSelectDesiredCascade(world_position, raw_radius, _CameraPos.xyz, cascade_count,
                                                 _SurfelGridResolution, _SurfelBaseCellSize, 0.7f, is_in_grid);
    // Cascades index one continuous surface field. Changing the contributing set at a desired
    // level or confidence threshold creates visible seams; normalize all supported levels together.
    // Keep desired selection only for the existing local fallback, and allow support across grid edges.
    [loop]
    for (uint cascade = 0u; cascade < cascade_count; ++cascade)
        AccumulateSurfelCascade(world_position, surface_normal, cascade, support_scale, high_watermark,
                                irradiance_sum, weight_sum, geometry_confidence, cascade_query_count, candidate_count);
}

bool SampleSurfelIrradiance(float3 world_position, float3 surface_normal, out float3 irradiance)
{
    irradiance = 0.0f.xxx;
    if (_SurfelQueryEnabled == 0u || _SurfelCascadeCount == 0u || _SurfelGridResolution == 0u ||
        _SurfelMaxCount == 0u)
        return false;

    const uint high_watermark = min(_SurfelState[0], _SurfelMaxCount);
    if (high_watermark == 0u)
        return false;

    float3 irradiance_sum;
    float weight_sum;
    float geometry_confidence;
    uint desired_cascade;
    uint cascade_query_count;
    uint candidate_count;
    SampleSurfelField(world_position, surface_normal, 4.0f, high_watermark,
                      irradiance_sum, weight_sum, geometry_confidence, desired_cascade,
                      cascade_query_count, candidate_count);
    irradiance = irradiance_sum / max(weight_sum, 1e-12f) * geometry_confidence;
    if (geometry_confidence < kSurfelFallbackConfidence)
    {
        const float4 grid_info = SurfelGetGridInfo(desired_cascade);
        float3 fallback_sum;
        float fallback_weight;
        float fallback_geometry_confidence;
        uint fallback_candidate_count;
        SampleSurfelCascade(world_position, surface_normal, desired_cascade, grid_info, 5.0f, high_watermark,
                            fallback_sum, fallback_weight, fallback_geometry_confidence, fallback_candidate_count);
        const float fill_weight = saturate((kSurfelFallbackConfidence - geometry_confidence) /
                                           kSurfelFallbackConfidence) * fallback_geometry_confidence;
        irradiance += fallback_sum / max(fallback_weight, 1e-12f) * fill_weight;
        geometry_confidence = saturate(geometry_confidence + fill_weight);
        candidate_count += fallback_candidate_count;
        if (_SurfelQueryDebugEnabled != 0u)
            InterlockedAdd(_SurfelQueryDebugCounters[3], 1u);
    }
    if (_SurfelQueryDebugEnabled != 0u)
    {
        InterlockedAdd(_SurfelQueryDebugCounters[0], 1u);
        InterlockedAdd(_SurfelQueryDebugCounters[1], cascade_query_count);
        InterlockedAdd(_SurfelQueryDebugCounters[2], candidate_count);
        InterlockedMax(_SurfelQueryDebugCounters[4], candidate_count);
    }
    return geometry_confidence > 1e-5f;
}

float3 SampleDDGIProbeIrradiance(uint probe_index, float3 direction)
{
    const float2 oct_uv = DDGIOctEncode(direction);
    const uint tiles_x = (uint)_DDGI_AtlasInfo.z;
    const uint physical_resolution = kRadiancePhysicalResolution;
    const uint2 tile_origin = uint2(probe_index % tiles_x, probe_index / tiles_x) * physical_resolution;
    const float2 atlas_pixel = (float2)tile_origin + kRadianceBorderSize + oct_uv * kRadianceInteriorResolution;
    const float2 atlas_uv = atlas_pixel / _DDGI_AtlasInfo.xy;
    return _DDGI_IrradianceAtlas.SampleLevel(g_LinearClampSampler, atlas_uv, 0.0f).rgb;
}

float2 SampleDDGIProbeDistance(uint probe_index, float3 direction)
{
    const float2 oct_uv = DDGIOctEncode(direction);
    const uint tiles_x = (uint)_DDGI_DistanceAtlasInfo.z;
    const uint physical_resolution = kDistancePhysicalResolution;
    const uint2 tile_origin = uint2(probe_index % tiles_x, probe_index / tiles_x) * physical_resolution;
    const float2 atlas_pixel = (float2)tile_origin + kDistanceBorderSize + oct_uv * kDistanceInteriorResolution;
    const float2 atlas_uv = atlas_pixel / _DDGI_DistanceAtlasInfo.xy;
    return _DDGI_DistanceAtlas.SampleLevel(g_LinearClampSampler, atlas_uv, 0.0f);
}

float DDGIGetNormalWeight(float3 normal_ws, float3 direction_to_probe)
{
    const float wrap = saturate((dot(normal_ws, direction_to_probe) + 1.0f) * 0.5f);
    return wrap * wrap + 0.2f;
}

float DDGIGetProbeStateWeight(float state)
{
    const float base_state = DDGIProbeStateWithoutHistoryReset(state);
    if (base_state >= kDDGIProbeStateBackfaceRejected)
        return 0.0f;
    if (base_state >= kDDGIProbeStateBackfaceNear)
        return 0.5f;
    return 1.0f;
}

float DDGIGetVisibilityWeight(uint probe_index, float3 probe_to_surface_direction, float surface_distance)
{
    const float2 moments = SampleDDGIProbeDistance(probe_index, probe_to_surface_direction);
    const float mean_distance = max(moments.x, 0.0f);
    const float variance = max(moments.y - mean_distance * mean_distance, 0.0f);
    const float distance_delta = max(surface_distance - mean_distance, 0.0f);
    if (distance_delta <= 0.0f)
        return 1.0f;
    float chebyshev_weight = variance / max(variance + distance_delta * distance_delta, 1e-5f);
    chebyshev_weight = max(chebyshev_weight * chebyshev_weight * chebyshev_weight, 0.0f);
    return max(0.05f, chebyshev_weight);
}

float3 DDGIGetSurfaceBias(float3 surface_normal, float3 camera_direction)
{
    return surface_normal * _DDGI_ProbeNormalBias - camera_direction * _DDGI_ProbeViewBias;
}

bool SampleDDGIIrradiance(float3 world_position, float3 normal_ws, out float3 irradiance)
{
    irradiance = 0.0f.xxx;
    if (_DDGI_Enabled < 0.5f || any(_DDGI_ProbeCount.xyz < 1.0f) ||
        any(_DDGI_AtlasInfo.xy <= 0.0f) || _DDGI_AtlasInfo.z < 1.0f ||
        any(_DDGI_DistanceAtlasInfo.xy <= 0.0f) || _DDGI_DistanceAtlasInfo.z < 1.0f)
    {
        return false;
    }

    const uint3 probe_count = uint3(_DDGI_ProbeCount.xyz);
    const float3 half_extent = (float3(probe_count) - 1.0f.xxx) * _DDGI_ProbeSpacing.xyz * 0.5f;
    const float3 local_position = mul(_DDGI_WorldToLocal, float4(world_position, 1.0f)).xyz;
    if (any(abs(local_position) > half_extent + 1e-4f.xxx))
        return false;

    const float3 surface_normal = normalize(normal_ws);
    const float3 camera_to_surface = world_position - _CameraPos.xyz;
    const float camera_distance = length(camera_to_surface);
    const float3 camera_direction = camera_distance > 1e-4f
        ? camera_to_surface / camera_distance : float3(0.0f, 0.0f, 0.0f);
    const float3 surface_bias = DDGIGetSurfaceBias(surface_normal, camera_direction);
    const float3 biased_world_position = world_position + surface_bias;
    const float3 biased_local_position = mul(_DDGI_WorldToLocal, float4(biased_world_position, 1.0f)).xyz;
    const float3 grid_position = clamp((biased_local_position + half_extent) / max(_DDGI_ProbeSpacing.xyz, 1e-4f.xxx),
                                        0.0f.xxx, float3(probe_count - 1u));
    const uint3 base = uint3(floor(grid_position));
    const float3 fraction = grid_position - base;
    const uint probe_count_xy = probe_count.x * probe_count.y;
    float weight_sum = 0.0f;
    float max_probe_weight = 0.0f;
    [unroll]
    for (uint z = 0u; z <= 1u; ++z)
        [unroll]
        for (uint y = 0u; y <= 1u; ++y)
            [unroll]
            for (uint x = 0u; x <= 1u; ++x)
            {
                const uint3 coordinate = min(base + uint3(x, y, z), probe_count - 1u);
                const float trilinear_weight = (x != 0u ? fraction.x : 1.0f - fraction.x) *
                                               (y != 0u ? fraction.y : 1.0f - fraction.y) *
                                               (z != 0u ? fraction.z : 1.0f - fraction.z);
                if (trilinear_weight <= 0.0f)
                    continue;

                const uint probe_index = coordinate.x + coordinate.y * probe_count.x + coordinate.z * probe_count_xy;
                float3 probe_local_position = DDGIProbeLocalPosition(coordinate, probe_count,
                                                                       _DDGI_ProbeSpacing.xyz);
                if (_DDGI_UseProbeRelocation > 0.5f)
                    probe_local_position += _DDGI_ProbeData[probe_index]._relocation_offset;
                const float3 probe_position_ws = mul(_DDGI_LocalToWorld,
                                                     float4(probe_local_position, 1.0f)).xyz;
                const float3 probe_to_biased_surface = biased_world_position - probe_position_ws;
                const float surface_distance = length(probe_to_biased_surface);
                const float3 direction_to_probe = surface_distance > 1e-4f
                    ? -probe_to_biased_surface / surface_distance : -surface_normal;
                const float3 probe_to_surface_direction = surface_distance > 1e-4f
                    ? probe_to_biased_surface / surface_distance : float3(0.0f, 0.0f, 1.0f);
                const float normal_weight = DDGIGetNormalWeight(surface_normal, direction_to_probe);
                const float visibility_weight = _DDGI_UseVisibility > 0.5f
                    ? DDGIGetVisibilityWeight(probe_index, probe_to_surface_direction, surface_distance) : 1.0f;
                const float probe_state_weight = DDGIGetProbeStateWeight(_DDGI_ProbeData[probe_index]._state);
                float weight = max(normal_weight * visibility_weight * probe_state_weight, 1e-6f);
                if (weight < kDDGIWeightCrushThreshold)
                {
                    weight *= weight * weight / (kDDGIWeightCrushThreshold * kDDGIWeightCrushThreshold);
                }
                weight *= trilinear_weight;
                irradiance += SampleDDGIProbeIrradiance(probe_index, surface_normal) * weight;
                weight_sum += weight;
                max_probe_weight = max(max_probe_weight, weight);
            }
    if (weight_sum <= 1e-4f)
    {
        irradiance = 0.0f.xxx;
        return false;
    }
    irradiance /= weight_sum;
    if ((uint)_DDGI_DebugMode == kDDGIProbeDebugModeFinalWeight)
    {
        const float final_weight = saturate(max_probe_weight / weight_sum);
        irradiance = final_weight.xxx;
    }
    return true;
}

// float3 GetViewRay(float2 uv)
// {
//     float2 ndc = uv * 2.0 - 1.0;
//     float3 ray;
//     ray.x = ndc.x / _MatrixP._11;
//     ray.y = ndc.y / _MatrixP._22;
//     ray.z = 1.0;
//     return normalize(ray);
// }

FullScreenPSInput FullscreenVSMain(FullScreenVSInput i);

float4 DeferredLightingPSMain(FullScreenPSInput input) : SV_TARGET
{
	//float2 gbuf0 = _GBuffer0.SampleLevel(g_LinearWrapSampler,input.uv,0).xy;
	float2 gbuf0 = SAMPLE_TEXTURE2D_LOD(_GBuffer0, g_LinearClampSampler, input.uv,0).xy;
	float4 gbuf1 = _GBuffer1.Sample(g_LinearWrapSampler,input.uv);
	float4 gbuf2 = _GBuffer2.Sample(g_LinearWrapSampler,input.uv);
	float4 environment_data = SAMPLE_TEXTURE2D_LOD(_GBuffer3, g_LinearClampSampler, input.uv, 0);
    float3 environment_irradiance = environment_data.rgb;
	float3 diffuse_irradiance = environment_irradiance;
	float depth = SAMPLE_TEXTURE2D_LOD(_CameraDepthTexture,g_LinearWrapSampler,input.uv,0).r;
	float3 world_pos = ComputeWorldSpacePosition(input.uv,depth,_MatrixIVP);
	//float3 world_pos = _CameraPos.xyz + UnprojectByCameraRay(input.uv,depth);
	SurfaceData surface_data;
	surface_data.wnormal = UnpackNormal(gbuf0);
	surface_data.albedo = float4(gbuf1.rgb,1.0f);
	surface_data.roughness = gbuf1.w;
	surface_data.metallic = gbuf2.w;
	surface_data.specular = 1.0.xxx;
        surface_data.anisotropy = 0.0f;
        surface_data.emssive = gbuf2.rgb;
	OrthonormalBasis(surface_data.wnormal,surface_data.tangent,surface_data.bitangent);
	float3 ddgi_irradiance;
	const bool has_ddgi_irradiance = SampleDDGIIrradiance(world_pos, surface_data.wnormal, ddgi_irradiance);
	bool has_surfel_irradiance = false;
	if (has_ddgi_irradiance)
	    diffuse_irradiance = ddgi_irradiance;
	else
	{
	    float3 surfel_irradiance;
	    has_surfel_irradiance = SampleSurfelIrradiance(world_pos, surface_data.wnormal, surfel_irradiance);
	    if (has_surfel_irradiance)
	        diffuse_irradiance = surfel_irradiance;
	}
	bool has_static_probe_irradiance = false;
	#if defined(STATIC_DIFFUSE_PROBE_GPU)
	    float3 static_probe_irradiance;
	    if (!has_ddgi_irradiance && !has_surfel_irradiance &&
	        SampleStaticDiffuseProbeIrradiance(world_pos, surface_data.wnormal, static_probe_irradiance))
	    {
	        diffuse_irradiance = static_probe_irradiance;
	        has_static_probe_irradiance = true;
	    }
	#endif
	uint material_id = (uint)gbuf2.r;
#ifdef DEBUG_NORMAL
	return float4(surface_data.wnormal * GetPreExposure(),1);
#elif DEBUG_ALBEDO
	return float4(surface_data.albedo.rgb * GetPreExposure(), surface_data.albedo.a);
#elif DEBUG_WORLDPOS
	return float4(world_pos.xyz * GetPreExposure(),1);
#else
        const bool has_diffuse_gi = has_ddgi_irradiance || has_surfel_irradiance || has_static_probe_irradiance;
        float3 light = max(0.0, CalculateLightPBR(surface_data, world_pos.xyz, input.uv, diffuse_irradiance,
                                                 environment_irradiance, has_diffuse_gi, 1.0, environment_data.a));
	light += surface_data.emssive;
	//light = LinearEyeDepth(depth,_ProjectionParams.z,_ProjectionParams.y).xxxx;
    //light = Linear01Depth(depth,_ZBufferParams).xxxx;
    //light = Linear01DepthNew(depth,100.0f,1.0f).xxxx;
	return float4(light * GetPreExposure(), 1.0); 
#endif 
}
