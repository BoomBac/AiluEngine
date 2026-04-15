#ifndef _RESTIR_DI_HLSLI_
#define _RESTIR_DI_HLSLI_

#include "rt_common.hlsli"

struct TinyLightSample
{
    uint _light_idx;
    float3 _position;
    float3 _le;
    float _pdf;//solid_angle_pdf * picking_pdf
    float _weight;
};

struct Reservoir
{
    TinyLightSample y;   // 被选中的样本（只存一个）
    float w_sum;     // 所有候选样本权重和
    uint  M;         // 看过的候选数量
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
    return r;
}

void UpdateReservoir(inout Reservoir r, TinyLightSample x, float w_x, uint M, float rand)
{
    r.M += M;
    r.w_sum += w_x;
    if (w_x > 0.0 && r.w_sum > 0.0 && rand < w_x / r.w_sum)
    {
        r.y = x;
    }
}

//// Adds `newReservoir` into `reservoir`, returns true if the new reservoir's sample was selected.
bool MergeReservoir(inout Reservoir reservoir, Reservoir newReservoir, float rand)
{
    if (newReservoir.M == 0 || newReservoir.w_sum <= 0.0f)
        return false;

    float w_x = newReservoir.w_sum;
    UpdateReservoir(reservoir, newReservoir.y, w_x, newReservoir.M, rand);
    return reservoir.y._light_idx == newReservoir.y._light_idx;
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