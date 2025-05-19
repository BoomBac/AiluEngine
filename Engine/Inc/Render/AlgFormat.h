#pragma once
#ifndef __ALG_FORMAT_H__
#define __ALG_FORMAT_H__
#include "Framework/Common/Assert.h"
#include "GlobalMarco.h"
#include "dxgiformat.h"
#include <cstdint>

namespace Ailu
{
    DECLARE_ENUM(EALGFormat,
                 kALGFormatUNKOWN,
                 kALGFormatR32G32B32A32_TYPELESS,
                 kALGFormatR32G32B32A32_FLOAT,
                 kALGFormatR32G32B32A32_UINT,
                 kALGFormatR32G32B32A32_SINT,
                 kALGFormatR32G32B32_TYPELESS,
                 kALGFormatR32G32B32_FLOAT,
                 kALGFormatR32G32B32_UINT,
                 kALGFormatR32G32B32_SINT,
                 kALGFormatR16G16B16A16_TYPELESS,
                 kALGFormatR16G16B16A16_FLOAT,
                 kALGFormatR16G16B16A16_UNORM,
                 kALGFormatR16G16B16A16_UINT,
                 kALGFormatR16G16B16A16_SNORM,
                 kALGFormatR16G16B16A16_SINT,
                 kALGFormatR32G32_TYPELESS,
                 kALGFormatR32G32_FLOAT,
                 kALGFormatR32G32_UINT,
                 kALGFormatR32G32_SINT,
                 kALGFormatR32G8X24_TYPELESS,
                 kALGFormatD32_FLOAT_S8X24_UINT,
                 kALGFormatR32_FLOAT_X8X24_TYPELESS,
                 kALGFormatX32_TYPELESS_G8X24_UINT,
                 kALGFormatR10G10B10A2_TYPELESS,
                 kALGFormatR10G10B10A2_UNORM,
                 kALGFormatR10G10B10A2_UINT,
                 kALGFormatR11G11B10_FLOAT,
                 kALGFormatR8G8B8A8_TYPELESS,
                 kALGFormatR8G8B8A8_UNORM,
                 kALGFormatR8G8B8A8_UNORM_SRGB,
                 kALGFormatR8G8B8A8_UINT,
                 kALGFormatR8G8B8A8_SNORM,
                 kALGFormatR8G8B8A8_SINT,
                 kALGFormatR16G16_TYPELESS,
                 kALGFormatR16G16_FLOAT,
                 kALGFormatR16G16_UNORM,
                 kALGFormatR16G16_UINT,
                 kALGFormatR16G16_SNORM,
                 kALGFormatR16G16_SINT,
                 kALGFormatR32_TYPELESS,
                 kALGFormatD32_FLOAT,
                 kALGFormatR32_FLOAT,
                 kALGFormatR32_UINT,
                 kALGFormatR32_SINT,
                 kALGFormatR24G8_TYPELESS,
                 kALGFormatD24S8_UINT,
                 kALGFormatR24_UNORM_X8_TYPELESS,
                 kALGFormatX24_TYPELESS_G8_UINT,
                 kALGFormatR8G8_TYPELESS,
                 kALGFormatR8G8_UNORM,
                 kALGFormatR8G8_UINT,
                 kALGFormatR8G8_SNORM,
                 kALGFormatR8G8_SINT,
                 kALGFormatR16_TYPELESS,
                 kALGFormatR16_FLOAT,
                 kALGFormatD16_UNORM,
                 kALGFormatR16_UNORM,
                 kALGFormatR16_UINT,
                 kALGFormatR16_SNORM,
                 kALGFormatR16_SINT,
                 kALGFormatR8_TYPELESS,
                 kALGFormatR8_UNORM,
                 kALGFormatR8_UINT,
                 kALGFormatR8_SNORM,
                 kALGFormatR8_SINT,
                 kALGFormatA8_UNORM,
                 kALGFormatR1_UNORM,
                 kALGFormatR9G9B9E5_SHAREDEXP,
                 kALGFormatR8G8_B8G8_UNORM,
                 kALGFormatG8R8_G8B8_UNORM,
                 kALGFormatBC1_TYPELESS,
                 kALGFormatBC1_UNORM,
                 kALGFormatBC1_UNORM_SRGB,
                 kALGFormatBC2_TYPELESS,
                 kALGFormatBC2_UNORM,
                 kALGFormatBC2_UNORM_SRGB,
                 kALGFormatBC3_TYPELESS,
                 kALGFormatBC3_UNORM,
                 kALGFormatBC3_UNORM_SRGB,
                 kALGFormatBC4_TYPELESS,
                 kALGFormatBC4_UNORM,
                 kALGFormatBC4_SNORM,
                 kALGFormatBC5_TYPELESS,
                 kALGFormatBC5_UNORM,
                 kALGFormatBC5_SNORM,
                 kALGFormatB5G6R5_UNORM,
                 kALGFormatB5G5R5A1_UNORM,
                 kALGFormatB8G8R8A8_UNORM,
                 kALGFormatB8G8R8X8_UNORM,
                 kALGFormatR10G10B10_XR_BIAS_A2_UNORM,
                 kALGFormatB8G8R8A8_TYPELESS,
                 kALGFormatB8G8R8A8_UNORM_SRGB,
                 kALGFormatB8G8R8X8_TYPELESS,
                 kALGFormatB8G8R8X8_UNORM_SRGB,
                 kALGFormatBC6H_TYPELESS,
                 kALGFormatBC6H_UF16,
                 kALGFormatBC6H_SF16,
                 kALGFormatBC7_TYPELESS,
                 kALGFormatBC7_UNORM,
                 kALGFormatBC7_UNORM_SRGB)


    static DXGI_FORMAT ConvertToDXGIFormat(const EALGFormat::EALGFormat &format)
    {
        switch (format)
        {
            case EALGFormat::kALGFormatUNKOWN:
                return DXGI_FORMAT_UNKNOWN;
            case EALGFormat::kALGFormatR8G8B8A8_UNORM:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            case EALGFormat::kALGFormatR8G8B8A8_UNORM_SRGB:
                return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            case EALGFormat::kALGFormatR24G8_TYPELESS:
                return DXGI_FORMAT_R24G8_TYPELESS;
            case EALGFormat::kALGFormatD24S8_UINT:
                return DXGI_FORMAT_D24_UNORM_S8_UINT;
            case EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT:
                return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
            case EALGFormat::kALGFormatR32_FLOAT:
                return DXGI_FORMAT_R32_FLOAT;
            case EALGFormat::kALGFormatD32_FLOAT:
                return DXGI_FORMAT_D32_FLOAT;
            case EALGFormat::kALGFormatR16_FLOAT:
                return DXGI_FORMAT_R16_FLOAT;
            case EALGFormat::kALGFormatR32G32B32A32_FLOAT:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case EALGFormat::kALGFormatR32G32B32_FLOAT:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case EALGFormat::kALGFormatR32G32_FLOAT:
                return DXGI_FORMAT_R32G32_FLOAT;
            case EALGFormat::kALGFormatR16G16B16A16_FLOAT:
                return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case EALGFormat::kALGFormatR11G11B10_FLOAT:
                return DXGI_FORMAT_R11G11B10_FLOAT;
            case EALGFormat::kALGFormatR16G16_FLOAT:
                return DXGI_FORMAT_R16G16_FLOAT;
            case EALGFormat::kALGFormatR32_UINT:
                return DXGI_FORMAT_R32_UINT;
            case EALGFormat::kALGFormatR32_SINT:
                return DXGI_FORMAT_R32_SINT;
        }
        return DXGI_FORMAT_UNKNOWN;
    }

    static i8 GetFormatChannel(const EALGFormat::EALGFormat &format)
    {
        switch (format)
        {
            case EALGFormat::kALGFormatUNKOWN:
                return -1;
            case EALGFormat::kALGFormatR8G8B8A8_UNORM:
                return 4;
            case EALGFormat::kALGFormatR24G8_TYPELESS:
                return -1;
            case EALGFormat::kALGFormatD24S8_UINT:
                return -1;
            case EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT:
                return -1;
            case EALGFormat::kALGFormatR32_FLOAT:
                return 1;
            case EALGFormat::kALGFormatD32_FLOAT:
                return 1;
            case EALGFormat::kALGFormatR32G32B32A32_FLOAT:
                return 4;
            case EALGFormat::kALGFormatR32G32B32_FLOAT:
                return 3;
            case EALGFormat::kALGFormatR16G16B16A16_FLOAT:
                return 4;
            case EALGFormat::kALGFormatR11G11B10_FLOAT:
                return 1;
        }
        return -1;
    }


    static bool IsShadowMapFormat(const EALGFormat::EALGFormat &format)
    {
        switch (format)
        {
            case EALGFormat::kALGFormatR8G8B8A8_UNORM:
                return false;
            case EALGFormat::kALGFormatD24S8_UINT:
                return true;
            case EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT:
                return true;
            case EALGFormat::kALGFormatR24G8_TYPELESS:
                return true;
            case EALGFormat::kALGFormatR32_FLOAT:
                return true;
            case EALGFormat::kALGFormatD32_FLOAT:
                return true;
        }
        return false;
    }

    static u16 GetPixelByteSize(const EALGFormat::EALGFormat &format)
    {
        switch (format)
        {
            case EALGFormat::kALGFormatUNKOWN:
                return 65535;
                break;
            case EALGFormat::kALGFormatR8G8B8A8_UNORM:
            case EALGFormat::kALGFormatR8G8B8A8_UNORM_SRGB:
                return 4;
                break;
            case EALGFormat::kALGFormatR24G8_TYPELESS:
                return 4;
                break;
            case EALGFormat::kALGFormatR32_FLOAT:
                return 4;
                break;
            case EALGFormat::kALGFormatD32_FLOAT:
                return 4;
                break;
            case EALGFormat::kALGFormatR16_FLOAT:
                return 2;
                break;
            case EALGFormat::kALGFormatD24S8_UINT:
                return 4;
                break;
            case EALGFormat::kALGFormatR32G32B32A32_FLOAT:
                return 16;
                break;
            case EALGFormat::kALGFormatR32G32B32_FLOAT:
                return 12;
                break;
            case EALGFormat::kALGFormatR16G16B16A16_FLOAT:
                return 8;
                break;
            case EALGFormat::kALGFormatR11G11B10_FLOAT:
                return 4;
                break;
            case EALGFormat::kALGFormatR32_UINT:
                return 4;
                break;
            case EALGFormat::kALGFormatR32_SINT:
                return 4;
                break;
            case EALGFormat::kALGFormatR16G16_FLOAT:
                return 4;
            case EALGFormat::kALGFormatR32G32_FLOAT:
            case EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT:
                return 8;
            default:
                break;
        }
        AL_ASSERT_MSG(false, "Unknown format %d", (u32) format);
        return 65535;
    }
}// namespace Ailu

#endif// !ALG_FORMAT_H__
