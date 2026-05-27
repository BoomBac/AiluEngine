#ifndef __COMMON_STRUCT_HLSLI__
#define __COMMON_STRUCT_HLSLI__

struct RTXDI_DIInitialSamplingParameters
{
    uint numLocalLightSamples;
    uint numInfiniteLightSamples;
    uint numEnvironmentSamples;
    uint numBrdfSamples;

    float brdfCutoff;
    float brdfRayMinT;
    //ReSTIRDI_LocalLightSamplingMode localLightSamplingMode;
    uint enableInitialVisibility;

    uint environmentMapImportanceSampling; // Only used in InitialSamplingFunctions.hlsli via RAB_EvaluateEnvironmentMapSamplingPdf
    uint pad1;
    uint pad2;
    uint pad3;
};

struct RTXDI_RandomSamplerState
{
    uint seed;
    uint index;
};

struct RTXDI_RuntimeParameters
{
    uint32_t neighborOffsetMask; // Spatial
    uint32_t activeCheckerboardField; // 0 - no checkerboard, 1 - odd pixels, 2 - even pixels
    uint32_t frameIndex;
    uint32_t pad2;
};

struct RTXDI_ReservoirBufferParameters
{
    uint reservoirBlockRowPitch;
    uint reservoirArrayPitch;
    uint pad1;
    uint pad2;
};
// Bias correction modes for temporal and spatial resampling:
// Use (1/M) normalization, which is very biased but also very fast.
#define RTXDI_BIAS_CORRECTION_OFF 0
// Use MIS-like normalization but assume that every sample is visible.
#define RTXDI_BIAS_CORRECTION_BASIC 1
// Use pairwise MIS normalization (assuming every sample is visible).  Better perf & specular quality
#define RTXDI_BIAS_CORRECTION_PAIRWISE 2
// Use MIS-like normalization with visibility rays. Unbiased.
#define RTXDI_BIAS_CORRECTION_RAY_TRACED 3

/*
enum class ReSTIRDI_SpatioTemporalBiasCorrectionMode : uint32_t
{
    Off = RTXDI_BIAS_CORRECTION_OFF,
    Basic = RTXDI_BIAS_CORRECTION_BASIC,
    Pairwise = RTXDI_BIAS_CORRECTION_PAIRWISE,
    Raytraced = RTXDI_BIAS_CORRECTION_RAY_TRACED
};
*/
struct RTXDI_DISpatioTemporalResamplingParameters
{
    // Common parameters, see RTXDI_DITemporal* or RTXDI_DISpatialResamplingParameters

    float depthThreshold;

    float normalThreshold;

    uint biasCorrectionMode;

    uint maxHistoryLength;

    // Temporal parameters, see RTXDI_DITemporalResamplingParameters

    uint enablePermutationSampling;

    uint uniformRandomNumber;

    uint enableVisibilityShortcut;

    // Spatial parameters, see RTXDI_DISpatialResamplingParameters

    uint numSamples;

    uint numDisocclusionBoostSamples;

    float samplingRadius;

    uint enableMaterialSimilarityTest;

    uint discountNaiveSamples;
};

#endif//__COMMON_STRUCT_HLSLI__