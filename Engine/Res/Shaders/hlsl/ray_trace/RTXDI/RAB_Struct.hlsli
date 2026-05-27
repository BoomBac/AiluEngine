#if !defined(RAB_STRUCT_HLSLI)
#define RAB_STRUCT_HLSLI

struct RAB_Material
{
    float3 diffuseAlbedo;
    float3 specularF0;
    float roughness;
	float3 emissiveColor;
};

struct RAB_Surface
{
    float3 worldPos;
    float3 viewDir;
    float3 normal;
    float3 geoNormal;
    float viewDepth;
    float diffuseProbability;
    RAB_Material material;
};

struct RAB_LightInfo
{
    // uint4[0]
    float3 center;
    uint scalars; // 2x float16
    
    // uint4[1]
    uint2 radiance; // fp16x4
    uint direction1; // oct-encoded
    uint direction2; // oct-encoded
};

struct RAB_LightSample
{
    float3 position;
    float3 normal;
    float3 radiance;
    float solidAnglePdf;
};
#endif