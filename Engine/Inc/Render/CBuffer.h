#ifndef __COMMON_CBUFFER__
#define __COMMON_CBUFFER__

#define kMaxDirectionalLight 2
#define kMaxPointLight 4
#define kMaxSpotLight 4
#define kMaxCascadeShadowMapSplit 4


#ifdef __cplusplus
#include "Framework/Math/ALMath.hpp"
#define float2 Vector2f
#define float3 Vector3f
#define float4 Vector4f
#define float4x4 Matrix4x4f
#endif//__cplusplus
// C++
#ifdef __cplusplus
namespace Ailu
{
#endif//__cplusplus
    struct ShaderDirectionalAndPointLightData
    {
#ifdef __cplusplus
        ShaderDirectionalAndPointLightData() {};
        union
        {
            float3 _LightDir;
            float3 _LightPos;
        };
#else
    float3 _LightPosOrDir;
#endif//__cplusplus
        float _LightParam0;
        float3 _LightColor;
        float _LightParam1;
        int _ShadowDataIndex;
        float _ShadowDistance;
        float _constant_bias;
        float _slope_bias;
    };

    struct ShaderSpotlLightData
    {
        float3 _LightDir;
        float _Rdius;
        float3 _LightPos;
        float _LightAngleScale;
        float3 _LightColor;
        float _LightAngleOffset;
        int _ShadowDataIndex;
        float _ShadowDistance;
        float _constant_bias;
        float _slope_bias;
    };

#ifdef __cplusplus
    struct CBufferPerObjectData
#else
cbuffer CBufferPerObjectData : register(b0)
#endif//__cplusplus
    {
        float4x4 _MatrixWorld;
        float4x4 _MatrixWorldPreTick;
        float padding0[32];// Padding so the constant buffer is 256-byte aligned.
    };

#ifdef __cplusplus
    struct CBufferPerSceneData
#else
cbuffer CBufferPerSceneData : register(b2)
#endif//__cplusplus
    {
        float4 _MainlightWorldPosition;
        float4 _CascadeShadowSplit[kMaxCascadeShadowMapSplit];//sphere center,radius * radius
        float4 _CascadeShadowParams;                          //cascade count,1/shadow_fade_out_factor,1/max_shadow_distance
        ShaderDirectionalAndPointLightData _DirectionalLights[kMaxDirectionalLight];
        ShaderDirectionalAndPointLightData _PointLights[kMaxPointLight];
        ShaderSpotlLightData _SpotLights[kMaxSpotLight];
        float4x4 _ShadowMatrix[kMaxCascadeShadowMapSplit + kMaxSpotLight + kMaxPointLight * 6];
        float g_IndirectLightingIntensity;
        float padding1[31];
        //float4x4 _JointMatrix[80];
    };

#ifdef __cplusplus
    struct CBufferPerCameraData
#else
cbuffer CBufferPerCameraData : register(b3)
#endif//__cplusplus
    {
        float4x4 _MatrixV;
        float4x4 _MatrixP;
        float4x4 _MatrixVP;
        float4x4 _MatrixIVP;
        float4 _CameraPos;
        //float4(w,h,1/w,1/h)
        float4 _ScreenParams;
        float4 _ZBufferParams;
        float padding2[52];
    };

#ifdef __cplusplus
    //	struct ScenePerMaterialData
    //	{
    //		float4 _BaseColor;
    //		float	 _Roughness;
    //		float3 _Emssive;
    //		float	 _Metallic;
    //		float    _Specular;
    //		// low_bit: metallic|roughness|emssive|normal|albedo
    //		u32 _SamplerMask;
    //		u32 _MaterialID;
    //        float _AlphaCulloff;
    //		float padding2[51]; // Padding so the constant buffer is 256-byte aligned.
    //	};
    static_assert((sizeof(CBufferPerObjectData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");
    static_assert((sizeof(CBufferPerSceneData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");
    //static_assert((sizeof(ScenePerMaterialData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");
    static_assert((sizeof(CBufferPerCameraData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");
#else
#define PerMaterialCBufferBegin                   \
    cbuffer CBufferPerMaterialData : register(b1) \
    {
#define PerMaterialCBufferEnd }
#endif//__cplusplus


#ifdef __cplusplus
}
#endif//__cplusplus


#endif// !COMMON_CBUFFER__
