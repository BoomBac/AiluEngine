#pragma once
#ifndef __ALG_FORMAT_H__
#define __ALG_FORMAT_H__
#include <cstdint>
#include "dxgiformat.h"
#include "GlobalMarco.h"
#include "Framework/Common/Assert.h"

DECLARE_ENUM(EALGFormat, kALGFormatUnknown,
    kALGFormatR8G8B8A8_UNORM, kALGFormatR8G8B8A8_UNORM_SRGB,
    kALGFormatR24G8_TYPELESS, kALGFormatR32_FLOAT, kALGFormatD32_FLOAT, kALGFormatD16_FLOAT,kALGFormatD24S8_UINT,
    kALGFormatR32G32B32A32_FLOAT, kALGFormatR32G32B32_FLOAT, kALGFormatR16G16B16A16_FLOAT, kALGFormatR11G11B10_FLOAT,
    kALGFormatR16G16_FLOAT, kALGFormatR32G32_FLOAT, kALGFormatR32_UINT, kALGFormatR32_SINT)


static DXGI_FORMAT ConvertToDXGIFormat(const EALGFormat::EALGFormat& format)
{
    switch (format)
    {
    case EALGFormat::kALGFormatUnknown: return DXGI_FORMAT_UNKNOWN;
    case EALGFormat::kALGFormatR8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case EALGFormat::kALGFormatR8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case EALGFormat::kALGFormatR24G8_TYPELESS: return DXGI_FORMAT_R24G8_TYPELESS;
    case EALGFormat::kALGFormatD24S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case EALGFormat::kALGFormatR32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    case EALGFormat::kALGFormatD32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
    case EALGFormat::kALGFormatD16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
    case EALGFormat::kALGFormatR32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case EALGFormat::kALGFormatR32G32B32_FLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
    case EALGFormat::kALGFormatR32G32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
    case EALGFormat::kALGFormatR16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case EALGFormat::kALGFormatR11G11B10_FLOAT: return DXGI_FORMAT_R11G11B10_FLOAT;
    case EALGFormat::kALGFormatR16G16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
    case EALGFormat::kALGFormatR32_UINT: return DXGI_FORMAT_R32_UINT;
    case EALGFormat::kALGFormatR32_SINT: return DXGI_FORMAT_R32_SINT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

static i8 GetFormatChannel(const EALGFormat::EALGFormat& format)
{
    switch (format)
    {
    case EALGFormat::kALGFormatUnknown: return -1;
    case EALGFormat::kALGFormatR8G8B8A8_UNORM: return 4;
    case EALGFormat::kALGFormatR24G8_TYPELESS: return -1;
    case EALGFormat::kALGFormatD24S8_UINT: return -1;
    case EALGFormat::kALGFormatR32_FLOAT: return 1;
    case EALGFormat::kALGFormatD32_FLOAT: return 1;
    case EALGFormat::kALGFormatR32G32B32A32_FLOAT: return 4;
    case EALGFormat::kALGFormatR32G32B32_FLOAT: return 3;
    case EALGFormat::kALGFormatR16G16B16A16_FLOAT: return 4;
    case EALGFormat::kALGFormatR11G11B10_FLOAT: return 1;
    }
    return -1;
}


static bool IsShadowMapFormat(const EALGFormat::EALGFormat& format)
{
    switch (format)
    {
    case EALGFormat::kALGFormatR8G8B8A8_UNORM: return false;
    case EALGFormat::kALGFormatD24S8_UINT: return true;
    case EALGFormat::kALGFormatR24G8_TYPELESS: return true;
    case EALGFormat::kALGFormatR32_FLOAT: return true;
    case EALGFormat::kALGFormatD32_FLOAT: return true;
    }
    return false;
}

static u16 GetPixelByteSize(const EALGFormat::EALGFormat& format)
{
    switch (format)
    {
    case EALGFormat::kALGFormatUnknown: return 65535;
        break;
    case EALGFormat::kALGFormatR8G8B8A8_UNORM: return 4;
        break;
    case EALGFormat::kALGFormatR24G8_TYPELESS: return 4;
        break;
    case EALGFormat::kALGFormatR32_FLOAT: return 4;
        break;
    case EALGFormat::kALGFormatD32_FLOAT: return 4;
        break;
    case EALGFormat::kALGFormatD16_FLOAT: return 2;
        break;
    case EALGFormat::kALGFormatD24S8_UINT: return 4;
        break;
    case EALGFormat::kALGFormatR32G32B32A32_FLOAT: return 16;
        break;
    case EALGFormat::kALGFormatR32G32B32_FLOAT: return 12;
        break;
    case EALGFormat::kALGFormatR16G16B16A16_FLOAT: return 8;
        break;
    case EALGFormat::kALGFormatR11G11B10_FLOAT: return 4;
        break;
    case EALGFormat::kALGFormatR32_UINT: return 4;
        break;
    case EALGFormat::kALGFormatR32_SINT: return 4;
        break;
    case EALGFormat::kALGFormatR16G16_FLOAT: return 4;
    case EALGFormat::kALGFormatR32G32_FLOAT: return 8;
    default:
        break;
    }
    AL_ASSERT_MSG(true, "Unknown format %d", (u32)format);
    return 65535;
}

#endif // !ALG_FORMAT_H__

