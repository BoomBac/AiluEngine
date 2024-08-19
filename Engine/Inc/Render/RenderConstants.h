#pragma once
#ifndef __D3DCONSTANTS__
#define __D3DCONSTANTS__
#include "AlgFormat.h"
#include "CBuffer.h"
#include <cstdint>

namespace Ailu
{
    DECLARE_ENUM(EColorRange, kLDR, kHDR)

    namespace RenderConstants
    {
        constexpr static u8 kFrameCount = 2u;
        constexpr static u16 kMaxMaterialDataCount = 128u;
        constexpr static u32 kMaxRenderObjectCount = 50u;
        constexpr static u32 kMaxPassDataCount = 10u;
        constexpr static u32 kMaxTextureCount = 128u;
        constexpr static u8 kMaxUAVTextureCount = 10U;
        constexpr static u32 kMaxRenderTextureCount = 32u;
        constexpr static u32 KMaxDynamicVertexNum = 2048;
        constexpr static u8 kMaxVertexAttrNum = 10u;
        constexpr static u16 kMaxDirectionalLightNum = kMaxDirectionalLight;
        constexpr static u16 kMaxPointLightNum = kMaxPointLight;
        constexpr static u16 kMaxSpotLightNum = kMaxSpotLight;
        constexpr static u16 kMaxCascadeShadowMapSplitNum = kMaxCascadeShadowMapSplit;

        constexpr static u32 kPerMaterialDataSize = 256;//sizeof(ScenePerMaterialData);
        constexpr static u32 kPerObjectDataSize = sizeof(CBufferPerObjectData);
        constexpr static u32 kPerCameraDataSize = sizeof(CBufferPerCameraData);
        constexpr static u32 kPerSceneDataSize = sizeof(CBufferPerSceneData);
        //2 is pass count,create cbv for each pass
        constexpr static u32 kPerFrameTotalSize = kPerSceneDataSize + kPerMaterialDataSize * kMaxMaterialDataCount * 2 + kPerCameraDataSize * kMaxPassDataCount +
                                                  kPerObjectDataSize * kMaxRenderObjectCount;
        inline const static String kVSModel_5_0 = "vs_5_0";
        inline const static String kPSModel_5_0 = "ps_5_0";
        inline const static String kVSModel_6_1 = "vs_6_1";
        inline const static String kPSModel_6_1 = "ps_6_1";
        inline const static String kCSModel_5_0 = "cs_5_0";

        inline const static char *kSemanticPosition = "POSITION";
        inline const static char *kSemanticColor = "COLOR";
        inline const static char *kSemanticTangent = "TANGENT";
        inline const static char *kSemanticNormal = "NORMAL";
        inline const static char *kSemanticTexcoord = "TEXCOORD";
        inline const static char *kSemanticBoneWeight = "BONEWEIGHT";
        inline const static char *kSemanticBoneIndex = "BONEINDEX";
        inline const static char *kSemanticVertexIndex = "SV_VERTEXID";

        inline const static String kCBufNamePerObject = "CBufferPerObjectData";
        inline const static String kCBufNamePerMaterial = "CBufferPerMaterialData";
        inline const static String kCBufNamePerScene = "CBufferPerSceneData";
        inline const static String kCBufNamePerCamera = "CBufferPerCameraData";

        inline const static String kAlbdeoTexName = "TexAlbedo";
        inline const static String kNormalTexName = "TexNormal";
        inline const static String kEmssiveTexName = "TexEmssive";
        inline const static String kRoughnessTexName = "TexRoughness";

        inline const static EColorRange::EColorRange kColorRange = EColorRange::kHDR;
        inline const static EALGFormat::EALGFormat kLDRFormat = EALGFormat::EALGFormat::kALGFormatR8G8B8A8_UNORM;
        //swap chain not support hdr 32bit
        inline const static EALGFormat::EALGFormat kHDRFormat = EALGFormat::EALGFormat::kALGFormatR16G16B16A16_FLOAT;
    };// namespace RenderConstants

}// namespace Ailu


#endif// !D3DCONSTANTS__
