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

struct Material
{
    float3 _albedo;
    float  _roughness;
    float  _metallic;
    float  _anisotropy;
    float  _ior;
    float  _transmission;
    float3 _emission;
    bool _is_glass;
};

struct TraceContext
{
    Material _mat;
    float _eta;   //
    float _ior_i;
    float _ior_t;
};

#endif//RT_COMMON_HLSLI