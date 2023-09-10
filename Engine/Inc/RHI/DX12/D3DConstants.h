#pragma once
#ifndef __D3DCONSTANTS__
#define __D3DCONSTANTS__
#include <cstdint>
#include "Framework/Math/ALMath.hpp"

namespace Ailu
{
	struct ScenePerFrameData
	{
		Matrix4x4f _MatrixV;
		Matrix4x4f _MatrixVP;
		float padding[32]; // Padding so the constant buffer is 256-byte aligned.
	};
	static_assert((sizeof(ScenePerFrameData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

	struct ScenePerMaterialData
	{
		float padding[64]; // Padding so the constant buffer is 256-byte aligned.
	};
	static_assert((sizeof(ScenePerMaterialData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

	struct ScenePerObjectData
	{
		Matrix4x4f _MatrixM;
		float padding[48]; // Padding so the constant buffer is 256-byte aligned.
	};
	static_assert((sizeof(ScenePerObjectData) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

	struct D3DConstants
	{
		constexpr static uint8_t  kFrameCount = 2u;
		constexpr static uint16_t kMaxMaterialDataCount = 4u;
		constexpr static uint32_t kMaxRenderObjectCount = 8u;

		constexpr static uint32_t kPerFrameTotalSize = sizeof(ScenePerFrameData) + sizeof(ScenePerMaterialData) * kMaxMaterialDataCount + sizeof(ScenePerObjectData) * kMaxRenderObjectCount;
		constexpr static uint32_t kPerFrameDataSize = sizeof(ScenePerFrameData);
		constexpr static uint32_t kPerMaterialDataSize = sizeof(ScenePerMaterialData);
		constexpr static uint32_t kPeObjectDataSize = sizeof(ScenePerObjectData);
	};
}


#endif // !D3DCONSTANTS__

