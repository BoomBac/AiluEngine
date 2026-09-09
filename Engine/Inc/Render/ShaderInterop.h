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

#if defined(__cplusplus) || defined(AL_SHADER_INTEROP_CBUFFER_AS_STRUCT)
    #define AL_SHADER_INTEROP_CBUFFER_BEGIN(name, slot) struct name
    #define AL_SHADER_INTEROP_CBUFFER_END ;
#else
    #define AL_SHADER_INTEROP_CBUFFER_BEGIN(name, slot) cbuffer name : register(slot)
    #define AL_SHADER_INTEROP_CBUFFER_END
#endif

#define AL_UNIFIED_LIGHT_TYPE_DIRECTIONAL 0u
#define AL_UNIFIED_LIGHT_TYPE_POINT 1u
#define AL_UNIFIED_LIGHT_TYPE_SPOT 2u
#define AL_UNIFIED_LIGHT_TYPE_RECT_AREA 3u
#define AL_UNIFIED_LIGHT_TYPE_TRIANGLE_AREA 4u

#define AL_UNIFIED_LIGHT_FLAG_TWO_SIDED 0x1u
// C++
#ifdef __cplusplus
namespace Ailu::Render
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
        int _shadowmap_index;
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
        int _shadowmap_index;
        float _ShadowDistance;
        float _constant_bias;
        float _slope_bias;
        float4x4 _shadow_matrix;
    };
    struct ShaderArealLightData
    {
        float4 _points[4];
        float3 _LightColor;
        int _is_twosided;
        int _shadowmap_index;
        float _ShadowDistance;
        float _constant_bias;
        float _slope_bias;
        float4x4 _shadow_matrix;
    };

    struct UnifiedLightData
    {
        uint _type;
        uint _flags;
        int _shadow_index;
        int _emissive_map;

        float3 _radiance;
        float _range;

        float3 _position;
        float _source_radius;

        float3 _direction;
        float _spot_angle_scale;

        float3 _shape_u;
        float _spot_angle_offset;

        float3 _shape_v;
        uint  _tri_index;

        float4 _shadow_params;
    };

    struct UnifiedLightBufferConfig
    {
        uint _light_count;
        uint _finite_light_count;
        uint _triangle_light_count;
        uint _reserved0;
    };

#ifdef __cplusplus
    enum EPrimitiveFlags : uint
    {
        kPrimitiveNone = 0u,
        kPrimitivePerObjectMotion = 1u << 0u,
        kPrimitiveForceZeroMotion = 1u << 1u,
        kPrimitiveSkinned = 1u << 2u
    };
#else
    static const uint kPrimitiveNone = 0u;
    static const uint kPrimitivePerObjectMotion = 1u << 0u;
    static const uint kPrimitiveForceZeroMotion = 1u << 1u;
    static const uint kPrimitiveSkinned = 1u << 2u;
#endif

#ifdef __cplusplus
    enum EPrimitiveDrawFlags : uint
    {
        kPrimitiveDrawNone = 0u,
        kPrimitiveDrawUseInstanceIndex = 1u << 0u
    };
#else
    static const uint kPrimitiveDrawNone = 0u;
    static const uint kPrimitiveDrawUseInstanceIndex = 1u << 0u;
#endif

    struct PrimitiveData
    {
        float4x4 _local_to_world;
        float4x4 _world_to_local;
        float4x4 _prev_local_to_world;
        float _max_inv_scale;
        uint _entity_id;
        uint _material_id;
        uint _submesh_id;
        uint _flags;
        uint _global_triangle_offset;
        uint _blas_node_start;
        uint _blas_node_count;
        uint _position_bindless_idx;
        uint _normal_bindless_idx;
        uint _uv_bindless_idx;
        uint _tangent_bindless_idx;
        uint _index_bindless_idx;
        uint _submesh_triangle_offset;
        uint _submesh_triangle_count;
        uint _reserved0;
    };

#if defined(__cplusplus) || defined(AL_SCENE_PRIMITIVE)
    AL_SHADER_INTEROP_CBUFFER_BEGIN(CBufferPrimitiveDrawData, b0)
    {
        uint _primitive_base;
        uint _instance_index_offset;
        uint _flags;
        uint _reserved0;
    } AL_SHADER_INTEROP_CBUFFER_END
#endif

    struct TriangleData
    {
        float3 v0;
        float3 v1;
        float3 v2;
        float3 n0;
        float3 n1;
        float3 n2;
        float2 uv0;
        float2 uv1;
        float2 uv2;
    };

    struct MaterialData
    {
        // -------------------------------------------------
        // Base parameters
        // -------------------------------------------------

        float3 _base_color;          // albedo / baseColor
        float  _metallic;            // [0,1]

        float3 _emission;            // radiance (W·sr⁻¹·m⁻²)
        float  _emission_strength;

        float  _roughness;           // perceptual roughness [0,1]
        float  _specular;            // specular weight (dielectric F0 control)
        float _anisotropy;          // anisotropy [0,1], 0 is isotropic, 1 is fully anisotropic
        float  _ior;                 // index of refraction (1.0 ~ 2.5)
        float  _transmission;             // 1 = opaque, <1 transmission

        // -------------------------------------------------
        // Texture indices (bindless / array)
        // -------------------------------------------------

        uint _base_color_tex;
        uint _normal_tex;
        uint _metallic_roughness_tex;
        uint _emission_tex;

        uint _opacity_tex;
        uint _reserved0;
        uint _reserved1;
        uint _flags;                 // MaterialFlags
    };

    
    struct LBVHNode
    {
        float3 _min;
        float _left_or_tri_offset_or_inst_idx;
        float3 _max;
        float _neg_right_or_tri_count;
    };

#if !defined(AL_SCENE_PRIMITIVE)
    AL_SHADER_INTEROP_CBUFFER_BEGIN(CBufferPerObjectData, b0)
    {
        float4x4 _MatrixWorld;
        float4x4 _MatrixInvWorld;
        float4x4 _MatrixWorld_Pre;
        //x is dynamic,y is force off
        float4 _MotionVectorParam;
        uint _ObjectID;
        uint _SubmeshID;
        float _cbo_paddings[10];// Padding so the constant buffer is 256-byte aligned.
    } AL_SHADER_INTEROP_CBUFFER_END
#endif

    AL_SHADER_INTEROP_CBUFFER_BEGIN(CBufferPerSceneData, b2)
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
        float4x4 _CascadeShadowMatrix[kMaxCascadeShadowMapSplit];
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
    } AL_SHADER_INTEROP_CBUFFER_END

    AL_SHADER_INTEROP_CBUFFER_BEGIN(CBufferPerCameraData, b3)
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
        float  _matrix_jitter_x;
        float3 _RT;
        float _matrix_jitter_y;
        float3 _LB;
        float _uv_jitter_x;
        float3 _RB;
        float _uv_jitter_y;
    } AL_SHADER_INTEROP_CBUFFER_END

#ifdef __cplusplus
    static_assert((sizeof(PrimitiveData) % 16) == 0, "PrimitiveData must preserve StructuredBuffer element alignment");
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
    static_assert(sizeof(CBufferPrimitiveDrawData) == 16, "Primitive draw root constants must be 16 bytes");
#if !defined(AL_SCENE_PRIMITIVE)
    static_assert((sizeof(CBufferPerObjectData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");
#endif
    static_assert((sizeof(CBufferPerSceneData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");
    //static_assert((sizeof(ScenePerMaterialData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");
    static_assert((sizeof(CBufferPerCameraData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");
    static_assert((sizeof(UnifiedLightData) % 16) == 0, "Unified light data must be 16-byte aligned");
    static_assert((sizeof(UnifiedLightBufferConfig) % 16) == 0, "Unified light config must be 16-byte aligned");
#else
#define PerMaterialCBufferBegin                   \
    cbuffer CBufferPerMaterialData : register(b1) \
    {
#define PerMaterialCBufferEnd }
#endif//__cplusplus


#ifdef __cplusplus
}
#endif//__cplusplus


#undef AL_SHADER_INTEROP_CBUFFER_BEGIN
#undef AL_SHADER_INTEROP_CBUFFER_END


#endif// !COMMON_CBUFFER__
