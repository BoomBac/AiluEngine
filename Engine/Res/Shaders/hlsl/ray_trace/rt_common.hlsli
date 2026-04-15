#ifndef RT_COMMON_HLSLI
#define RT_COMMON_HLSLI

#include "../cbuffer.hlsli"

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
#define LIGHT_TYPE_DIRECTIONAL AL_UNIFIED_LIGHT_TYPE_DIRECTIONAL
#define LIGHT_TYPE_POINT AL_UNIFIED_LIGHT_TYPE_POINT
#define LIGHT_TYPE_SPOT AL_UNIFIED_LIGHT_TYPE_SPOT
#define LIGHT_TYPE_RECT_AREA AL_UNIFIED_LIGHT_TYPE_RECT_AREA
#define LIGHT_TYPE_TRIANGLE_AREA AL_UNIFIED_LIGHT_TYPE_TRIANGLE_AREA
struct SampleLightData
{
    float3 _color;
    uint   _type;
    float3 _position;
    float  _radius_or_sun_angle;
    float  _range;
    float3 _direction;
    float3 _light_u;
    float3 _light_v;
    uint   _instance_index;
    uint   _tri_index;
    uint   _emissive_map;
    bool _is_double_sided;
};
struct LightSample
{
    float3 _wi;
    float  _pdf;
    float3 _normal;
    float _t;
    float3 _radiance;
    float2 _uv;
};
#endif//RT_COMMON_HLSLI