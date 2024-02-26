#pragma once
#ifndef __D3DCONSTANTS__
#define __D3DCONSTANTS__
#include <cstdint>
#include "AlgFormat.h"
#include "CBuffer.h"

namespace Ailu
{
	DECLARE_ENUM(EColorRange, kLDR, kHDR)

	namespace RenderConstants
	{
		constexpr static uint8_t  kFrameCount = 2u;
		constexpr static uint16_t kMaxMaterialDataCount = 24u;
		constexpr static uint32_t kMaxRenderObjectCount = 50u;
		constexpr static uint32_t kMaxPassDataCount = 10u;
		constexpr static uint32_t kMaxTextureCount = 16u;
		constexpr static uint8_t  kMaxUAVTextureCount = 10U;
		constexpr static uint32_t kMaxRenderTextureCount = 16u;
		constexpr static uint32_t KMaxDynamicVertexNum = 2048;
		constexpr static uint8_t  kMaxVertexAttrNum = 10u;

		constexpr static uint32_t kPerFrameTotalSize = sizeof(ScenePerFrameData) + sizeof(ScenePerMaterialData) * kMaxMaterialDataCount + + sizeof(ScenePerPassData) * kMaxPassDataCount+
			sizeof(ScenePerObjectData) * kMaxRenderObjectCount;
		constexpr static uint32_t kPerFrameDataSize = sizeof(ScenePerFrameData);
		constexpr static uint32_t kPerMaterialDataSize = sizeof(ScenePerMaterialData);
		constexpr static uint32_t kPeObjectDataSize = sizeof(ScenePerObjectData);
		constexpr static uint32_t kPePassDataSize = sizeof(ScenePerPassData);
		constexpr static wchar_t kVSModel_5_0[] = L"vs_5_0";
		constexpr static wchar_t kPSModel_5_0[] = L"ps_5_0";
		constexpr static wchar_t kVSModel_6_1[] = L"vs_6_1";
		constexpr static wchar_t kPSModel_6_1[] = L"ps_6_1";

		inline const static char* kSemanticPosition = "POSITION";
		inline const static char* kSemanticColor = "COLOR";
		inline const static char* kSemanticTangent = "TANGENT";
		inline const static char* kSemanticNormal = "NORMAL";
		inline const static char* kSemanticTexcoord = "TEXCOORD";
		inline const static char* kSemanticBoneWeight = "BONEWEIGHT";
		inline const static char* kSemanticBoneIndex = "BONEINDEX";

		constexpr static char kCBufNameSceneObject[] = "ScenePerObjectData";
		constexpr static char kCBufNameSceneMaterial[] = "ScenePerMaterialData";
		constexpr static char kCBufNameSceneState[] = "ScenePerFrameData";
		constexpr static char kCBufNameScenePass[] = "ScenePerPassData";

		inline const static std::string kAlbdeoTexName = "TexAlbedo";
		inline const static std::string kNormalTexName = "TexNormal";
		inline const static std::string kEmssiveTexName = "TexEmssive";
		inline const static std::string kRoughnessTexName = "TexRoughness";

		inline const static EColorRange::EColorRange kColorRange = EColorRange::kHDR;
		inline const static EALGFormat kLDRFormat = EALGFormat::kALGFormatR8G8B8A8_UNORM;
		inline const static EALGFormat kHDRFormat = EALGFormat::kALGFormatR16G16B16A16_FLOAT;
	};

}


#endif // !D3DCONSTANTS__

