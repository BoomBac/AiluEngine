#include "Assets/TextureArtifact.h"

#include "Framework/Common/Log.h"
#include "pch.h"

#include <cstring>
#include <limits>

namespace Ailu
{
    namespace
    {
        using namespace Render;

        template<typename T>
        void AppendValue(Vector<u8> &data, const T &value)
        {
            const auto *begin = reinterpret_cast<const u8 *>(&value);
            data.insert(data.end(), begin, begin + sizeof(T));
        }

        template<typename T>
        bool ReadValue(std::span<const u8> data, size_t &offset, T &value)
        {
            if (offset > data.size() || sizeof(T) > data.size() - offset)
                return false;
            memcpy(&value, data.data() + offset, sizeof(T));
            offset += sizeof(T);
            return true;
        }

        void AppendVector(Vector<u8> &data, const Vector3f &value)
        {
            AppendValue(data, value.x);
            AppendValue(data, value.y);
            AppendValue(data, value.z);
        }

        void AppendVector(Vector<u8> &data, const Vector4f &value)
        {
            AppendValue(data, value.x);
            AppendValue(data, value.y);
            AppendValue(data, value.z);
            AppendValue(data, value.w);
        }
    }

    bool BuildTextureArtifact(const Render::Texture2D &texture, TextureArtifact &out_artifact)
    {
        const u16 mip_count = texture.MipmapLevel();
        if (mip_count == 0u || texture.Width() == 0u || texture.Height() == 0u)
            return false;

        out_artifact = {};
        out_artifact._desc._width = texture.Width();
        out_artifact._desc._height = texture.Height();
        out_artifact._desc._mip_count = mip_count;
        out_artifact._desc._format = texture.PixelFormat();
        out_artifact._desc._dimension = texture.Dimension();
        out_artifact._desc._is_linear = !texture.sRGB();
        out_artifact._desc._is_readable = texture.Readble();
        const u32 pixel_size = GetPixelByteSize(texture.PixelFormat());
        if (pixel_size == 0u)
            return false;

        out_artifact._subresources.reserve(mip_count);
        for (u16 mip = 0u; mip < mip_count; ++mip)
        {
            const auto [width, height] = Texture::CalculateMipSize(texture.Width(), texture.Height(), mip);
            const u64 size = static_cast<u64>(width) * static_cast<u64>(height) * pixel_size;
            const Ptr pixel_data = const_cast<Render::Texture2D &>(texture).GetPixelData(mip);
            if (pixel_data == nullptr || size > (std::numeric_limits<u64>::max)() - out_artifact._pixel_data.size())
                return false;
            TextureArtifactSubresource subresource;
            subresource._offset = out_artifact._pixel_data.size();
            subresource._size = size;
            out_artifact._subresources.emplace_back(subresource);
            const auto *begin = static_cast<const u8 *>(pixel_data);
            out_artifact._pixel_data.insert(out_artifact._pixel_data.end(), begin, begin + size);
        }
        return true;
    }

    bool SerializeTextureArtifact(const TextureArtifact &artifact, const AssetArtifactKey &key, Vector<u8> &out_data)
    {
        Vector<u8> desc_data;
        AppendValue(desc_data, artifact._desc._width);
        AppendValue(desc_data, artifact._desc._height);
        AppendValue(desc_data, artifact._desc._depth);
        AppendValue(desc_data, artifact._desc._mip_count);
        AppendValue(desc_data, artifact._desc._array_size);
        AppendValue(desc_data, static_cast<u32>(artifact._desc._format));
        AppendValue(desc_data, static_cast<u32>(artifact._desc._dimension));
        AppendValue(desc_data, static_cast<u8>(artifact._desc._is_linear));
        AppendValue(desc_data, static_cast<u8>(artifact._desc._is_readable));
        AppendValue(desc_data, static_cast<u8>(artifact._desc._is_random_access));

        Vector<u8> subresource_data;
        for (const TextureArtifactSubresource &subresource : artifact._subresources)
        {
            AppendValue(subresource_data, subresource._offset);
            AppendValue(subresource_data, subresource._size);
        }

        AssetArtifactWriter writer;
        writer.AddSection(kTextureArtifactDescSection, desc_data, 1u, static_cast<u32>(desc_data.size()));
        writer.AddSection(kTextureArtifactSubresourceSection, subresource_data,
                          static_cast<u32>(artifact._subresources.size()), sizeof(TextureArtifactSubresource));
        writer.AddSection(kTextureArtifactPixelDataSection, artifact._pixel_data,
                          static_cast<u32>(artifact._pixel_data.size()), 1u);
        return writer.Serialize(EAssetArtifactType::kTexture, kTextureArtifactVersion, key, out_data);
    }

    bool DeserializeTextureArtifact(std::span<const u8> data, const AssetArtifactKey &key,
                                    TextureArtifact &out_artifact)
    {
        AssetArtifactReader reader;
        if (!reader.Load(data, key, EAssetArtifactType::kTexture))
            return false;

        const std::span<const u8> desc_data = reader.GetSectionData(kTextureArtifactDescSection);
        const std::span<const u8> subresource_data = reader.GetSectionData(kTextureArtifactSubresourceSection);
        const std::span<const u8> pixel_data = reader.GetSectionData(kTextureArtifactPixelDataSection);
        if (desc_data.empty() || subresource_data.size() % (sizeof(u64) * 2u) != 0u)
            return false;

        size_t offset = 0u;
        u32 format = 0u;
        u32 dimension = 0u;
        u8 is_linear = 0u;
        u8 is_readable = 0u;
        u8 is_random_access = 0u;
        if (!ReadValue(desc_data, offset, out_artifact._desc._width) ||
            !ReadValue(desc_data, offset, out_artifact._desc._height) ||
            !ReadValue(desc_data, offset, out_artifact._desc._depth) ||
            !ReadValue(desc_data, offset, out_artifact._desc._mip_count) ||
            !ReadValue(desc_data, offset, out_artifact._desc._array_size) ||
            !ReadValue(desc_data, offset, format) || !ReadValue(desc_data, offset, dimension) ||
            !ReadValue(desc_data, offset, is_linear) || !ReadValue(desc_data, offset, is_readable) ||
            !ReadValue(desc_data, offset, is_random_access) || offset != desc_data.size())
        {
            return false;
        }
        out_artifact._desc._format = static_cast<EALGFormat>(format);
        out_artifact._desc._dimension = static_cast<ETextureDimension>(dimension);
        out_artifact._desc._is_linear = is_linear != 0u;
        out_artifact._desc._is_readable = is_readable != 0u;
        out_artifact._desc._is_random_access = is_random_access != 0u;

        constexpr size_t kSubresourceSize = sizeof(u64) * 2u;
        out_artifact._subresources.resize(subresource_data.size() / kSubresourceSize);
        offset = 0u;
        for (TextureArtifactSubresource &subresource : out_artifact._subresources)
        {
            if (!ReadValue(subresource_data, offset, subresource._offset) ||
                !ReadValue(subresource_data, offset, subresource._size) ||
                subresource._offset > pixel_data.size() || subresource._size > pixel_data.size() - subresource._offset)
            {
                return false;
            }
        }
        out_artifact._pixel_data.assign(pixel_data.begin(), pixel_data.end());
        return true;
    }

    Ref<Render::Texture2D> CreateTextureFromArtifact(const TextureArtifact &artifact)
    {
        if (artifact._desc._mip_count == 0u || artifact._subresources.size() != artifact._desc._mip_count)
            return nullptr;
        TextureDesc desc;
        desc._width = artifact._desc._width;
        desc._height = artifact._desc._height;
        desc._depth = artifact._desc._depth;
        desc._mip_num = artifact._desc._mip_count;
        desc._array_size = artifact._desc._array_size;
        desc._format = artifact._desc._format;
        desc._dimension = artifact._desc._dimension;
        desc._is_linear = artifact._desc._is_linear;
        desc._is_readable = artifact._desc._is_readable;
        desc._is_random_access = artifact._desc._is_random_access;
        auto texture = Texture2D::Create(desc);
        if (texture == nullptr)
            return nullptr;
        Vector<Render::TextureSubresourceData> subresources;
        subresources.reserve(artifact._subresources.size());
        for (const TextureArtifactSubresource &subresource : artifact._subresources)
            subresources.emplace_back(Render::TextureSubresourceData{subresource._offset, subresource._size});
        texture->SetImportedPixelData(artifact._pixel_data, std::move(subresources));
        return texture;
    }
}
