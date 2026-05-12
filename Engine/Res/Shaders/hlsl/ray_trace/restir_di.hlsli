#ifndef _RESTIR_DI_HLSLI_
#define _RESTIR_DI_HLSLI_

#include "rt_common.hlsli"

struct TinyLightSample
{
    uint _light_idx;
    float3 _position;
    float3 _le;
};

struct Reservoir
{
    TinyLightSample y;   // 被选中的样本（只存一个）
    float w_sum;     // 所有候选样本权重和
    uint  M;         // 看过的候选数量
    float target;
};
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

Reservoir InitReservoir()
{
    Reservoir r;
    r.y = (TinyLightSample)0;
    r.w_sum = 0.0f;
    r.M = 0;
    r.target = 0.0f;
    return r;
}

bool UpdateReservoir(inout Reservoir r, TinyLightSample x,float target,float rand,float inv_pdf = 1.0f)
{
    float ris_weight = target * inv_pdf;
    r.M += 1;
    r.w_sum += ris_weight;
    bool select_sample = ris_weight > 0.0 && r.w_sum > 0.0 && rand < ris_weight / r.w_sum;
    if (select_sample)
    {
        r.y = x;
        r.target = target;
    }
    return select_sample;
}

void FinalizeResampling(
    inout Reservoir reservoir,
    float normalizationNumerator,
    float normalizationDenominator)
{
    float denominator = reservoir.target * normalizationDenominator;

    reservoir.w_sum = (denominator == 0.0) ? 0.0 : (reservoir.w_sum * normalizationNumerator) / denominator;
}

// Adds `newReservoir` into `reservoir`, returns true if the new reservoir's sample was selected.
// This is a very general form, allowing input parameters to specfiy normalization and targetPdf
// rather than computing them from `newReservoir`.  Named "internal" since these parameters take
// different meanings (e.g., in RTXDI_CombineDIReservoirs() or RTXDI_StreamNeighborWithPairwiseMIS())
bool RTXDI_InternalSimpleResample(
    inout Reservoir reservoir,
    const Reservoir newReservoir,
    float random,
    float targetPdf = 1.0f,            // Usually closely related to the sample normalization, 
    float sampleNormalization = 1.0f,  //     typically off by some multiplicative factor 
    float sampleM = 1.0f               // In its most basic form, should be newReservoir.M
)
{
    // What's the current weight (times any prior-step RIS normalization factor)
    float risWeight = targetPdf * sampleNormalization;

    // Our *effective* candidate pool is the sum of our candidates plus those of our neighbors
    reservoir.M += sampleM;

    // Update the weight sum
    reservoir.w_sum += risWeight;

    // Decide if we will randomly pick this sample
    bool selectSample = (random * reservoir.w_sum < risWeight);

    // If we did select this sample, update the relevant data
    if (selectSample)
    {
        reservoir.y = newReservoir.y;
        reservoir.target = targetPdf;
    }

    return selectSample;
}

// Adds `newReservoir` into `reservoir`, returns true if the new reservoir's sample was selected.
// Algorithm (4) from the ReSTIR paper, Combining the streams of multiple reservoirs.
// Normalization - Equation (6) - is postponed until all reservoirs are combined.
bool CombineDIReservoirs(
    inout Reservoir reservoir,
    const Reservoir newReservoir,
    float random,
    float targetPdf)
{
    return RTXDI_InternalSimpleResample(
        reservoir,
        newReservoir,
        random,
        targetPdf,
        newReservoir.w_sum * newReservoir.M,
        newReservoir.M
    );
}

bool CombineTemporalReservoir(inout Reservoir state,Reservoir history,float target,float random)
{
    if (history.M == 0)
        return false;

    float ris_weight =target *history.w_sum *history.M;

    state.M += history.M;
    state.w_sum += ris_weight;

    bool selected =ris_weight > 0.0 && random * state.w_sum < ris_weight;

    if (selected)
    {
        state.y = history.y;
        state.target = target;
    }

    return selected;
}


Reservoir ClampReservoirHistory(Reservoir r, uint maxM)
{
    if (r.M == 0 || r.w_sum <= 0.0)
        return InitReservoir();

    uint clampedM = min(r.M, maxM);
    if (clampedM == r.M)
        return r;

    float scale = (float)clampedM / (float)r.M;
    r.w_sum *= scale;
    r.M = clampedM;
    return r;
}
#endif//_RESTIR_DI_HLSLI_