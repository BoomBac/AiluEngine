#include "Render/ImageCapture/DdsCaptureEncoder.h"

#include <algorithm>
#include <DirectXTex.h>
#include <format>

namespace Ailu::Render
{
bool DdsCaptureEncoder::Encode(const ImageCaptureCpuImage &image, const WString &file_path, String &error)
{
    if (image._format == EALGFormat::kALGFormatUNKOWN || image._sub_resources.empty())
    {
        error = "DDS encoder received an empty or unknown image";
        return false;
    }

    DirectX::TexMetadata metadata{};
    metadata.width = image._width;
    metadata.height = image._height;
    metadata.depth = image._dimension == ETextureDimension::kTex3D ? image._depth : 1u;
    metadata.arraySize = std::max<u32>(1u, image._array_size);
    metadata.mipLevels = std::max<u32>(1u, image._mip_count);
    metadata.format = ConvertToDXGIFormat(image._format);
    if (metadata.format == DXGI_FORMAT_UNKNOWN)
    {
        error = "DDS encoder does not support the source format";
        return false;
    }

    switch (image._dimension)
    {
    case ETextureDimension::kTex2D:
    case ETextureDimension::kTex2DArray:
    case ETextureDimension::kCube:
    case ETextureDimension::kCubeArray:
        metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
        break;
    case ETextureDimension::kTex3D:
        metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE3D;
        break;
    default:
        error = "DDS encoder does not support the texture dimension";
        return false;
    }

    const bool is_cube = image._dimension == ETextureDimension::kCube ||
                         image._dimension == ETextureDimension::kCubeArray;
    metadata.miscFlags = is_cube ? DirectX::TEX_MISC_TEXTURECUBE : 0u;

    Vector<DirectX::Image> images;
    images.reserve(image._sub_resources.size());
    for (const auto &subresource: image._sub_resources)
    {
        if (subresource._data == nullptr || subresource._data_size == 0u || subresource._row_pitch == 0u ||
            subresource._slice_pitch == 0u)
        {
            error = "DDS encoder received an invalid subresource";
            return false;
        }

        // D3D12 has one subresource per volume mip; DirectXTex expects one Image per depth slice.
        const u32 depth = metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE3D ? subresource._depth : 1u;
        if (depth == 0u || u64(subresource._slice_pitch) * depth > subresource._data_size)
        {
            error = "DDS encoder received an incomplete volume footprint";
            return false;
        }
        for (u32 slice = 0u; slice < depth; ++slice)
        {
            DirectX::Image &out = images.emplace_back();
            out.width = subresource._width;
            out.height = subresource._height;
            out.format = metadata.format;
            out.rowPitch = subresource._row_pitch;
            out.slicePitch = subresource._slice_pitch;
            out.pixels = const_cast<uint8_t *>(subresource._data + u64(slice) * subresource._slice_pitch);
        }
    }

    const HRESULT result = DirectX::SaveToDDSFile(images.data(), images.size(), metadata, DirectX::DDS_FLAGS_NONE,
                                                   file_path.c_str());
    if (FAILED(result))
    {
        error = std::format("SaveToDDSFile failed with HRESULT 0x{:08X}", static_cast<u32>(result));
        return false;
    }
    return true;
}
} // namespace Ailu::Render
