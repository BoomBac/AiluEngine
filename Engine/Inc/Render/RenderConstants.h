#pragma once
#ifndef __D3DCONSTANTS__
#define __D3DCONSTANTS__
#include <cstdint>
#include "Framework/Math/ALMath.hpp"

namespace Ailu
{
	struct ShaderDirectionalAndPointLightData
	{
		union
		{
			Vector3f _LightDir;
			Vector3f _LightPos;
		};
		float _LightParam0;
		Vector3f _LightColor;
		float _LightParam1;
		ShaderDirectionalAndPointLightData() {}
	};

	struct ShaderSpotlLightData
	{
		Vector3f  _LightDir;
		float	  _Rdius;
		Vector3f  _LightPos;
		float	  _InnerAngle;
		Vector3f  _LightColor;
		float     _OuterAngle;
	};
	constexpr static uint16_t kMaxDirectionalLightNum = 2u;
	constexpr static uint16_t kMaxPointLightNum = 4u;
	constexpr static uint16_t kMaxSpotLightNum = 4u;

	struct ScenePerFrameData
	{
		Matrix4x4f _MatrixV;
		Matrix4x4f _MatrixP;
		Matrix4x4f _MatrixVP;
		Vector4f   _CameraPos;
		ShaderDirectionalAndPointLightData _DirectionalLights[kMaxDirectionalLightNum];
		ShaderDirectionalAndPointLightData _PointLights[kMaxPointLightNum];
		ShaderSpotlLightData _SpotLights[kMaxSpotLightNum];
		float padding[44];
	};

	static_assert((sizeof(ScenePerFrameData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

	struct ScenePerMaterialData
	{
		Vector4f _BaseColor;
		float	 _Roughness;
		Vector3f _Emssive;
		float	 _Metallic;
		float    _Specular;
		// low_bit: metallic|roughness|emssive|normal|albedo
		uint32_t _SamplerMask;
		float padding[53]; // Padding so the constant buffer is 256-byte aligned.
	};

	static_assert((sizeof(ScenePerMaterialData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

	struct ScenePerObjectData
	{
		Matrix4x4f _MatrixM;
		float padding[48]; // Padding so the constant buffer is 256-byte aligned.
	};
	static_assert((sizeof(ScenePerObjectData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

	struct RenderConstants
	{
		constexpr static uint8_t  kFrameCount = 2u;
		constexpr static uint16_t kMaxMaterialDataCount = 8u;
		constexpr static uint32_t kMaxRenderObjectCount = 8u;
		constexpr static uint32_t kMaxTextureCount = 8u;
		constexpr static uint32_t kMaxRenderTextureCount = 8u;
		constexpr static uint32_t KMaxDynamicVertexNum = 2048;
		constexpr static uint8_t kMaxVertexAttrNum = 10u;

		constexpr static uint32_t kPerFrameTotalSize = sizeof(ScenePerFrameData) + sizeof(ScenePerMaterialData) * kMaxMaterialDataCount + sizeof(ScenePerObjectData) * kMaxRenderObjectCount;
		constexpr static uint32_t kPerFrameDataSize = sizeof(ScenePerFrameData);
		constexpr static uint32_t kPerMaterialDataSize = sizeof(ScenePerMaterialData);
		constexpr static uint32_t kPeObjectDataSize = sizeof(ScenePerObjectData);
		constexpr static wchar_t kVSModel_5_0[] = L"vs_5_0";
		constexpr static wchar_t kPSModel_5_0[] = L"ps_5_0";
		constexpr static wchar_t kVSModel_6_1[] = L"vs_6_1";
		constexpr static wchar_t kPSModel_6_1[] = L"ps_6_1";

		inline const static char* kSemanticPosition = "POSITION";
		inline const static char* kSemanticColor = "COLOR";
		inline const static char* kSemanticTangent = "TANGENT";
		inline const static char* kSemanticNormal = "NORMAL";
		inline const static char* kSemanticTexcoord = "TEXCOORD";

		constexpr static char kCBufNameSceneObject[] = "SceneObjectBuffer";
		constexpr static char kCBufNameSceneMaterial[] = "SceneMaterialBuffer";
		constexpr static char kCBufNameSceneState[] = "SceneStatetBuffer";

		inline const static std::string kAlbdeoTexName = "TexAlbedo";
		inline const static std::string kNormalTexName = "TexNormal";
		inline const static std::string kEmssiveTexName = "TexEmssive";
		inline const static std::string kRoughnessTexName = "TexRoughness";
	};

}


#endif // !D3DCONSTANTS__

