#pragma once
#ifndef __D3DCONSTANTS__
#define __D3DCONSTANTS__
#include <cstdint>
#include "RenderingData.h"

namespace Ailu
{
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

