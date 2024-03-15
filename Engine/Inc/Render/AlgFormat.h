#pragma once
#ifndef __ALG_FORMAT_H__
#define __ALG_FORMAT_H__
#include <cstdint>
#include "dxgiformat.h"

enum class EALGFormat : u8
{
    kALGFormatUnknown = 0,
	kALGFormatR8G8B8A8_UNORM,
	kALGFormatR24G8_TYPELESS, kALGFormatR32_FLOAT, kALGFormatD32_FLOAT, kALGFormatD24S8_UINT,
    kALGFormatR32G32B32A32_FLOAT, kALGFormatR32G32B32_FLOAT, kALGFormatR16G16B16A16_FLOAT, kALGFormatR16G16_FLOAT,kALGFormatR32_UINT, kALGFormatR32_SINT
};

static DXGI_FORMAT ConvertToDXGIFormat(const EALGFormat& format)
{
    switch (format)
    {
    case EALGFormat::kALGFormatUnknown: return DXGI_FORMAT_UNKNOWN;
    case EALGFormat::kALGFormatR8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case EALGFormat::kALGFormatR24G8_TYPELESS: return DXGI_FORMAT_R24G8_TYPELESS;
    case EALGFormat::kALGFormatD24S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case EALGFormat::kALGFormatR32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    case EALGFormat::kALGFormatD32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
    case EALGFormat::kALGFormatR32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case EALGFormat::kALGFormatR32G32B32_FLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
    case EALGFormat::kALGFormatR16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case EALGFormat::kALGFormatR16G16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
    case EALGFormat::kALGFormatR32_UINT: return DXGI_FORMAT_R32_UINT;
    case EALGFormat::kALGFormatR32_SINT: return DXGI_FORMAT_R32_SINT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

static i8 GetFormatChannel(const EALGFormat& format)
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
    }
    return -1;
}


static bool IsShadowMapFormat(const EALGFormat& format)
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

static bool IsHDRFormat(const EALGFormat& format)
{
    return format == EALGFormat::kALGFormatR32G32B32A32_FLOAT || format == EALGFormat::kALGFormatR32G32B32_FLOAT;
}

static u16 GetPixelByteSize(const EALGFormat& format)
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
    case EALGFormat::kALGFormatD24S8_UINT: return 4;
        break;
    case EALGFormat::kALGFormatR32G32B32A32_FLOAT: return 16;
        break;
    case EALGFormat::kALGFormatR32G32B32_FLOAT: return 12;
        break;
    case EALGFormat::kALGFormatR16G16B16A16_FLOAT: return 8;
        break;
    case EALGFormat::kALGFormatR32_UINT: return 4;
        break;
    case EALGFormat::kALGFormatR32_SINT: return 4;
        break;
    default:
        break;
    }
    return 65535;
}

#endif // !ALG_FORMAT_H__

