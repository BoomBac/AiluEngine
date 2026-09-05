#include "Assets/TextureImporter.h"
#include "pch.h"
#include <DirectXTex.h>
#include <filesystem>
#include <filesystem>

namespace Ailu
{
    namespace
    {
        DXGI_FORMAT ResolveFormat(const TextureImportSetting &setting)
        {
            if (setting._compression == ETextureCompression::kNone)
                return setting._content == ETextureContent::kHdr ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
            if (setting._compression == ETextureCompression::kAuto)
            {
                if (setting._content == ETextureContent::kNormal) return DXGI_FORMAT_BC5_UNORM;
                if (setting._content == ETextureContent::kHdr) return DXGI_FORMAT_BC6H_UF16;
                return setting._is_srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
            }
            switch (setting._compression)
            {
                case ETextureCompression::kBc1: return setting._is_srgb ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
                case ETextureCompression::kBc3: return setting._is_srgb ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
                case ETextureCompression::kBc4: return DXGI_FORMAT_BC4_UNORM;
                case ETextureCompression::kBc5: return DXGI_FORMAT_BC5_UNORM;
                case ETextureCompression::kBc6H: return DXGI_FORMAT_BC6H_UF16;
                case ETextureCompression::kBc7: return setting._is_srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
                default: return DXGI_FORMAT_UNKNOWN;
            }
        }
        EALGFormat ToAlgFormat(DXGI_FORMAT format)
        {
            switch (format)
            {
                case DXGI_FORMAT_R8G8B8A8_UNORM: return EALGFormat::kALGFormatR8G8B8A8_UNORM;
                case DXGI_FORMAT_R32G32B32A32_FLOAT: return EALGFormat::kALGFormatR32G32B32A32_FLOAT;
                case DXGI_FORMAT_BC1_UNORM: return EALGFormat::kALGFormatBC1_UNORM;
                case DXGI_FORMAT_BC1_UNORM_SRGB: return EALGFormat::kALGFormatBC1_UNORM_SRGB;
                case DXGI_FORMAT_BC3_UNORM: return EALGFormat::kALGFormatBC3_UNORM;
                case DXGI_FORMAT_BC3_UNORM_SRGB: return EALGFormat::kALGFormatBC3_UNORM_SRGB;
                case DXGI_FORMAT_BC4_UNORM: return EALGFormat::kALGFormatBC4_UNORM;
                case DXGI_FORMAT_BC5_UNORM: return EALGFormat::kALGFormatBC5_UNORM;
                case DXGI_FORMAT_BC6H_UF16: return EALGFormat::kALGFormatBC6H_UF16;
                case DXGI_FORMAT_BC7_UNORM: return EALGFormat::kALGFormatBC7_UNORM;
                case DXGI_FORMAT_BC7_UNORM_SRGB: return EALGFormat::kALGFormatBC7_UNORM_SRGB;
                default: return EALGFormat::kALGFormatUNKOWN;
            }
        }
        bool BuildArtifact(const DirectX::ScratchImage &image, const TextureImportSetting &setting, TextureArtifact &artifact)
        {
            const auto &meta = image.GetMetadata();
            artifact = {};
            artifact._desc._width = static_cast<u32>(meta.width);
            artifact._desc._height = static_cast<u32>(meta.height);
            artifact._desc._mip_count = static_cast<u16>(meta.mipLevels);
            artifact._desc._array_size = static_cast<u16>(meta.arraySize);
            artifact._desc._format = ToAlgFormat(meta.format);
            artifact._desc._dimension = Render::ETextureDimension::kTex2D;
            artifact._desc._is_linear = !setting._is_srgb;
            artifact._desc._is_readable = setting._is_readable;
            if (artifact._desc._format == EALGFormat::kALGFormatUNKOWN) return false;
            for (size_t i = 0u; i < image.GetImageCount(); ++i)
            {
                const auto &source = image.GetImages()[i];
                auto &sub = artifact._subresources.emplace_back();
                sub._offset = artifact._pixel_data.size();
                sub._size = source.slicePitch;
                artifact._pixel_data.insert(artifact._pixel_data.end(), source.pixels, source.pixels + source.slicePitch);
            }
            return true;
        }
    }

    bool TextureImporter::Import(const WString &source_path, const TextureImportSetting &setting, TextureArtifact &out_artifact)
    {
        const String extension = ToChar(std::filesystem::path(source_path).extension().wstring());
        DirectX::ScratchImage decoded;
        const HRESULT load_result = su::EndWith(extension, ".dds") ? DirectX::LoadFromDDSFile(source_path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, decoded) :
            su::EndWith(extension, ".tga") ? DirectX::LoadFromTGAFile(source_path.c_str(), nullptr, decoded) :
            su::EndWith(extension, ".hdr") ? DirectX::LoadFromHDRFile(source_path.c_str(), nullptr, decoded) :
            DirectX::LoadFromWICFile(source_path.c_str(), DirectX::WIC_FLAGS_FORCE_RGB, nullptr, decoded);
        if (FAILED(load_result)) return false;
        const DXGI_FORMAT target_format = ResolveFormat(setting);
        if (target_format == DXGI_FORMAT_UNKNOWN) return false;
        const auto source_meta = decoded.GetMetadata();
        if (extension == ".dds" && source_meta.format == target_format && setting._max_size == 0u &&
            (!setting._generate_mipmap || source_meta.mipLevels > 1u)) return BuildArtifact(decoded, setting, out_artifact);
        DirectX::ScratchImage normalized;
        const DXGI_FORMAT normalized_format = setting._content == ETextureContent::kHdr ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
        if (FAILED(DirectX::Convert(decoded.GetImages(), decoded.GetImageCount(), source_meta, normalized_format, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, normalized))) return false;
        size_t target_width = normalized.GetMetadata().width, target_height = normalized.GetMetadata().height;
        if (setting._max_size != 0u)
        {
            const float scale = std::min(1.0f, std::min(static_cast<float>(setting._max_size) / target_width, static_cast<float>(setting._max_size) / target_height));
            target_width = static_cast<size_t>(std::max(1.0f, target_width * scale));
            target_height = static_cast<size_t>(std::max(1.0f, target_height * scale));
        }
        const bool is_bc = DirectX::IsCompressed(target_format);
        if (is_bc) { target_width = (target_width + 3u) & ~size_t(3u); target_height = (target_height + 3u) & ~size_t(3u); }
        DirectX::ScratchImage resized;
        const DirectX::ScratchImage *working = &normalized;
        if (target_width != normalized.GetMetadata().width || target_height != normalized.GetMetadata().height)
        {
            if (FAILED(DirectX::Resize(normalized.GetImages(), normalized.GetImageCount(), normalized.GetMetadata(), target_width, target_height, DirectX::TEX_FILTER_DEFAULT, resized))) return false;
            working = &resized;
        }
        DirectX::ScratchImage mipmapped;
        if (setting._generate_mipmap)
        {
            if (FAILED(DirectX::GenerateMipMaps(working->GetImages(), working->GetImageCount(), working->GetMetadata(), DirectX::TEX_FILTER_DEFAULT, 0u, mipmapped))) return false;
            working = &mipmapped;
        }
        DirectX::ScratchImage compressed;
        if (is_bc)
        {
            auto flags = DirectX::TEX_COMPRESS_PARALLEL;
            if (setting._compression_quality == ETextureCompressionQuality::kFast) flags |= DirectX::TEX_COMPRESS_BC7_QUICK;
            if (setting._compression_quality == ETextureCompressionQuality::kHigh) flags |= DirectX::TEX_COMPRESS_BC7_USE_3SUBSETS;
            if (FAILED(DirectX::Compress(working->GetImages(), working->GetImageCount(), working->GetMetadata(), target_format, flags, DirectX::TEX_THRESHOLD_DEFAULT, compressed))) return false;
            working = &compressed;
        }
        return BuildArtifact(*working, setting, out_artifact);
    }
}
