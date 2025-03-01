#ifndef __COMMON_CBUFFER__
#define __COMMON_CBUFFER__

#define kMaxDirectionalLight 2
#define kMaxPointLight 4
#define kMaxSpotLight 4
#define kMaxAreaLight 4
#define kMaxCascadeShadowMapSplit 4

#if defined(_REVERSED_Z)
    #define kZFar  0.0f
    #define kZNear 1.0f
#else
    #define kZFar  1.0f
    #define kZNear 0.0f
#endif

#ifdef __cplusplus
    #include "Framework/Math/ALMath.hpp"
    #define float2 Vector2f
    #define float3 Vector3f
    #define float4 Vector4f
    #define float4x4 Matrix4x4f
    #define int4 Vector4Int
    #define uint u32
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
    struct ShaderArealLightData
    {
        float4 _points[4];
        float3 _LightColor;
        bool _is_twosided;
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
        float4x4 _MatrixInvWorld;
        float4x4 _MatrixWorld_Pre;
        //x is dynamic,y is force off
        float4 _MotionVectorParam;
        int _ObjectID;
        float _cbo_paddings[11];// Padding so the constant buffer is 256-byte aligned.
    };

#ifdef __cplusplus
    struct CBufferPerSceneData
#else
cbuffer CBufferPerSceneData : register(b2)
#endif//__cplusplus
    {
        float3 _MainlightWorldPosition;
        uint   _FrameIndex;
        float3 _MainlightColor;
        float _cbs_padding1;
        float4 _CascadeShadowSplit[kMaxCascadeShadowMapSplit];//sphere center,radius * radius
        float4 _CascadeShadowParams;                          //cascade count,1/shadow_fade_out_factor,1/max_shadow_distance
        ShaderDirectionalAndPointLightData _DirectionalLights[kMaxDirectionalLight];
        ShaderDirectionalAndPointLightData _PointLights[kMaxPointLight];
        ShaderSpotlLightData _SpotLights[kMaxSpotLight];
        ShaderArealLightData _AreaLights[kMaxAreaLight];
        float4x4 _ShadowMatrix[kMaxCascadeShadowMapSplit + kMaxSpotLight + kMaxPointLight * 6];
        float4 _ActiveLightCount;
        float4 _Time;     // (t/20,t,t*2,t*3)
        float4 _SinTime;  // sin(t/8),sin(t/4),sin(t/2),sin(t)
        float4 _CosTime;  // cos(t/8),cos(t/4),cos(t/2),cos(t)
        float4 _DeltaTime;//(dt,1/dt,smoothDt,1/smoothDt)
        // x,y,z
        int4 g_VXGI_GridNum;
        //XYZ:center,w:max distance
        float4 g_VXGI_Center;
        //XYZ:cell size,w:min distance
        float4 g_VXGI_GridSize;
        float4 g_VXGI_Size;
        float g_IndirectLightingIntensity;
        float _cbs_paddings[23];
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
        float4x4 _MatrixVP;//can include jitter
        float4x4 _MatrixVP_NoJitter;
        float4x4 _MatrixIVP;//can include jitter
        float4x4 _MatrixVP_Pre;//no jitter
        float4 _CameraPos;
        //(1/w,1/h,w,h)
        float4 _ScreenParams;
        // x = 1-far/near
        // y = far/near
        // z = x/far
        // w = y/far
        // or in case of a reversed depth buffer (_REVERSED_Z is 1)
        // x = -1+far/near
        // y = 1
        // z = x/far
        // w = 1/far
        float4 _ZBufferParams;
        // x = 1 or -1 (-1 if projection is flipped) 详见https://www.jianshu.com/p/07ccf5ea1494，dx下值为1，这里不使用
        // y = near plane
        // z = far plane
        // w = 1/far plane
        float4 _ProjectionParams;
        float3 _LT;
        float _cbc_padding0;
        float3 _RT;
        float _cbc_padding1;
        float3 _LB;
        float _cbc_padding2;
        float3 _RB;
        float _cbc_padding3;
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
