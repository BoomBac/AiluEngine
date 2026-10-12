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
    // [0,1]：1 = 全部按玻璃折射；小于 1 时剩余部分按不透明表面着色。
    // 之前这里被当作布尔用（>0 即玻璃），0.64 的材质被当成 100% 玻璃，
    // 本该由那 36% 漫反射填亮的区域因此直接黑掉。
    float  _transmission;
    float _opacity;
    // RGB 体积吸收系数 (1/世界单位)，0 = 无损介质。
    float3 _sigma_a;
    float3 _emission;
    bool _is_glass;
    bool _is_smooth_dielectric;
};

// 本次求值/采样走的是哪个瓣
static const uint kBSDFLobeReflection = 0u;
static const uint kBSDFLobeTransmission = 1u;
// `_transmission < 1` 时玻璃材质里混合进来的那一份不透明表面着色
static const uint kBSDFLobeOpaque = 3u;

struct TraceContext
{
    Material _mat;
    float _eta;   //
    float _ior_i;
    float _ior_t;
    // 本次求值算的是哪个瓣。反射 h = normalize(wi + wo)，折射 h = normalize(wi * ior_t + wo * ior_i)。
    // EvalBRDF/SampleBRDF 必须用同一个瓣，否则求值和分布对不上。
    uint _lobe;
    // BSDF 采样时直接把采样到的半程向量带下来。
    // 近镜面玻璃的半程向量分布极窄，靠 (wo, wi) 反推在掠射和近法线折射时几乎退化成 0 向量，
    // 方向和符号都不可靠，会把整条折射贡献判掉。
    float3 _sampled_h;
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
    float  _pdf_area;
    float3 _normal;
    float _t;
    float3 _radiance;
    float3 _position;
    float2 _uv;
};
#endif//RT_COMMON_HLSLI
