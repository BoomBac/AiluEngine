#include "../rt_common.hlsli"
#include "../../Compute/cs_common.hlsli"
#include "../../bindless.hlsli"
#include "../../color_space_utils.hlsli"
#include "../sampling.hlsli"
#include "../hit.hlsli"

#include "CommonStruct.hlsli"
#include "Reservoir.hlsli"
#include "TriangleLight.hlsli"
#include "ReservoirStorage.hlsli"


#pragma kernel RayGen

#define MAX_ACCUMULATED_FRAMES 4096
#define ADDITIONAL_SAMPLING 1
#define PER_FRAME_SAMPLES 4



RWTEXTURE2D(_GI_Texture,float4)

CBUFFER_START(ComputeCB)
    int2 _PickPixel;
    int2 _GI_TileOffset;
    int2 _GI_TileSize;
    uint _inst_count;
    uint _tlas_count;
    uint _blas_count;
    uint _tri_count;
    uint _debug_hit_box_idx;
    uint _frame_index;
    bool _show_debug;
    uint _light_count;
    uint _scene_bindless_idx;
    bool _enable_ris;
    bool _enable_resampling;
    uint _prev_surface_buffer_idx;
    uint _curr_surface_buffer_idx;
    uint _prev_reservoir_buffer_handle;
    uint _curr_reservoir_buffer_handle;
    uint _light_sample_count;
    uint _brdf_sample_count;
CBUFFER_END

//size: 4 * 16 = 64 bytes
struct Surface
{
    float3 _position;
    float _linear_depth;

    float3 _normal;
    float _roughness;

    float3 _geo_normal;
    float _metallic;

    float3 _albedo;
    float _padding;
};

bool IsValidSurface(Surface s)
{
#if defined(_REVERSED_Z)
    return s._linear_depth > kZFar;
#else
    return s._linear_depth < kZFar;
#endif
}


//#define DEBUG_MODE DEBUG_MODE_NORMAL
#define RAYTRACE_GI_HIT_USE_BINDLESS_TRIANGLE_BUFFER 1
#define RAYTRACE_GI_HIT_USE_BINDLESS_MESH_BUFFERS 1

#ifndef DEBUG_MODE
    #define DEBUG_MODE DEBUG_MODE_NONE
#endif
#ifndef RAYTRACE_GI_HIT_USE_BINDLESS_TRIANGLE_BUFFER
    #define RAYTRACE_GI_HIT_USE_BINDLESS_TRIANGLE_BUFFER 0
#endif
#ifndef RAYTRACE_GI_HIT_USE_BINDLESS_MESH_BUFFERS
    #define RAYTRACE_GI_HIT_USE_BINDLESS_MESH_BUFFERS 0
#endif


AppendStructuredBuffer<DebugRay>   _debug_rays;
AppendStructuredBuffer<uint>   _debug_ray_indices;

// StructuredBuffer<Reservoir>    g_prev_reservoir;
// RWStructuredBuffer<Reservoir>  g_curr_reservoir;


StructuredBuffer<MaterialData> g_material_buffer;
TEXTURE2D(_MotionVectorTexture)
//ConstantBuffer<UnifiedLightBufferConfig> _UnifiedLightConfig;


uint GetUnifiedLightCount()
{
    return _light_count;
}

#include "../path_tracer_light_common.hlsli"

// 固定的每层颜色
static const float3 kDepthColors[kMaxDepth] =
{
    float3(0.5, 0.0, 1.0),    // depth = 7  (紫)
    float3(0.0, 1.0, 0.0),   // depth = 1  (绿)
    float3(0.0, 0.0, 1.0),   // depth = 2  (蓝)
    float3(1.0, 1.0, 0.0),   // depth = 3  (黄)
    float3(1.0, 0.0, 1.0),   // depth = 4  (品红)
    float3(0.0, 1.0, 1.0),   // depth = 5  (青)
    float3(1.0, 0.5, 0.0),   // depth = 6  (橙)
    float3(0.5, 0.0, 1.0),    // depth = 7  (紫)
    float3(1.0, 0.0, 0.0)    // miss
};


uint PixelToLinearIndex(uint2 pixel)
{
    return pixel.y * (uint)_ScreenParams.z + pixel.x;
}


bool HitWorld(float3 ray_origin,float3 ray_dir,bool is_debug,out HitRecord rec,out LightSample light_sample,out float3 debug_color)
{
    rec = (HitRecord)0;
    rec.t = 1e20;
    rec.is_light = false;
    rec.emission = 0;
    rec.pdf = 1.0;
    rec.light_idx = 0;
    debug_color = 0;
    bool hit_anything = false;

    for (uint light_index = 0; light_index < _light_count; ++light_index)
    {
        float light_t = 0.0;
        LightSample hit_light_sample = (LightSample)0;
        if (EvaluateUnifiedLightHit(_light_count,light_index, ray_origin, ray_dir, light_t, hit_light_sample) && light_t < rec.t)
        {
            rec.t = light_t;
            rec.p = ray_origin + rec.t * ray_dir;
            rec.normal = hit_light_sample._normal;
            rec.front_face = dot(ray_dir, rec.normal) < 0.0;
            rec.uv = 0;
            rec.material_type = 0;
            rec.material_idx = 0;
            rec.is_light = true;
            rec.light_idx = light_index;
            rec.emission = hit_light_sample._radiance;
            hit_anything = true;
            light_sample = hit_light_sample;
            debug_color = float3(1.0, 0.8, 0.2);
            rec.pdf = light_sample._pdf; 
        }
    }
    if (hit_anything)
        return true;
    TriangleHitCandidate best_hit;
    ObjectInstanceData best_inst;
    if (!TraverseSceneClosest(ray_origin, ray_dir, rec, best_hit, best_inst))
        return hit_anything;

    TriangleData tri;
    LoadHitTriangleData(best_hit.tri_idx,best_inst._position_bindless_idx,best_inst._normal_bindless_idx,best_inst._uv_bindless_idx,best_inst._index_bindless_idx, tri); 

    float3 n_local = normalize(
        tri.n0 * (1 - best_hit.bary.x - best_hit.bary.y) +
        tri.n1 * best_hit.bary.x +
        tri.n2 * best_hit.bary.y);
    float3 n_world = normalize(mul(best_inst._local_to_world, float4(n_local, 0)).xyz);
    rec.front_face = dot(ray_dir, n_world) < 0;
    rec.normal = rec.front_face ? n_world : -n_world;
    rec.material_idx = best_inst._material_id;
    rec.material_type = 0;
    rec.emission = g_material_buffer[rec.material_idx]._emission;
    rec.is_light = false;
    rec.uv = tri.uv0 * (1 - best_hit.bary.x - best_hit.bary.y) +
              tri.uv1 * best_hit.bary.x +
              tri.uv2 * best_hit.bary.y;
    debug_color = float3(rec.uv,0.0);
    return true;
}

bool HitAnyWorld(float3 ray_origin, float3 ray_dir, float max_t)
{
    float ray_tmax_world = max_t - 1e-3;
    if (ray_tmax_world <= 0.0)
        return false;

    return TraverseSceneAny(ray_origin, ray_dir, ray_tmax_world);
}

bool IsMaterialTransmissive(MaterialData mat_data)
{
    return mat_data._transmission > 0.0 && mat_data._metallic < 1.0;
}

bool IsOccluded(float3 origin, float3 dir, float max_t)
{
    float remaining_t = max_t;
    float3 ray_origin = origin;

    [loop]
    for (uint step = 0; step < 8; ++step)
    {
        HitRecord rec = (HitRecord)0;
        rec.t = remaining_t;

        TriangleHitCandidate best_hit;
        ObjectInstanceData best_inst;
        if (!TraverseSceneClosest(ray_origin, dir, rec, best_hit, best_inst))
            return false;

        MaterialData mat_data = g_material_buffer[best_inst._material_id];
        if (!IsMaterialTransmissive(mat_data))
            return true;

        float hit_t = best_hit.world_t;
        float adaptive_offset = max(0.001, hit_t * 1e-4);
        ray_origin = best_hit.world_pos + adaptive_offset * dir;
        remaining_t -= hit_t + adaptive_offset;

        if (remaining_t <= 0.0)
            return false;
    }

    return false;
}


// 32 bit Jenkins hash
uint RTXDI_JenkinsHash(uint a)
{
    // http://burtleburtle.net/bob/hash/integer.html
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23c) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09) ^ (a >> 16);
    return a;
}
#define RTXDI_RANDAOM_SAMPLER_PRIME_CONSTANT 31
// Initialized the random sampler for a given pixel or tile index.
// The pass parameter is provided to help generate different RNG sequences
// for different resampling passes, which is important for image quality.
// In general, a high quality RNG is critical to get good results from ReSTIR.
// A table-based blue noise RNG dose not provide enough entropy, for example.
RTXDI_RandomSamplerState RTXDI_InitRandomSampler(uint2 pixelPos, uint frameIndex, uint pass)
{
    RTXDI_RandomSamplerState state;

    uint linearPixelIndex = PixelToLinearIndex(pixelPos);

    state.index = 1;
    state.seed = RTXDI_JenkinsHash(linearPixelIndex) + frameIndex + (pass * RTXDI_RANDAOM_SAMPLER_PRIME_CONSTANT);

    return state;

}

struct RTXDI_InitialSamplingMisData
{
	uint numMisSamples;
	float localLightMisWeight;
	float environmentMapMisWeight;
	float brdfMisWeight;
};

RTXDI_InitialSamplingMisData RTXDI_ComputeInitialSamplingMisData(RTXDI_DIInitialSamplingParameters initialSamplingParams)
{
	RTXDI_InitialSamplingMisData result;

	result.numMisSamples = initialSamplingParams.numLocalLightSamples +
		initialSamplingParams.numEnvironmentSamples +
		initialSamplingParams.numBrdfSamples;

	result.localLightMisWeight = float(initialSamplingParams.numLocalLightSamples) / result.numMisSamples;
	result.environmentMapMisWeight = float(initialSamplingParams.numEnvironmentSamples) / result.numMisSamples;
	result.brdfMisWeight = float(initialSamplingParams.numBrdfSamples) / result.numMisSamples;

	return result;
}


RAB_LightSample RAB_EmptyLightSample()
{
    return (RAB_LightSample)0;
}

bool RAB_IsAnalyticLightSample(RAB_LightSample lightSample)
{
    return false;
}

float RAB_LightSampleSolidAnglePdf(RAB_LightSample lightSample)
{
    return lightSample.solidAnglePdf;
}



struct RTXDI_LightBufferRegion
{
    uint32_t firstLightIndex;
    uint32_t numLights;
    uint32_t pad1;
    uint32_t pad2;
};

struct RTXDI_EnvironmentLightBufferParameters
{
    uint32_t lightPresent;
    uint32_t lightIndex;
    uint32_t pad1;
    uint32_t pad2;
};



struct RTXDI_LightBufferParameters
{
    RTXDI_LightBufferRegion localLightBufferRegion;
    RTXDI_LightBufferRegion infiniteLightBufferRegion;
    RTXDI_EnvironmentLightBufferParameters environmentLightParams;
};

uint RTXDI_murmur3(inout RTXDI_RandomSamplerState r)
{
#define ROT32(x, y) ((x << y) | (x >> (32 - y)))

    // https://en.wikipedia.org/wiki/MurmurHash
    uint c1 = 0xcc9e2d51;
    uint c2 = 0x1b873593;
    uint r1 = 15;
    uint r2 = 13;
    uint m = 5;
    uint n = 0xe6546b64;

    uint hash = r.seed;
    uint k = r.index++;
    k *= c1;
    k = ROT32(k, r1);
    k *= c2;

    hash ^= k;
    hash = ROT32(hash, r2) * m + n;

    hash ^= 4;
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);

#undef ROT32

    return hash;
}

// Draws a random number X from the sampler, so that (0 <= X < 1).
float RTXDI_GetNextRandom(inout RTXDI_RandomSamplerState rng)
{
    uint v = RTXDI_murmur3(rng);
    const uint one = asuint(1.f);
    const uint mask = (1 << 23) - 1;
    return asfloat((mask & v) | one) - 1.f;
}
float2 RTXDI_RandomlySelectLocalLightUV(inout RTXDI_RandomSamplerState rng)
{
    float2 uv;
    uv.x = RTXDI_GetNextRandom(rng);
    uv.y = RTXDI_GetNextRandom(rng);
    return uv;
}

float square(float x)
{
    return x * x;
}

float ImportanceSampleGGX_VNDF_PDF(float roughness, float3 N, float3 V, float3 L)
{
    float3 H = normalize(L + V);
    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));

    float alpha = square(roughness);
    float D = square(alpha) / (PI * square(square(NoH) * square(alpha) + (1 - square(NoH))));
    return (VoH > 0.0) ? D / (4.0 * VoH) : 0.0;
}

#define kMinRoughness 0.05f
// Return PDF wrt solid angle for the BRDF in the given dir
float RAB_SurfaceEvaluateBrdfPdf(RAB_Surface surface, float3 dir)
{
    float cosTheta = saturate(dot(surface.normal, dir));
    float diffusePdf = cosTheta / PI;
    float specularPdf = ImportanceSampleGGX_VNDF_PDF(max(surface.material.roughness, kMinRoughness), surface.normal, surface.viewDir, dir);
    float pdf = cosTheta > 0.f ? lerp(specularPdf, diffusePdf, surface.diffuseProbability) : 0.f;
    return pdf;
}

// Computes the multi importance sampling pdf for brdf and light sample.
// For light and BRDF PDFs wrt solid angle, blend between the two.
//      lightSelectionPdf is a dimensionless selection pdf
float RTXDI_LightBrdfMisWeight(RAB_Surface surface, RAB_LightSample lightSample,
    float lightSelectionPdf, float lightMisWeight, bool isEnvironmentMap,
    float brdfMisWeight, float brdfCutoff)
{
    float lightSolidAnglePdf = RAB_LightSampleSolidAnglePdf(lightSample);
    if (brdfMisWeight == 0 || RAB_IsAnalyticLightSample(lightSample) ||
        lightSolidAnglePdf <= 0 || isinf(lightSolidAnglePdf) || isnan(lightSolidAnglePdf))
    {
        // BRDF samples disabled or we can't trace BRDF rays MIS with analytical lights
        return lightMisWeight * lightSelectionPdf;
    }

    float3 lightDir = lightSample.position - surface.worldPos;
    //float lightDistance;
    //RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);

    // Compensate for ray shortening due to brdf cutoff, does not apply to environment map sampling
    float brdfPdf = RAB_SurfaceEvaluateBrdfPdf(surface, lightDir);
    // float maxDistance = RTXDI_BrdfMaxDistanceFromPdf(brdfCutoff, brdfPdf);
    // if (!isEnvironmentMap && lightDistance > maxDistance)
    //     brdfPdf = 0.f;

    // Convert light selection pdf (unitless) to a solid angle measurement
    float sourcePdfWrtSolidAngle = lightSelectionPdf * lightSolidAnglePdf;

    // MIS blending against solid angle pdfs.
    float blendedPdfWrtSolidangle = lightMisWeight * sourcePdfWrtSolidAngle + brdfMisWeight * brdfPdf;

    // Convert back, RTXDI divides shading again by this term later
    return blendedPdfWrtSolidangle / lightSolidAnglePdf;
}

float Lambert(float3 normal, float3 lightIncident)
{
    return max(0, -dot(normal, lightIncident)) / PI;
}

RAB_Material RAB_GetMaterial(RAB_Surface surface)
{
    return surface.material;
}
float G_Smith_over_NdotV(float roughness, float NdotV, float NdotL)
{
    float alpha = square(roughness);
    float g1 = NdotV * sqrt(square(alpha) + (1.0 - square(alpha)) * square(NdotL));
    float g2 = NdotL * sqrt(square(alpha) + (1.0 - square(alpha)) * square(NdotV));
    return 2.0 * NdotL / (g1 + g2);
}

float Schlick_Fresnel(float f0, float VdotH)
{
    return f0 + (1 - f0) * pow(max(1 - VdotH, 0), 5);
}

float3 Schlick_Fresnel(float3 f0, float VdotH)
{
    return f0 + (1 - f0) * pow(max(1 - VdotH, 0), 5);
}
float3 GGX_times_NdotL(float3 V, float3 L, float3 N, float roughness, float3 f0)
{
    float3 H = normalize(L + V);

    float NoL = saturate(dot(N, L));
    float VoH = saturate(dot(V, H));
    float NoV = saturate(dot(N, V));
    float NoH = saturate(dot(N, H));

    if (NoL > 0)
    {
        float G = G_Smith_over_NdotV(roughness, NoV, NoL);
        float alpha = square(roughness);
        float D = square(alpha) / (PI * square(square(NoH) * square(alpha) + (1 - square(NoH))));

        float3 F = Schlick_Fresnel(f0, VoH);

        return F * (D * G / 4);
    }
    return 0;
}

// Evaluate the surface BRDF and compute the weighted reflected radiance for the given light sample
float3 ShadeSurfaceWithLightSample(RAB_LightSample lightSample, RAB_Surface surface)
{
    // Ignore invalid light samples
    if (lightSample.solidAnglePdf <= 0)
        return 0;

    float3 L = normalize(lightSample.position - surface.worldPos);

    // Ignore light samples that are below the geometric surface (but above the normal mapped surface)
    if (dot(L, surface.geoNormal) <= 0)
        return 0;


    float3 V = surface.viewDir;
    
    // Evaluate the BRDF
    float diffuse = Lambert(surface.normal, -L);
    float3 specular = GGX_times_NdotL(V, L, surface.normal, max(RAB_GetMaterial(surface).roughness, kMinRoughness), RAB_GetMaterial(surface).specularF0);

    float3 reflectedRadiance = lightSample.radiance * (diffuse * surface.material.diffuseAlbedo + specular);

    return reflectedRadiance / lightSample.solidAnglePdf;
}
float calcLuminance(float3 color)
{
    return dot(color.xyz, float3(0.299f, 0.587f, 0.114f));
}

// Compute the target PDF (p-hat) for the given light sample relative to a surface
float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample lightSample, RAB_Surface surface)
{
    // Second-best implementation: the PDF is proportional to the reflected radiance.
    // The best implementation would be taking visibility into account,
    // but that would be prohibitively expensive.
    return calcLuminance(ShadeSurfaceWithLightSample(lightSample, surface));
}

RAB_LightSample LightSampleToRAB_LightSample(LightSample ls,float3 surface_pos)
{
    RAB_LightSample rabSample;
    rabSample.position = surface_pos + ls._wi * ls._t;
    rabSample.normal = ls._normal;
    rabSample.radiance = ls._radiance;
    rabSample.solidAnglePdf = ls._pdf;
    return rabSample;
}

bool RTXDI_StreamLocalLightAtUVIntoReservoir(
    inout RTXDI_RandomSamplerState rng,
    RTXDI_InitialSamplingMisData misData,
    RAB_Surface surface,
	float brdfCutoff,
	float localLightMisWeight,
    uint lightIndex,
    float2 uv,
    float invSourcePdf,
    RAB_LightInfo lightInfo,
    inout RTXDI_DIReservoir state,
    inout RAB_LightSample o_selectedSample)
{
    //SampleLight(SampleLightData light, float3 x, float3 n, float2 u)
    LightSample local_sample = SampleUnifiedLight(lightIndex,surface.worldPos, surface.normal, uv);
    RAB_LightSample candidateSample = LightSampleToRAB_LightSample(local_sample, surface.worldPos);
    float blendedSourcePdf = RTXDI_LightBrdfMisWeight(surface, candidateSample, 1.0 / invSourcePdf,
        misData.localLightMisWeight, false, misData.brdfMisWeight, brdfCutoff);
    float targetPdf = RAB_GetLightSampleTargetPdfForSurface(candidateSample, surface);
    float risRnd = RTXDI_GetNextRandom(rng);

    if (blendedSourcePdf == 0)
    {
        return false;
    }
    bool selected = RTXDI_StreamSample(state, lightIndex, uv, risRnd, targetPdf, 1.0 / blendedSourcePdf);

    if (selected) {
        o_selectedSample = candidateSample;
    }
    return true;
}


RTXDI_DIReservoir RTXDI_SampleLocalLightsInternal(
    inout RTXDI_RandomSamplerState rng,
    inout RTXDI_RandomSamplerState coherentRng,
    RAB_Surface surface,
    RTXDI_DIInitialSamplingParameters sampleParams,
	RTXDI_InitialSamplingMisData misData,
    uint localLightSamplingMode,
    RTXDI_LightBufferRegion localLightBufferRegion,
#if RTXDI_ENABLE_PRESAMPLING
    RTXDI_RISBufferSegmentParameters localLightRISBufferSegmentParams,
#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
    ReGIR_Parameters regirParams,
#endif
#endif
    out RAB_LightSample o_selectedSample)
{
    RTXDI_DIReservoir state = RTXDI_EmptyDIReservoir();

//     RTXDI_LocalLightSelectionContext lightSelectionContext = RTXDI_InitializeLocalLightSelectionContext(coherentRng, localLightSamplingMode, localLightBufferRegion
// #if RTXDI_ENABLE_PRESAMPLING
//     ,localLightRISBufferSegmentParams
// #if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
//     ,regirParams
//     ,surface
// #endif
// #endif
//     );

    for (uint i = 0; i < sampleParams.numLocalLightSamples; i++)
    {
        uint lightIndex;
        RAB_LightInfo lightInfo;
        float invSourcePdf;

        float rnd = RTXDI_GetNextRandom(rng);
#if RTXDI_STRATIFY_LOCAL_SAMPLING
        rnd = (rnd + i) / sampleParams.numLocalLightSamples;
#endif // RTXDI_STRATIFY_LOCAL_SAMPLING
        lightIndex = rnd * localLightBufferRegion.numLights;
        invSourcePdf = localLightBufferRegion.numLights;

        float2 uv = RTXDI_RandomlySelectLocalLightUV(rng);
        bool zeroPdf = RTXDI_StreamLocalLightAtUVIntoReservoir(rng, misData, surface, sampleParams.brdfCutoff, misData.localLightMisWeight, lightIndex, uv, invSourcePdf, lightInfo, state, o_selectedSample);

        if (zeroPdf)
            continue;
    }

    RTXDI_FinalizeResampling(state, 1.0, misData.numMisSamples);
    state.M = 1;

    return state;
}

RTXDI_DIReservoir RTXDI_SampleLocalLights(
    inout RTXDI_RandomSamplerState rng,
    inout RTXDI_RandomSamplerState coherentRng,
    RAB_Surface surface,
    RTXDI_DIInitialSamplingParameters sampleParams,
	RTXDI_InitialSamplingMisData misData,
    uint localLightSamplingMode,
    RTXDI_LightBufferRegion localLightBufferRegion,
#if RTXDI_ENABLE_PRESAMPLING
    RTXDI_RISBufferSegmentParameters localLightRISBufferSegmentParams,
#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
    ReGIR_Parameters regirParams,
#endif
#endif
    out RAB_LightSample o_selectedSample)
{
    o_selectedSample = RAB_EmptyLightSample();

    if (localLightBufferRegion.numLights == 0)
        return RTXDI_EmptyDIReservoir();

    if (sampleParams.numLocalLightSamples == 0)
        return RTXDI_EmptyDIReservoir();

    return RTXDI_SampleLocalLightsInternal(rng, coherentRng, surface, sampleParams, misData, localLightSamplingMode, localLightBufferRegion,
#if RTXDI_ENABLE_PRESAMPLING
    localLightRISBufferSegmentParams,
#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
    regirParams,
#endif
#endif
    o_selectedSample);
}
// Constructs an orthonormal basis based on the provided normal.
// https://graphics.pixar.com/library/OrthonormalB/paper.pdf
void ConstructONB(float3 normal, out float3 tangent, out float3 bitangent)
{
    float sign = (normal.z >= 0) ? 1 : -1;
    float a = -1.0 / (sign + normal.z);
    float b = normal.x * normal.y * a;
    tangent = float3(1.0f + sign * normal.x * normal.x * a, sign * b, -sign * normal.x);
    bitangent = float3(b, sign + normal.y * normal.y * a, -normal.y);
}

float3 worldToTangent(RAB_Surface surface, float3 w)
{
    // reconstruct tangent frame based off worldspace normal
    // this is ok for isotropic BRDFs
    // for anisotropic BRDFs, we need a user defined tangent
    float3 tangent;
    float3 bitangent;
    ConstructONB(surface.normal, tangent, bitangent);

    return float3(dot(bitangent, w), dot(tangent, w), dot(surface.normal, w));
}

float3 tangentToWorld(RAB_Surface surface, float3 h)
{
    // reconstruct tangent frame based off worldspace normal
    // this is ok for isotropic BRDFs
    // for anisotropic BRDFs, we need a user defined tangent
    float3 tangent;
    float3 bitangent;
    ConstructONB(surface.normal, tangent, bitangent);

    return bitangent * h.x + tangent * h.y + surface.normal * h.z;
}

float2 SampleDisk(float2 random)
{
    float angle = 2 * PI * random.x;
    return float2(cos(angle), sin(angle)) * sqrt(random.y);
}

float3 SampleCosHemisphere(float2 random, out float solidAnglePdf)
{
    float2 tangential = SampleDisk(random);
    float elevation = sqrt(saturate(1.0 - random.y));

    solidAnglePdf = elevation / PI;

    return float3(tangential.xy, elevation);
}

// Returns the sampled H vector in tangent space, assuming N = (0, 0, 1).
// Ve is in the same tangent space.
float3 ImportanceSampleGGX_VNDF(float2 random, float roughness, float3 Ve, float ndf_trim)
{
    float alpha = square(roughness);

    float3 Vh = normalize(float3(alpha * Ve.x, alpha * Ve.y, Ve.z));

    float lensq = square(Vh.x) + square(Vh.y);
    float3 T1 = lensq > 0.0 ? float3(-Vh.y, Vh.x, 0.0) * (1 / sqrt(lensq)) : float3(1.0, 0.0, 0.0);
    float3 T2 = cross(Vh, T1);

    float r = sqrt(random.x * ndf_trim);
    float phi = 2.0 * PI * random.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - square(t1)) + s * t2;

    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - square(t1) - square(t2))) * Vh;

    float3 H;
    H.x = alpha * Nh.x;
    H.y = alpha * Nh.y;
    H.z = max(0.0, Nh.z);

    return H;
}

/*
 * Importance sample the BRDF for the surface
 * Used by ReSTIR DI
 */
bool RAB_SurfaceImportanceSampleBrdf(RAB_Surface surface, inout RTXDI_RandomSamplerState rng, out float3 dir)
{
    float3 rand;
    rand.x = RTXDI_GetNextRandom(rng);
    rand.y = RTXDI_GetNextRandom(rng);
    rand.z = RTXDI_GetNextRandom(rng);
    if (rand.x < surface.diffuseProbability)
    {
        float pdf;
        float3 h = SampleCosHemisphere(rand.yz, pdf);
        dir = tangentToWorld(surface, h);
    }
    else
    {
        // Glossy reflection
        float3 Ve = normalize(worldToTangent(surface, surface.viewDir));
        float3 h = ImportanceSampleGGX_VNDF(rand.yz, max(surface.material.roughness, kMinRoughness), Ve, 1.0);
        h = normalize(h);
        dir = reflect(-surface.viewDir, tangentToWorld(surface, h));
    }

    return dot(surface.normal, dir) > 0.f;
}

#define RTXDI_InvalidLightIndex 0xFFFFFFFF

// Heuristic to determine a max visibility ray length from a PDF wrt. solid angle.
float RTXDI_BrdfMaxDistanceFromPdf(float brdfCutoff, float pdf)
{
    const float kRayTMax = 3.402823466e+38F; // FLT_MAX
    return brdfCutoff > 0.f ? sqrt((1.f / brdfCutoff - 1.f) * pdf) : kRayTMax;
}

float3 RAB_GetSurfaceWorldPos(RAB_Surface surface)
{
    return surface.worldPos;
}
float RAB_GetSurfaceLinearDepth(RAB_Surface surface)
{
    return surface.viewDepth;
}

RAB_Surface RAB_EmptySurface()
{
    RAB_Surface s = (RAB_Surface)0;
    s.viewDepth = 1.0;
    return s;
}

float RAB_EvaluateLocalLightSourcePdf(uint lightIndex)
{
    // Uniform pdf
    //return 1.0 / g_Const.lightBufferParams.localLightBufferRegion.numLights;
    return 1.0 / GetUnifiedLightCount();
}

RTXDI_DIReservoir RTXDI_SampleBrdf(
    inout RTXDI_RandomSamplerState rng,
    RAB_Surface surface,
	uint numBrdfSamples,
	float brdfCutoff,
	float brdfRayMinT,
	RTXDI_InitialSamplingMisData misData,
	inout RTXDI_RandomSamplerState coherentRng,
    RTXDI_LightBufferParameters lightBufferParams,
    out RAB_LightSample o_selectedSample)
{
    RTXDI_DIReservoir state = RTXDI_EmptyDIReservoir();
    
    for (uint i = 0; i < numBrdfSamples; ++i)
    {
        float lightSourcePdf = 0;
        float3 sampleDir;
        uint lightIndex = RTXDI_InvalidLightIndex;
        float2 randXY = float2(0, 0);
        RAB_LightSample candidateSample = RAB_EmptyLightSample();

        if (RAB_SurfaceImportanceSampleBrdf(surface, rng, sampleDir))
        {
            float brdfPdf = RAB_SurfaceEvaluateBrdfPdf(surface, sampleDir);
            float maxDistance = RTXDI_BrdfMaxDistanceFromPdf(brdfCutoff, brdfPdf);
            float3 brdf_origin = RAB_GetSurfaceWorldPos(surface) + 0.001 * sampleDir;
            uint hit_light_idx = 0u;
            float hit_light_t = 0.0;
            LightSample hit_light_sample = (LightSample)0;
            bool hitAnything =  FindClosestUnifiedLightHit(_light_count, brdf_origin, sampleDir, 1e5, hit_light_idx, hit_light_t, hit_light_sample);
            lightIndex = hitAnything? hit_light_idx : RTXDI_InvalidLightIndex;

            if (lightIndex != RTXDI_InvalidLightIndex)
            {
                candidateSample = LightSampleToRAB_LightSample(hit_light_sample, surface.worldPos);
                    
                if (brdfCutoff > 0.f)
                {
                    // If Mis cutoff is used, we need to evaluate the sample and make sure it actually could have been
                    // generated by the area sampling technique. This is due to numerical precision.
                    float3 lightDir = candidateSample.position - surface.worldPos;
                    float lightDistance = length(lightDir);
                    //float lightDistance;
                    //RAB_GetLightDirDistance(surface, candidateSample, lightDir, lightDistance);

                    float brdfPdf = RAB_SurfaceEvaluateBrdfPdf(surface, lightDir);
                    float maxDistance = RTXDI_BrdfMaxDistanceFromPdf(brdfCutoff, brdfPdf);
                    if (lightDistance > maxDistance)
                        lightIndex = RTXDI_InvalidLightIndex;
                }

                if (lightIndex != RTXDI_InvalidLightIndex)
                {
                    lightSourcePdf = RAB_EvaluateLocalLightSourcePdf(lightIndex);
                }
            }
            // else if (!hitAnything && (lightBufferParams.environmentLightParams.lightPresent != 0))
            // {
            //     // sample environment light
            //     lightIndex = lightBufferParams.environmentLightParams.lightIndex;
            //     RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
            //     randXY = RAB_GetEnvironmentMapRandXYFromDir(sampleDir);
            //     candidateSample = RAB_SamplePolymorphicLight(lightInfo, surface, randXY);
            //     lightSourcePdf = RAB_EvaluateEnvironmentMapSamplingPdf(sampleDir);
            // }
        }

        if (lightSourcePdf == 0)
        {
            // Did not hit a visible light
            continue;
        }

        bool isEnvMapSample = lightIndex == lightBufferParams.environmentLightParams.lightIndex;
        float targetPdf = RAB_GetLightSampleTargetPdfForSurface(candidateSample, surface);
        float blendedSourcePdf = RTXDI_LightBrdfMisWeight(surface, candidateSample, lightSourcePdf,
            isEnvMapSample ? misData.environmentMapMisWeight : misData.localLightMisWeight, 
            isEnvMapSample,
            misData.brdfMisWeight, brdfCutoff);
        float risRnd = RTXDI_GetNextRandom(rng);

        bool selected = RTXDI_StreamSample(state, lightIndex, randXY, risRnd, targetPdf, 1.0f / blendedSourcePdf);
        if (selected) {
            o_selectedSample = candidateSample;
        }
    }

    RTXDI_FinalizeResampling(state, 1.0, misData.numMisSamples);
    state.M = 1;

    return state;
}

float CalcDiffLobeProbability(float3 f0,float metallic, float3 base_color)
{
    float diffuse_weight =Luminance(base_color) *(1.0 - metallic);
    float specular_weight = Luminance(f0);
    float sum = max(diffuse_weight + specular_weight, 1e-5);
    return diffuse_weight / sum;
}

#define RTXDI_ALLOWED_BIAS_CORRECTION 0
// This macro can be defined in the including shader file to reduce code bloat
// and/or remove ray tracing calls from temporal and spatial resampling shaders
// if bias correction is not necessary.
#ifndef RTXDI_ALLOWED_BIAS_CORRECTION
#define RTXDI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_RAY_TRACED
#endif


// Spatio-temporal resampling pass.
// A combination of the temporal and spatial passes that operates only on the previous frame reservoirs.
// The selectedLightSample parameter is used to update and return the selected sample; it's optional,
// and it's safe to pass a null structure there and ignore the result.
RTXDI_DIReservoir RTXDI_DISpatioTemporalResampling(
    uint2 pixelPosition,
    RAB_Surface surface,
    RTXDI_DIReservoir curSample,
    inout RTXDI_RandomSamplerState rng,
    float3 screenSpaceMotion,
    uint sourceBufferIndex,
    RTXDI_RuntimeParameters params,
    RTXDI_ReservoirBufferParameters reservoirParams,
    RTXDI_DISpatioTemporalResamplingParameters stparams,
    out int2 temporalSamplePixelPos,
    inout RAB_LightSample selectedLightSample)
{
    // if (stparams.biasCorrectionMode == RTXDI_BIAS_CORRECTION_PAIRWISE)
    // {
    //     return RTXDI_DISpatioTemporalResamplingWithPairwiseMIS(pixelPosition, surface,
    //         curSample, rng, screenSpaceMotion, sourceBufferIndex, params, reservoirParams, stparams, temporalSamplePixelPos, selectedLightSample);
    // }

    uint historyLimit = min(RTXDI_PackedDIReservoir_MaxM, uint(stparams.maxHistoryLength * curSample.M));

    int selectedLightPrevID = -1;

    // if (RTXDI_IsValidDIReservoir(curSample))
    // {
    //     selectedLightPrevID = RAB_TranslateLightIndex(RTXDI_GetDIReservoirLightIndex(curSample), true);
    // }

    temporalSamplePixelPos = int2(-1, -1);

    RTXDI_DIReservoir state = RTXDI_EmptyDIReservoir();
    RTXDI_CombineDIReservoirs(state, curSample, /* random = */ 0.5, curSample.targetPdf);

    uint startIdx = uint(RTXDI_GetNextRandom(rng) * params.neighborOffsetMask);

    // Backproject this pixel to last frame
    float3 motion = screenSpaceMotion;

    if (!stparams.enablePermutationSampling)
    {
        motion.xy += float2(RTXDI_GetNextRandom(rng), RTXDI_GetNextRandom(rng)) - 0.5;
    }

    float2 reprojectedSamplePosition = float2(pixelPosition) + motion.xy;
    int2 prevPos = int2(round(reprojectedSamplePosition));

    float expectedPrevLinearDepth = RAB_GetSurfaceLinearDepth(surface) + motion.z;

    int i;

    RAB_Surface temporalSurface = RAB_EmptySurface();
    bool foundTemporalSurface = false;
    const float temporalSearchRadius = (params.activeCheckerboardField == 0) ? 4 : 8;
    int2 temporalSpatialOffset = int2(0, 0);

    // // Try to find a matching surface in the neighborhood of the reprojected pixel
    // for (i = 0; i < 9; i++)
    // {
    //     int2 offset = int2(0, 0);
    //     if (i > 0)
    //     {
    //         offset.x = int((RTXDI_GetNextRandom(rng) - 0.5) * temporalSearchRadius);
    //         offset.y = int((RTXDI_GetNextRandom(rng) - 0.5) * temporalSearchRadius);
    //     }

    //     int2 idx = prevPos + offset;

    //     // if (stparams.enablePermutationSampling && i == 0)
    //     // {
    //     //     RTXDI_ApplyPermutationSampling(idx, stparams.uniformRandomNumber);
    //     // }

    //     //RTXDI_ActivateCheckerboardPixel(idx, true, params.activeCheckerboardField);

    //     // Grab shading / g-buffer data from last frame
    //     temporalSurface = RAB_GetGBufferSurface(idx, true);
    //     if (!RAB_IsSurfaceValid(temporalSurface))
    //         continue;
        
    //     // Test surface similarity, discard the sample if the surface is too different.
    //     if (!RTXDI_IsValidNeighbor(
    //         RAB_GetSurfaceNormal(surface), RAB_GetSurfaceNormal(temporalSurface), 
    //         expectedPrevLinearDepth, RAB_GetSurfaceLinearDepth(temporalSurface), 
    //         stparams.normalThreshold, stparams.depthThreshold))
    //         continue;

    //     temporalSpatialOffset = idx - prevPos;
    //     foundTemporalSurface = true;
    //     break;
    // }

    // // Clamp the sample count at 32 to make sure we can keep the neighbor mask in an uint (cachedResult)
    // uint numSamples = clamp(stparams.numSamples, 1, 32);

    // // Apply disocclusion boost if there is no temporal surface
    // if (!foundTemporalSurface)
    //     numSamples = clamp(stparams.numDisocclusionBoostSamples, numSamples, 32);

    // // We loop through neighbors twice.  Cache the validity / edge-stopping function
    // //   results for the 2nd time through.
    // uint cachedResult = 0;

    // // Since we're using our bias correction scheme, we need to remember which light selection we made
    // int selected = -1;

    // // Walk the specified number of neighbors, resampling using RIS
    // for (i = 0; i < numSamples; ++i)
    // {
    //     int2 spatialOffset, idx;

    //     // Get screen-space location of neighbor
    //     if (i == 0 && foundTemporalSurface)
    //     {
    //         spatialOffset = temporalSpatialOffset;
    //         idx = prevPos + spatialOffset;
    //     }
    //     else
    //     {
    //         uint sampleIdx = (startIdx + i) & params.neighborOffsetMask;
    //         spatialOffset = int2(float2(RTXDI_NEIGHBOR_OFFSETS_BUFFER[sampleIdx].xy) * stparams.samplingRadius);

    //         idx = prevPos + spatialOffset;

    //         idx = RAB_ClampSamplePositionIntoView(idx, true);

    //         RTXDI_ActivateCheckerboardPixel(idx, true, params.activeCheckerboardField);

    //         temporalSurface = RAB_GetGBufferSurface(idx, true);

    //         if (!RAB_IsSurfaceValid(temporalSurface))
    //             continue;

    //         if (!RTXDI_IsValidNeighbor(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceNormal(temporalSurface), 
    //             RAB_GetSurfaceLinearDepth(surface), RAB_GetSurfaceLinearDepth(temporalSurface), 
    //             stparams.normalThreshold, stparams.depthThreshold))
    //             continue;

    //         if (stparams.enableMaterialSimilarityTest && !RAB_AreMaterialsSimilar(RAB_GetMaterial(surface), RAB_GetMaterial(temporalSurface)))
    //             continue;
    //     }
        
    //     cachedResult |= (1u << uint(i));

    //     uint2 neighborReservoirPos = RTXDI_PixelPosToReservoirPos(idx, params.activeCheckerboardField);

    //     RTXDI_DIReservoir prevSample = RTXDI_LoadDIReservoir(reservoirParams,
    //         neighborReservoirPos, sourceBufferIndex);

    //     if (RTXDI_IsValidDIReservoir(prevSample))
    //     {
    //         if (stparams.discountNaiveSamples && prevSample.M <= RTXDI_NAIVE_SAMPLING_M_THRESHOLD)
    //             continue;
    //     }

    //     prevSample.M = min(prevSample.M, historyLimit);
    //     prevSample.spatialDistance += spatialOffset;
    //     prevSample.age += 1;

    //     uint originalPrevLightID = RTXDI_GetDIReservoirLightIndex(prevSample);

    //     // Map the light ID from the previous frame into the current frame, if it still exists
    //     if (RTXDI_IsValidDIReservoir(prevSample))
    //     {   
    //         if (i == 0 && foundTemporalSurface && prevSample.age <= 1)
    //         {
    //             temporalSamplePixelPos = idx;
    //         }

    //         int mappedLightID = RAB_TranslateLightIndex(RTXDI_GetDIReservoirLightIndex(prevSample), false);

    //         if (mappedLightID < 0)
    //         {
    //             // Kill the reservoir
    //             prevSample.weightSum = 0;
    //             prevSample.lightData = 0;
    //         }
    //         else
    //         {
    //             // Sample is valid - modify the light ID stored
    //             prevSample.lightData = mappedLightID | RTXDI_DIReservoir_LightValidBit;
    //         }
    //     }

        RAB_LightInfo candidateLight;
        RTXDI_DIReservoir prevSample = RTXDI_LoadDIReservoir(_prev_reservoir_buffer_handle,PixelToLinearIndex(pixelPosition));
        // Load that neighbor's RIS state, do resampling
        float neighborWeight = 0;
        RAB_LightSample candidateLightSample = RAB_EmptyLightSample();
        if (RTXDI_IsValidDIReservoir(prevSample))
        {
            prevSample.M = min(prevSample.M, historyLimit);
            // candidateLight = LightIn
            
            // candidateLightSample = RAB_SamplePolymorphicLight(
            //     candidateLight, surface, RTXDI_GetDIReservoirSampleUV(prevSample));
            uint light_index = RTXDI_GetDIReservoirLightIndex(prevSample);
            float2 uv = RTXDI_GetDIReservoirSampleUV(prevSample);
            LightSample local_sample = SampleUnifiedLight(light_index,surface.worldPos, surface.normal, uv);
            candidateLightSample = LightSampleToRAB_LightSample(local_sample, surface.worldPos);
            
            neighborWeight = RAB_GetLightSampleTargetPdfForSurface(candidateLightSample, surface);
        }

        if (RTXDI_CombineDIReservoirs(state, prevSample, RTXDI_GetNextRandom(rng), neighborWeight))
        {
            // selected = i;
            // selectedLightPrevID = int(originalPrevLightID);
            selectedLightSample = candidateLightSample;
        }
    // }

    if (RTXDI_IsValidDIReservoir(state))
    {
#if RTXDI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_BASIC
        if (stparams.biasCorrectionMode >= RTXDI_BIAS_CORRECTION_BASIC)
        {
            // Compute the unbiased normalization term (instead of using 1/M)
            float pi = state.targetPdf;
            float piSum = state.targetPdf * curSample.M;

            if (selectedLightPrevID >= 0)
            {
                const RAB_LightInfo selectedLightPrev = RAB_LoadLightInfo(selectedLightPrevID, true);

                // To do this, we need to walk our neighbors again
                for (i = 0; i < numSamples; ++i)
                {
                    // If we skipped this neighbor above, do so again.
                    if ((cachedResult & (1u << uint(i))) == 0) continue;

                    uint sampleIdx = (startIdx + i) & params.neighborOffsetMask;

                    // Get the screen-space location of our neighbor
                    int2 spatialOffset = (i == 0 && foundTemporalSurface) 
                        ? temporalSpatialOffset 
                        : int2(float2(RTXDI_NEIGHBOR_OFFSETS_BUFFER[sampleIdx].xy) * stparams.samplingRadius);
                    int2 idx = prevPos + spatialOffset;

                    if (!(i == 0 && foundTemporalSurface))
                    {
                        idx = RAB_ClampSamplePositionIntoView(idx, true);
                    }

                    RTXDI_ActivateCheckerboardPixel(idx, true, params.activeCheckerboardField);

                    // Load our neighbor's G-buffer
                    RAB_Surface neighborSurface = RAB_GetGBufferSurface(idx, true);
                    
                    // Get the PDF of the sample RIS selected in the first loop, above, *at this neighbor* 
                    const RAB_LightSample selectedSampleAtNeighbor = RAB_SamplePolymorphicLight(
                        selectedLightPrev, neighborSurface, RTXDI_GetDIReservoirSampleUV(state));

                    float ps = RAB_GetLightSampleTargetPdfForSurface(selectedSampleAtNeighbor, neighborSurface);

#if RTXDI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_RAY_TRACED
                                                                                                              // TODO:  WHY?
                    if (stparams.biasCorrectionMode == RTXDI_BIAS_CORRECTION_RAY_TRACED && ps > 0 && (selected != i || i != 0 || !stparams.enableVisibilityShortcut))
                    {
                        RAB_Surface fallbackSurface;
                        if (i == 0 && foundTemporalSurface)
                            fallbackSurface = surface;
                        else
                            fallbackSurface = neighborSurface;

                        if (!RAB_GetTemporalConservativeVisibility(fallbackSurface, neighborSurface, selectedSampleAtNeighbor))
                        {
                            ps = 0;
                        }
                    }
#endif

                    uint2 neighborReservoirPos = RTXDI_PixelPosToReservoirPos(idx, params.activeCheckerboardField);

                    RTXDI_DIReservoir prevSample = RTXDI_LoadDIReservoir(reservoirParams,
                        neighborReservoirPos, sourceBufferIndex);
                    prevSample.M = min(prevSample.M, historyLimit);

                    // Select this sample for the (normalization) numerator if this particular neighbor pixel
                    //     was the one we selected via RIS in the first loop, above.
                    pi = selected == i ? ps : pi;

                    // Add to the sums of weights for the (normalization) denominator
                    piSum += ps * prevSample.M;
                }
            }

            // Use "MIS-like" normalization
            RTXDI_FinalizeResampling(state, pi, piSum);
        }
        else
#endif
        {
            RTXDI_FinalizeResampling(state, 1.0, state.M);
        }
    }

    return state;
}


TEXTURE2D(_GBuffer0)
TEXTURE2D(_GBuffer1)
TEXTURE2D(_GBuffer2)
TEXTURE2D(_GBuffer3)
TEXTURE2D(_CameraDepthTexture)


[shader("compute")]
[numthreads(16,16,1)]
void RayGen(CSInput input)
{
    uint2 pixel = input.DispatchThreadID.xy + uint2(_GI_TileOffset);
    if (pixel.x >= (_GI_TileOffset.x + _GI_TileSize.x) || pixel.y >= (_GI_TileOffset.y + _GI_TileSize.y))
        return;
    RandomCtx ctx;
    InitSeed(ctx, pixel + _FrameIndex, _FrameIndex);
    float2 uv = (pixel + 0.5) * _ScreenParams.xy;
    float2 jitter = rand2(ctx); 
    jitter = (jitter * 2.0 - 1.0) * 0.5;
    //uv += jitter * _ScreenParams.xy;
    float2 gbuf0 = SAMPLE_TEXTURE2D_LOD(_GBuffer0, g_LinearClampSampler, uv,0).xy;
	float4 gbuf1 = SAMPLE_TEXTURE2D_LOD(_GBuffer1, g_LinearClampSampler, uv,0);
	float4 gbuf2 = SAMPLE_TEXTURE2D_LOD(_GBuffer2, g_LinearClampSampler, uv,0);
	half4 emssion = SAMPLE_TEXTURE2D_LOD(_GBuffer3, g_LinearClampSampler, uv,0);
	float depth = SAMPLE_TEXTURE2D_LOD(_CameraDepthTexture,g_LinearClampSampler,uv,0).r;
	float3 world_pos = ComputeWorldSpacePosition(uv,depth,_MatrixIVP);
	//float3 world_pos = _CameraPos.xyz + UnprojectByCameraRay(input.uv,depth);
	SurfaceData surface_data;
	surface_data.wnormal = UnpackNormal(gbuf0);
	surface_data.albedo = float4(gbuf1.rgb,1.0f);
	surface_data.roughness = gbuf1.w;
	surface_data.metallic = gbuf2.w;
	surface_data.specular = 1.0.xxx;
	surface_data.anisotropy = gbuf2.z;
	surface_data.emssive = emssion.rgb;
	OrthonormalBasis(surface_data.wnormal,surface_data.tangent,surface_data.bitangent);
    Surface surface;
    surface._position = world_pos;
    surface._normal = surface_data.wnormal;
    surface._albedo = surface_data.albedo.rgb;
    surface._roughness = surface_data.roughness;
    surface._metallic = surface_data.metallic;
    surface._geo_normal = surface_data.wnormal;
    surface._padding = surface_data.anisotropy;
    surface._linear_depth = Linear01Depth(depth,_ProjectionParams.y,_ProjectionParams.z);
    float3 shadingOutput = 0;
    RTXDI_DIReservoir reservoir = RTXDI_EmptyDIReservoir();
    if (IsValidSurface(surface))
    {
        RTXDI_LightBufferParameters lightBufferParams = (RTXDI_LightBufferParameters)0;
        lightBufferParams.localLightBufferRegion.firstLightIndex = 0;
        lightBufferParams.localLightBufferRegion.numLights = GetUnifiedLightCount();

        // Initialize the RNG
        RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixel, _FrameIndex, 1);
        RTXDI_DIInitialSamplingParameters initialSamplingParams = (RTXDI_DIInitialSamplingParameters)0;
        initialSamplingParams.numBrdfSamples = _brdf_sample_count;
        initialSamplingParams.numLocalLightSamples = _light_sample_count;
		RTXDI_InitialSamplingMisData misData = RTXDI_ComputeInitialSamplingMisData(initialSamplingParams);

        // Generate the initial sample
        RAB_LightSample lightSample = RAB_EmptyLightSample();
        RAB_Material rab_mat;
        rab_mat.diffuseAlbedo = surface_data.albedo.rgb * (1.0 - surface_data.metallic);
        rab_mat.emissiveColor = surface_data.emssive;
        rab_mat.roughness = surface_data.roughness;
        rab_mat.specularF0 = lerp(0.04,rab_mat.diffuseAlbedo,surface_data.metallic);
        RAB_Surface rab_surface;
        rab_surface.material = rab_mat;
        rab_surface.normal = surface._normal;
        rab_surface.viewDir = normalize(_CameraPos.xyz - surface._position);
        rab_surface.worldPos = surface._position;
        rab_surface.viewDepth = surface._linear_depth;
        rab_surface.diffuseProbability = CalcDiffLobeProbability(rab_mat.specularF0, surface_data.metallic, rab_mat.diffuseAlbedo);
        RTXDI_DIReservoir localReservoir = RTXDI_SampleLocalLights(rng, rng, rab_surface,
            initialSamplingParams, misData, 0, lightBufferParams.localLightBufferRegion, lightSample);
        RTXDI_CombineDIReservoirs(reservoir, localReservoir, 0.5, localReservoir.targetPdf);

        // Resample BRDF samples.
        RAB_LightSample brdfSample = RAB_EmptyLightSample();
        RTXDI_DIReservoir brdfReservoir = RTXDI_SampleBrdf(rng, rab_surface, initialSamplingParams.numBrdfSamples, initialSamplingParams.brdfCutoff, 0.001f, 
        misData, rng, lightBufferParams, brdfSample);
        bool selectBrdf = RTXDI_CombineDIReservoirs(reservoir, brdfReservoir, RTXDI_GetNextRandom(rng), brdfReservoir.targetPdf);
        if (selectBrdf)
        {
            lightSample = brdfSample;
        }

        RTXDI_FinalizeResampling(reservoir, 1.0, 1.0);
        reservoir.M = 1;

        if (_enable_resampling)
        {
            RTXDI_RuntimeParameters params = (RTXDI_RuntimeParameters)0;
            RTXDI_ReservoirBufferParameters reservoirParams = (RTXDI_ReservoirBufferParameters)0;
            RTXDI_DISpatioTemporalResamplingParameters stparams = (RTXDI_DISpatioTemporalResamplingParameters)0;
            stparams.maxHistoryLength = 10;
            float3 screenSpaceMotion = float3(0, 0, 0);
            int2 temporalSamplePixelPos;
            reservoir = RTXDI_DISpatioTemporalResampling(
                pixel,
                rab_surface,
                reservoir,
                rng,
                screenSpaceMotion,
                _prev_reservoir_buffer_handle,
                params,
                reservoirParams,
                stparams,
                temporalSamplePixelPos,
                lightSample);
        }

        if (RTXDI_IsValidDIReservoir(reservoir))
        {
            float3 light_dir = lightSample.position - rab_surface.worldPos;
            float shadow_ray_tmax = length(light_dir) - 0.001;
            light_dir = normalize(light_dir);

            bool is_occluded = IsOccluded(rab_surface.worldPos + 0.001 * rab_surface.normal, light_dir, shadow_ray_tmax);
            if (!is_occluded)
                shadingOutput = ShadeSurfaceWithLightSample(lightSample, rab_surface) * RTXDI_GetDIReservoirInvPdf(reservoir);
            shadingOutput += surface_data.emssive;
            _GI_Texture[pixel] = float4(shadingOutput,1.0);
        }
    }
    else
    {
        _GI_Texture[pixel] = 0.0.xxxx;
    }
    RTXDI_StoreDIReservoir(reservoir,_curr_reservoir_buffer_handle, PixelToLinearIndex(pixel));
}