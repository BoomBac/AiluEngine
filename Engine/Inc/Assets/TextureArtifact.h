#pragma once

#include "Assets/AssetArtifact.h"
#include "Render/Texture.h"

namespace Ailu
{
    inline constexpr u32 kTextureArtifactDescSection = 1u;
    inline constexpr u32 kTextureArtifactSubresourceSection = 2u;
    inline constexpr u32 kTextureArtifactPixelDataSection = 3u;

    struct AILU_API TextureArtifactDesc
    {
        u32 _width = 0u;
        u32 _height = 0u;
        u16 _depth = 1u;
        u16 _mip_count = 1u;
        u16 _array_size = 1u;
        EALGFormat _format = EALGFormat::kALGFormatUNKOWN;
        Render::ETextureDimension _dimension = Render::ETextureDimension::kTex2D;
        bool _is_linear = false;
        bool _is_readable = false;
        bool _is_random_access = false;
    };

    struct TextureArtifactSubresource
    {
        u64 _offset = 0u;
        u64 _size = 0u;
    };


    struct AILU_API TextureArtifact
    {
        TextureArtifactDesc _desc;
        Vector<TextureArtifactSubresource> _subresources;
        Vector<u8> _pixel_data;
    };

    AILU_API bool SerializeTextureArtifact(const TextureArtifact &artifact, const AssetArtifactKey &key,
                                           Vector<u8> &out_data);
    AILU_API bool DeserializeTextureArtifact(std::span<const u8> data, const AssetArtifactKey &key,
                                             TextureArtifact &out_artifact);
    AILU_API Ref<Render::Texture2D> CreateTextureFromArtifact(const TextureArtifact &artifact);
}
