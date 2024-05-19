#ifndef __COMMON_CBUFFER__
#define __COMMON_CBUFFER__

#define kMaxDirectionalLightNum 2
#define kMaxPointLightNum 4
#define kMaxSpotLightNum 4

#ifdef __cplusplus
#include "Framework/Math/ALMath.hpp"
#define float2 Vector2f
#define float3 Vector3f
#define float4 Vector4f
#define float4x4 Matrix4x4f
#endif //__cplusplus
// C++
#ifdef __cplusplus
namespace Ailu
{
#endif //__cplusplus
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
#endif //__cplusplus
		float _LightParam0;
		float3 _LightColor;
		float _LightParam1;
		int _ShadowDataIndex;
		float _ShadowDistance;
		float2 _padding0;
	};

	struct ShaderSpotlLightData
	{
		float3  _LightDir;
		float	  _Rdius;
		float3  _LightPos;
		float	 _LightAngleScale;
		float3  _LightColor;
		float   _LightAngleOffset;
		int _ShadowDataIndex;
		float _ShadowDistance;
		float2 _padding0;
	};

#ifdef __cplusplus
	struct ScenePerObjectData
#else
	cbuffer ScenePerObjectData : register(b0)
#endif //__cplusplus
	{
		float4x4 _MatrixWorld;
		float padding0[48]; // Padding so the constant buffer is 256-byte aligned.
	};

#ifdef __cplusplus
	struct ScenePerFrameData
#else
	cbuffer ScenePerFrameData : register(b2)
#endif //__cplusplus
	{
		float4x4 _MatrixV;
		float4x4 _MatrixP;
		float4x4 _MatrixVP;
		float4x4 _MatrixIVP;
		float4   _CameraPos;
		ShaderDirectionalAndPointLightData _DirectionalLights[kMaxDirectionalLightNum];
		ShaderDirectionalAndPointLightData _PointLights[kMaxPointLightNum];
		ShaderSpotlLightData _SpotLights[kMaxSpotLightNum];
		float4x4 _ShadowMatrix[1 + kMaxSpotLightNum + kMaxPointLightNum];
		float padding1[36];
		//float4x4 _JointMatrix[80];
	};

#ifdef __cplusplus
	struct ScenePerPassData
#else
	cbuffer ScenePerPassData : register(b3)
#endif //__cplusplus
	{
		float4x4 _PassMatrixV;
		float4x4 _PassMatrixP;
		float4x4 _PassMatrixVP;
		float4   _PassCameraPos;
		float padding3[12];
	};

#ifdef __cplusplus
	struct ScenePerMaterialData
	{
		float4 _BaseColor;
		float	 _Roughness;
		float3 _Emssive;
		float	 _Metallic;
		float    _Specular;
		// low_bit: metallic|roughness|emssive|normal|albedo
		u32 _SamplerMask;
		u32 _MaterialID;
		float padding2[52]; // Padding so the constant buffer is 256-byte aligned.
	};
	static_assert((sizeof(ScenePerObjectData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");
	static_assert((sizeof(ScenePerFrameData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");
	static_assert((sizeof(ScenePerMaterialData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");
	static_assert((sizeof(ScenePerPassData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");
#else
#define CBufBegin cbuffer ScenePerMaterialData : register(b1) {
#define CBufEnd }
#endif //__cplusplus



#ifdef __cplusplus
}
#endif //__cplusplus

#endif // !COMMON_CBUFFER__
