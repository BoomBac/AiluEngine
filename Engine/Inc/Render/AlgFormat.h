#pragma once
#ifndef __ALG_FORMAT_H__
#define __ALG_FORMAT_H__
//#include "Framework/Common/Assert.h"
//#include "GlobalMarco.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/ReflectionMacros.h"
#include "dxgiformat.h"
#include "generated/AlgFormat.gen.h"

namespace Ailu
{
    AENUM()
    enum class EALGFormat : u16
    {
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
        kALGFormatBC7_UNORM_SRGB
    };


    static DXGI_FORMAT ConvertToDXGIFormat(const EALGFormat& format)
    {
        switch (format)
        {
            // 128-bit
            case EALGFormat::kALGFormatR32G32B32A32_FLOAT:    return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case EALGFormat::kALGFormatR32G32B32A32_UINT:     return DXGI_FORMAT_R32G32B32A32_UINT;
            case EALGFormat::kALGFormatR32G32B32A32_SINT:     return DXGI_FORMAT_R32G32B32A32_SINT;
            case EALGFormat::kALGFormatR32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_TYPELESS;

            // 96-bit
            case EALGFormat::kALGFormatR32G32B32_FLOAT:       return DXGI_FORMAT_R32G32B32_FLOAT;
            case EALGFormat::kALGFormatR32G32B32_UINT:        return DXGI_FORMAT_R32G32B32_UINT;
            case EALGFormat::kALGFormatR32G32B32_SINT:        return DXGI_FORMAT_R32G32B32_SINT;
            case EALGFormat::kALGFormatR32G32B32_TYPELESS:    return DXGI_FORMAT_R32G32B32_TYPELESS;

            // 64-bit
            case EALGFormat::kALGFormatR16G16B16A16_FLOAT:    return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case EALGFormat::kALGFormatR16G16B16A16_UNORM:    return DXGI_FORMAT_R16G16B16A16_UNORM;
            case EALGFormat::kALGFormatR16G16B16A16_UINT:     return DXGI_FORMAT_R16G16B16A16_UINT;
            case EALGFormat::kALGFormatR16G16B16A16_SNORM:    return DXGI_FORMAT_R16G16B16A16_SNORM;
            case EALGFormat::kALGFormatR16G16B16A16_SINT:     return DXGI_FORMAT_R16G16B16A16_SINT;
            case EALGFormat::kALGFormatR32G32_FLOAT:          return DXGI_FORMAT_R32G32_FLOAT;
            case EALGFormat::kALGFormatR32G32_UINT:           return DXGI_FORMAT_R32G32_UINT;
            case EALGFormat::kALGFormatR32G32_SINT:           return DXGI_FORMAT_R32G32_SINT;
            case EALGFormat::kALGFormatR32G32_TYPELESS:       return DXGI_FORMAT_R32G32_TYPELESS;

            // 32-bit
            case EALGFormat::kALGFormatR8G8B8A8_UNORM:        return DXGI_FORMAT_R8G8B8A8_UNORM;
            case EALGFormat::kALGFormatR8G8B8A8_UNORM_SRGB:   return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            case EALGFormat::kALGFormatR8G8B8A8_UINT:         return DXGI_FORMAT_R8G8B8A8_UINT;
            case EALGFormat::kALGFormatR8G8B8A8_SNORM:        return DXGI_FORMAT_R8G8B8A8_SNORM;
            case EALGFormat::kALGFormatR8G8B8A8_SINT:         return DXGI_FORMAT_R8G8B8A8_SINT;
            case EALGFormat::kALGFormatR10G10B10A2_TYPELESS:  return DXGI_FORMAT_R10G10B10A2_TYPELESS;
            case EALGFormat::kALGFormatR10G10B10A2_UNORM:     return DXGI_FORMAT_R10G10B10A2_UNORM;
            case EALGFormat::kALGFormatR10G10B10A2_UINT:      return DXGI_FORMAT_R10G10B10A2_UINT;
            case EALGFormat::kALGFormatR11G11B10_FLOAT:       return DXGI_FORMAT_R11G11B10_FLOAT;

            case EALGFormat::kALGFormatR32_FLOAT:             return DXGI_FORMAT_R32_FLOAT;
            case EALGFormat::kALGFormatR32_UINT:              return DXGI_FORMAT_R32_UINT;
            case EALGFormat::kALGFormatR32_SINT:              return DXGI_FORMAT_R32_SINT;
            case EALGFormat::kALGFormatR32_TYPELESS:          return DXGI_FORMAT_R32_TYPELESS;

            case EALGFormat::kALGFormatR16G16_FLOAT:          return DXGI_FORMAT_R16G16_FLOAT;
            case EALGFormat::kALGFormatR16G16_UNORM:          return DXGI_FORMAT_R16G16_UNORM;
            case EALGFormat::kALGFormatR16G16_UINT:           return DXGI_FORMAT_R16G16_UINT;
            case EALGFormat::kALGFormatR16G16_SNORM:          return DXGI_FORMAT_R16G16_SNORM;
            case EALGFormat::kALGFormatR16G16_SINT:           return DXGI_FORMAT_R16G16_SINT;

            case EALGFormat::kALGFormatR16_FLOAT:             return DXGI_FORMAT_R16_FLOAT;
            case EALGFormat::kALGFormatR16_UNORM:             return DXGI_FORMAT_R16_UNORM;
            case EALGFormat::kALGFormatR16_UINT:              return DXGI_FORMAT_R16_UINT;
            case EALGFormat::kALGFormatR16_SNORM:             return DXGI_FORMAT_R16_SNORM;
            case EALGFormat::kALGFormatR16_SINT:              return DXGI_FORMAT_R16_SINT;

            case EALGFormat::kALGFormatR8_UNORM:              return DXGI_FORMAT_R8_UNORM;
            case EALGFormat::kALGFormatR8_UINT:               return DXGI_FORMAT_R8_UINT;
            case EALGFormat::kALGFormatR8_SNORM:              return DXGI_FORMAT_R8_SNORM;
            case EALGFormat::kALGFormatR8_SINT:               return DXGI_FORMAT_R8_SINT;

            case EALGFormat::kALGFormatR8G8_UNORM:            return DXGI_FORMAT_R8G8_UNORM;
            case EALGFormat::kALGFormatR8G8_UINT:             return DXGI_FORMAT_R8G8_UINT;
            case EALGFormat::kALGFormatR8G8_SNORM:            return DXGI_FORMAT_R8G8_SNORM;
            case EALGFormat::kALGFormatR8G8_SINT:             return DXGI_FORMAT_R8G8_SINT;

            case EALGFormat::kALGFormatD32_FLOAT:             return DXGI_FORMAT_D32_FLOAT;
            case EALGFormat::kALGFormatD16_UNORM:             return DXGI_FORMAT_D16_UNORM;
            case EALGFormat::kALGFormatD24S8_UINT:            return DXGI_FORMAT_D24_UNORM_S8_UINT;
            case EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT:  return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;

            // Compressed formats
            case EALGFormat::kALGFormatBC1_UNORM:             return DXGI_FORMAT_BC1_UNORM;
            case EALGFormat::kALGFormatBC1_UNORM_SRGB:        return DXGI_FORMAT_BC1_UNORM_SRGB;
            case EALGFormat::kALGFormatBC2_UNORM:             return DXGI_FORMAT_BC2_UNORM;
            case EALGFormat::kALGFormatBC2_UNORM_SRGB:        return DXGI_FORMAT_BC2_UNORM_SRGB;
            case EALGFormat::kALGFormatBC3_UNORM:             return DXGI_FORMAT_BC3_UNORM;
            case EALGFormat::kALGFormatBC3_UNORM_SRGB:        return DXGI_FORMAT_BC3_UNORM_SRGB;
            case EALGFormat::kALGFormatBC4_UNORM:             return DXGI_FORMAT_BC4_UNORM;
            case EALGFormat::kALGFormatBC4_SNORM:             return DXGI_FORMAT_BC4_SNORM;
            case EALGFormat::kALGFormatBC5_UNORM:             return DXGI_FORMAT_BC5_UNORM;
            case EALGFormat::kALGFormatBC5_SNORM:             return DXGI_FORMAT_BC5_SNORM;
            case EALGFormat::kALGFormatBC6H_UF16:             return DXGI_FORMAT_BC6H_UF16;
            case EALGFormat::kALGFormatBC6H_SF16:             return DXGI_FORMAT_BC6H_SF16;
            case EALGFormat::kALGFormatBC7_UNORM:             return DXGI_FORMAT_BC7_UNORM;
            case EALGFormat::kALGFormatBC7_UNORM_SRGB:        return DXGI_FORMAT_BC7_UNORM_SRGB;

            // BGRA formats
            case EALGFormat::kALGFormatB8G8R8A8_UNORM:        return DXGI_FORMAT_B8G8R8A8_UNORM;
            case EALGFormat::kALGFormatB8G8R8A8_UNORM_SRGB:   return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            case EALGFormat::kALGFormatB8G8R8X8_UNORM:        return DXGI_FORMAT_B8G8R8X8_UNORM;
            case EALGFormat::kALGFormatB8G8R8X8_UNORM_SRGB:   return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;

            // Special cases
            case EALGFormat::kALGFormatA8_UNORM:              return DXGI_FORMAT_A8_UNORM;
            case EALGFormat::kALGFormatR1_UNORM:              return DXGI_FORMAT_R1_UNORM;
            case EALGFormat::kALGFormatR9G9B9E5_SHAREDEXP:    return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;

            // Unknown / fallback
            case EALGFormat::kALGFormatUNKOWN:
            default:
            {
                //AL_ASSERT_MSG(false,"Format({}): convert not define yet",format);
                return DXGI_FORMAT_UNKNOWN;
            }
        }
    }

    static i8 GetFormatChannel(const EALGFormat &format)
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


    static bool IsShadowMapFormat(const EALGFormat &format)
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

static uint16_t GetPixelByteSize(const EALGFormat& format)
{
    switch (format)
    {
        // 128-bit formats (16 bytes per pixel)
        case EALGFormat::kALGFormatR32G32B32A32_FLOAT:
        case EALGFormat::kALGFormatR32G32B32A32_UINT:
        case EALGFormat::kALGFormatR32G32B32A32_SINT:
            return 16;

        // 96-bit formats (12 bytes per pixel)
        case EALGFormat::kALGFormatR32G32B32_FLOAT:
        case EALGFormat::kALGFormatR32G32B32_UINT:
        case EALGFormat::kALGFormatR32G32B32_SINT:
            return 12;

        // 64-bit formats (8 bytes per pixel)
        case EALGFormat::kALGFormatR16G16B16A16_FLOAT:
        case EALGFormat::kALGFormatR16G16B16A16_UNORM:
        case EALGFormat::kALGFormatR16G16B16A16_UINT:
        case EALGFormat::kALGFormatR16G16B16A16_SNORM:
        case EALGFormat::kALGFormatR16G16B16A16_SINT:
        case EALGFormat::kALGFormatR32G32_FLOAT:
        case EALGFormat::kALGFormatR32G32_UINT:
        case EALGFormat::kALGFormatR32G32_SINT:
        case EALGFormat::kALGFormatD32_FLOAT_S8X24_UINT:  // technically 64-bit
            return 8;

        // 48-bit formats (6 bytes per pixel)
        case EALGFormat::kALGFormatR10G10B10A2_UNORM:
        case EALGFormat::kALGFormatR10G10B10A2_UINT:
            return 4; // Note: Packed into 32-bit (4 bytes), though "10/10/10" suggests more

        // 32-bit formats (4 bytes per pixel)
        case EALGFormat::kALGFormatR8G8B8A8_UNORM:
        case EALGFormat::kALGFormatR8G8B8A8_UNORM_SRGB:
        case EALGFormat::kALGFormatR8G8B8A8_UINT:
        case EALGFormat::kALGFormatR8G8B8A8_SNORM:
        case EALGFormat::kALGFormatR8G8B8A8_SINT:
        case EALGFormat::kALGFormatR16G16_FLOAT:
        case EALGFormat::kALGFormatR16G16_UNORM:
        case EALGFormat::kALGFormatR16G16_UINT:
        case EALGFormat::kALGFormatR16G16_SNORM:
        case EALGFormat::kALGFormatR16G16_SINT:
        case EALGFormat::kALGFormatR32_FLOAT:
        case EALGFormat::kALGFormatR32_UINT:
        case EALGFormat::kALGFormatR32_SINT:
        case EALGFormat::kALGFormatD32_FLOAT:
        case EALGFormat::kALGFormatD24S8_UINT:
        case EALGFormat::kALGFormatR10G10B10A2_TYPELESS:
        case EALGFormat::kALGFormatR11G11B10_FLOAT:
        case EALGFormat::kALGFormatB8G8R8A8_UNORM:
        case EALGFormat::kALGFormatB8G8R8X8_UNORM:
        case EALGFormat::kALGFormatB8G8R8A8_UNORM_SRGB:
            return 4;

        // 16-bit formats (2 bytes per pixel)
        case EALGFormat::kALGFormatR16_FLOAT:
        case EALGFormat::kALGFormatR16_UNORM:
        case EALGFormat::kALGFormatR16_UINT:
        case EALGFormat::kALGFormatR16_SNORM:
        case EALGFormat::kALGFormatR16_SINT:
        case EALGFormat::kALGFormatD16_UNORM:
            return 2;

        // 8-bit formats (1 byte per pixel)
        case EALGFormat::kALGFormatR8_UNORM:
        case EALGFormat::kALGFormatR8_UINT:
        case EALGFormat::kALGFormatR8_SNORM:
        case EALGFormat::kALGFormatR8_SINT:
        case EALGFormat::kALGFormatA8_UNORM:
            return 1;

        // Compressed formats - return 0 or special case
        case EALGFormat::kALGFormatBC1_UNORM:
        case EALGFormat::kALGFormatBC1_UNORM_SRGB:
        case EALGFormat::kALGFormatBC2_UNORM:
        case EALGFormat::kALGFormatBC2_UNORM_SRGB:
        case EALGFormat::kALGFormatBC3_UNORM:
        case EALGFormat::kALGFormatBC3_UNORM_SRGB:
        case EALGFormat::kALGFormatBC4_UNORM:
        case EALGFormat::kALGFormatBC4_SNORM:
        case EALGFormat::kALGFormatBC5_UNORM:
        case EALGFormat::kALGFormatBC5_SNORM:
        case EALGFormat::kALGFormatBC6H_UF16:
        case EALGFormat::kALGFormatBC6H_SF16:
        case EALGFormat::kALGFormatBC7_UNORM:
        case EALGFormat::kALGFormatBC7_UNORM_SRGB:
            return 0; // block-compressed, pixel size is not constant

        default:
            return 0;
    }
}

}// namespace Ailu

#endif// !ALG_FORMAT_H__
