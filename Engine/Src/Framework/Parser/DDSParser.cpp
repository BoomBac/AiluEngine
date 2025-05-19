#include "pch.h"
#include "Framework/Parser/DDSParser.h"
#include "Framework/Common/FileManager.h"
#include "Ext/dds_loader/DDSTextureLoader12.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"


using namespace DirectX;
using namespace Ailu::Render;

namespace Ailu
{
    // 创建一个映射函数，将 DXGI_FORMAT 转换为 ETextureFormat
    ETextureFormat::ETextureFormat DXGIToETextureFormat(DXGI_FORMAT dxgiFormat)
    {
        switch (dxgiFormat) {
            case DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM:
                return ETextureFormat::kRGBA32;
            case DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT:
                return ETextureFormat::kRGBAFloat;
            case DXGI_FORMAT::DXGI_FORMAT_R32G32B32_FLOAT:
                return ETextureFormat::kRGBFloat;
            case DXGI_FORMAT::DXGI_FORMAT_R16G16B16A16_FLOAT:
                return ETextureFormat::kRGBAHalf;
            case DXGI_FORMAT::DXGI_FORMAT_R32G32_FLOAT:
                return ETextureFormat::kRGFloat;
            case DXGI_FORMAT::DXGI_FORMAT_R11G11B10_FLOAT:
                return ETextureFormat::kR11G11B10;
            default:
            {
                AL_ASSERT(true);
                return ETextureFormat::kRGBA32;
            }
        }
    }

    Ref<CubeMap> DDSParser::Parser(Vector<String> &paths,const TextureImportSetting& import_settings)
    {
        return nullptr;
    }
    Ref<Texture2D> DDSParser::Parser(const WString &sys_path,const TextureImportSetting& import_settings)
    {
        TextureLoadData load_data;
        if (!LoadTextureData(sys_path, load_data))
        {
            return nullptr;
        }
        Texture2DInitializer desc;
        desc._width = load_data._width;
        desc._height = load_data._height;
        desc._is_linear = import_settings._is_srgb;
        desc._is_mipmap_chain = import_settings._generate_mipmap;
        desc._is_readble = import_settings._is_readble;
        desc._format = load_data._format;
        auto tex = Texture2D::Create(desc);
        tex->SetPixelData(load_data._data[0],0);
        auto sys_path_n = ToChar(sys_path);
        tex->Name(PathUtils::GetFileName(sys_path_n));
        return tex;
    }
    bool DDSParser::LoadTextureData(const WString &sys_path,TextureLoadData& data)
    {
        if (!FileManager::Exist(sys_path))
        {
            LOG_ERROR("DDSParser::LoadTextureData: File not exist: {}", ToChar(sys_path));
            return false;
        }
        std::unique_ptr<uint8_t[]> ddsData;
        const DirectX::DDS_HEADER *header = nullptr;
        const uint8_t *bitData = nullptr;
        size_t bitSize = 0;
        ThrowIfFailed(DirectX::LoadTextureDataFromFile(sys_path.c_str(), ddsData, &header, &bitData, &bitSize));
        HRESULT error_code = S_OK;

        const UINT width = header->width;
        UINT height = header->height;
        UINT depth = header->depth;
        data._width = width;
        data._height = height;

        D3D12_RESOURCE_DIMENSION resDim = D3D12_RESOURCE_DIMENSION_UNKNOWN;
        UINT arraySize = 1;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        bool isCubeMap = false;

        size_t mipCount = header->mipMapCount;
        if (0 == mipCount)
        {
            mipCount = 1;
        }
        if ((header->ddspf.flags & DDS_FOURCC) &&
            (MAKEFOURCC('D', 'X', '1', '0') == header->ddspf.fourCC))
        {
            auto d3d10ext = reinterpret_cast<const DDS_HEADER_DXT10*>(reinterpret_cast<const char *>(header) + sizeof(DDS_HEADER));

            arraySize = d3d10ext->arraySize;
            if (arraySize == 0)
            {
                error_code = HRESULT_E_INVALID_DATA;
            }

            switch (d3d10ext->dxgiFormat)
            {
                case DXGI_FORMAT_NV12:
                case DXGI_FORMAT_P010:
                case DXGI_FORMAT_P016:
                case DXGI_FORMAT_420_OPAQUE:
                    if ((d3d10ext->resourceDimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) || (width % 2) != 0 || (height % 2) != 0)
                    {
                        error_code =  HRESULT_E_NOT_SUPPORTED;
                    }
                    break;

                case DXGI_FORMAT_YUY2:
                case DXGI_FORMAT_Y210:
                case DXGI_FORMAT_Y216:
                case DXGI_FORMAT_P208:
                    if ((width % 2) != 0)
                    {
                        error_code = HRESULT_E_NOT_SUPPORTED;
                    }
                    break;

                case DXGI_FORMAT_NV11:
                    if ((width % 4) != 0)
                    {
                        error_code =  HRESULT_E_NOT_SUPPORTED;
                    }
                    break;

                case DXGI_FORMAT_AI44:
                case DXGI_FORMAT_IA44:
                case DXGI_FORMAT_P8:
                case DXGI_FORMAT_A8P8:
                    error_code = HRESULT_E_NOT_SUPPORTED;
                    break;

                case DXGI_FORMAT_V208:
                    if ((d3d10ext->resourceDimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) || (height % 2) != 0)
                    {
                        error_code = HRESULT_E_NOT_SUPPORTED;
                        break ;
                    }
                    break;

                default:
                    if (BitsPerPixel(d3d10ext->dxgiFormat) == 0)
                    {
                        error_code = HRESULT_E_NOT_SUPPORTED;
                        break;
                    }
            }

            format = d3d10ext->dxgiFormat;

            switch (d3d10ext->resourceDimension)
            {
                case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
                    // D3DX writes 1D textures with a fixed Height of 1
                    if ((header->flags & DDS_HEIGHT) && height != 1)
                    {
                        error_code = HRESULT_E_INVALID_DATA;
                        break;
                    }
                    height = depth = 1;
                    break;

                case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
                    if (d3d10ext->miscFlag & 0x4 /* RESOURCE_MISC_TEXTURECUBE */)
                    {
                        arraySize *= 6;
                        isCubeMap = true;
                    }
                    depth = 1;
                    break;

                case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
                    if (!(header->flags & DDS_HEADER_FLAGS_VOLUME))
                    {
                        error_code = HRESULT_E_INVALID_DATA;
                        break;
                    }

                    if (arraySize > 1)
                    {
                        error_code = HRESULT_E_NOT_SUPPORTED;
                        break;
                    }
                    break;

                default:
                    error_code = HRESULT_E_NOT_SUPPORTED;
                    break;
            }

            resDim = static_cast<D3D12_RESOURCE_DIMENSION>(d3d10ext->resourceDimension);
        }
        else
        {
            format = GetDXGIFormat(header->ddspf);

            if (format == DXGI_FORMAT_UNKNOWN)
            {
                error_code = HRESULT_E_NOT_SUPPORTED;
                return false;
            }

            if (header->flags & DDS_HEADER_FLAGS_VOLUME)
            {
                resDim = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
            }
            else
            {
                if (header->caps2 & DDS_CUBEMAP)
                {
                    // We require all six faces to be defined
                    if ((header->caps2 & DDS_CUBEMAP_ALLFACES) != DDS_CUBEMAP_ALLFACES)
                    {
                        error_code =  HRESULT_E_NOT_SUPPORTED;
                        return false;
                    }

                    arraySize = 6;
                    isCubeMap = true;
                }

                depth = 1;
                resDim = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

                // Note there's no way for a legacy Direct3D 9 DDS to express a '1D' texture
            }

            assert(BitsPerPixel(format) != 0);
        }
        data._format = DXGIToETextureFormat(format);
        u8* raw_data = new u8[bitSize];
        memcpy(raw_data, bitData, bitSize);
        data._data.emplace_back(raw_data);
        return true;
    }

    bool DDSParser::Parser(const WString &sys_path,Ref<Texture2D>& texture,const TextureImportSetting& import_settings)
    {
        TextureLoadData load_data;
        if (!LoadTextureData(sys_path, load_data))
        {
            return false;
        }
        else
        {
            Texture2DInitializer desc;
            desc._width = load_data._width;
            desc._height = load_data._height;
            desc._is_linear = import_settings._is_srgb;
            desc._is_mipmap_chain = import_settings._generate_mipmap;
            desc._is_readble = import_settings._is_readble;
            desc._format = load_data._format;
            texture->ReCreate(desc);
            for(u16 i = 0; i < load_data._data.size(); ++i)
            {
                texture->SetPixelData(load_data._data[i], i);
            }
            return true;
        }
    }
}// namespace Ailu
