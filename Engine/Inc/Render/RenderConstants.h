#pragma once
#ifndef __D3DCONSTANTS__
#define __D3DCONSTANTS__
#include "AlgFormat.h"
#include "ShaderInterop.h"
#include <cstdint>
#include <cstring>
#include "generated/RenderConstants.gen.h"

namespace Ailu::Render
{
    enum class EVertexSemantic : u8
    {
        kUnknown = 0u,
        kPosition,
        kNormal,
        kTangent,
        kColor,
        kBoneIndex,
        kBoneWeight,
        kTexcoord0,
        kTexcoord1,
        kTexcoord2,
        kTexcoord3,
        kTexcoord4,
        kTexcoord5,
        kTexcoord6,
        kTexcoord7,
        kTexcoord8,
        kTexcoord9,
        kVertexIndex,
        kInstanceID
    };

    namespace RenderConstants
    {
        constexpr bool IsTexcoordSemantic(EVertexSemantic semantic) noexcept
        {
            const u8 value = static_cast<u8>(semantic);
            return value >= static_cast<u8>(EVertexSemantic::kTexcoord0) &&
                   value <= static_cast<u8>(EVertexSemantic::kTexcoord9);
        }

        constexpr u8 GetVertexSemanticIndex(EVertexSemantic semantic) noexcept
        {
            return IsTexcoordSemantic(semantic) ? static_cast<u8>(semantic) - static_cast<u8>(EVertexSemantic::kTexcoord0) : 0u;
        }

        constexpr const char *GetVertexSemanticName(EVertexSemantic semantic) noexcept
        {
            switch (semantic)
            {
                case EVertexSemantic::kPosition:
                    return "POSITION";
                case EVertexSemantic::kNormal:
                    return "NORMAL";
                case EVertexSemantic::kTangent:
                    return "TANGENT";
                case EVertexSemantic::kColor:
                    return "COLOR";
                case EVertexSemantic::kBoneIndex:
                    return "BONEINDEX";
                case EVertexSemantic::kBoneWeight:
                    return "BONEWEIGHT";
                case EVertexSemantic::kTexcoord0:
                case EVertexSemantic::kTexcoord1:
                case EVertexSemantic::kTexcoord2:
                case EVertexSemantic::kTexcoord3:
                case EVertexSemantic::kTexcoord4:
                case EVertexSemantic::kTexcoord5:
                case EVertexSemantic::kTexcoord6:
                case EVertexSemantic::kTexcoord7:
                case EVertexSemantic::kTexcoord8:
                case EVertexSemantic::kTexcoord9:
                    return "TEXCOORD";
                case EVertexSemantic::kVertexIndex:
                    return "SV_VERTEXID";
                case EVertexSemantic::kInstanceID:
                    return "SV_INSTANCEID";
                default:
                    return "";
            }
        }

        inline EVertexSemantic GetVertexSemantic(const char *semantic_name, u8 semantic_index = 0u) noexcept
        {
            if (semantic_name == nullptr)
                return EVertexSemantic::kUnknown;
            if (std::strcmp(semantic_name, "POSITION") == 0)
                return EVertexSemantic::kPosition;
            if (std::strcmp(semantic_name, "NORMAL") == 0)
                return EVertexSemantic::kNormal;
            if (std::strcmp(semantic_name, "TANGENT") == 0)
                return EVertexSemantic::kTangent;
            if (std::strcmp(semantic_name, "COLOR") == 0)
                return EVertexSemantic::kColor;
            if (std::strcmp(semantic_name, "BONEINDEX") == 0)
                return EVertexSemantic::kBoneIndex;
            if (std::strcmp(semantic_name, "BONEWEIGHT") == 0)
                return EVertexSemantic::kBoneWeight;
            if (std::strcmp(semantic_name, "TEXCOORD") == 0 && semantic_index < 10u)
                return static_cast<EVertexSemantic>(static_cast<u8>(EVertexSemantic::kTexcoord0) + semantic_index);
            if (std::strcmp(semantic_name, "SV_VERTEXID") == 0 ||
                std::strcmp(semantic_name, "SV_VertexID") == 0)
                return EVertexSemantic::kVertexIndex;
            if (std::strcmp(semantic_name, "SV_INSTANCEID") == 0 ||
                std::strcmp(semantic_name, "SV_InstanceID") == 0)
                return EVertexSemantic::kInstanceID;
            return EVertexSemantic::kUnknown;
        }
    }

    AENUM()
    enum class EColorRange
    {
        kLDR,
        kHDR
    };

    namespace RenderConstants
    {
        constexpr static u8 kFrameCount = 2u;
        constexpr static u16 kMaxMaterialDataCount = 512u;
        constexpr static u32 kMaxRenderObjectCount = 2000u;
        constexpr static u32 kMaxPassDataCount = 10u;
        constexpr static u32 kMaxTextureCount = 512u;
        constexpr static u8 kMaxUAVTextureCount = 10U;
        constexpr static u32 kMaxRenderTextureCount = 128u;
        constexpr static u32 KMaxDynamicVertexNum = 8192u;
        constexpr static u8 kMaxVertexAttrNum = 10u;
        constexpr static u16 kMaxDirectionalLightNum = kMaxDirectionalLight;
        constexpr static u16 kMaxPointLightNum = kMaxPointLight;
        constexpr static u16 kMaxSpotLightNum = kMaxSpotLight;
        constexpr static u16 kMaxAreaLightNum = kMaxAreaLight;
        constexpr static u16 kMaxCascadeShadowMapSplitNum = kMaxCascadeShadowMapSplit;

        constexpr static u16 kMaxMRTNum = 8u;

        constexpr static u64 kMaxGpuTimerNum = 512u;

        constexpr static u32 kPerMaterialDataSize = 256;//sizeof(ScenePerMaterialData);
        constexpr static u32 kPerObjectDataSize = sizeof(CBufferPerObjectData);
        constexpr static u32 kPerCameraDataSize = sizeof(CBufferPerCameraData);
        constexpr static u32 kPerSceneDataSize = sizeof(CBufferPerSceneData);
        //2 is pass count,create cbv for each pass
        constexpr static u32 kPerFrameTotalSize = kPerSceneDataSize + kPerMaterialDataSize * kMaxMaterialDataCount * 2 + kPerCameraDataSize * kMaxPassDataCount +
                                                  kPerObjectDataSize * kMaxRenderObjectCount;
        inline const static String kVSModel_5_0 = "vs_5_0";
        inline const static String kPSModel_5_0 = "ps_5_0";
        inline const static String kGSModel_5_0 = "gs_5_0";
        inline const static String kCSModel_5_0 = "cs_5_0";
        inline const static String kVSModel_6_1 = "vs_6_1";
        inline const static String kPSModel_6_1 = "ps_6_1";
        inline const static String kCSModel_6_1 = "cs_6_1";
        inline const static String kGSModel_6_1 = "gs_6_1";
        inline const static String kLibModel_6_3 = "lib_6_3";
        inline const static String kLibModel_6_6 = "lib_6_6";

        inline const static String kCBufNamePerObject = "CBufferPerObjectData";
        inline const static String kCBufNamePrimitiveDraw = "CBufferPrimitiveDrawData";
        inline const static String kCBufNamePerMaterial = "CBufferPerMaterialData";
        inline const static String kCBufNamePerScene = "CBufferPerSceneData";
        inline const static String kCBufNamePerCamera = "CBufferPerCameraData";

        inline const static String kAlbdeoTexName = "TexAlbedo";
        inline const static String kNormalTexName = "TexNormal";
        inline const static String kEmssiveTexName = "TexEmssive";
        inline const static String kRoughnessTexName = "TexRoughness";

        inline const static EColorRange kColorRange = EColorRange::kHDR;
        inline const static EALGFormat kLDRFormat = EALGFormat::kALGFormatR8G8B8A8_UNORM;
        //swap chain not support hdr 32bit
        inline const static EALGFormat kHDRFormat = EALGFormat::kALGFormatR16G16B16A16_FLOAT;

        inline const static u32 kInvalidBindlessHandle = 0xFFFFFFFF;
    };// namespace RenderConstants

}// namespace Ailu


#endif// !D3DCONSTANTS__
