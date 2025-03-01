#ifndef _CS_COMMON_H
#define _CS_COMMON_H
#include "../common.hlsli"
// SamplerState g_LinearWrapSampler : register(s0);
// SamplerState g_LinearClampSampler : register(s1);
// SamplerState g_LinearBorderSampler : register(s2);

struct CSInput
{
    uint3 GroupID           : SV_GroupID;           // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID     : SV_GroupThreadID;     // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID  : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex        : SV_GroupIndex;        // Flattened local index of the thread within a thread group.
};
#define VALID_INDEX(size)  if (input.DispatchThreadID.x >= (uint)size.x || input.DispatchThreadID.y >= (uint)size.y)\
        return;
#endif //_CS_COMMON_H