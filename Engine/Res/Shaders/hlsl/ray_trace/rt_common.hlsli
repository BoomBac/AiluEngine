#ifndef RT_COMMON_HLSLI
#define RT_COMMON_HLSLI

struct DebugRay
{
    float3 pos;
    uint color;
    int aabb_idx;
    uint3 padding;
};
static const uint kMaxDepth = 9;

#endif//RT_COMMON_HLSLI