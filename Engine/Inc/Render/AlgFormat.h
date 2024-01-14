#pragma once
#ifndef __ALG_FORMAT_H__
#define __ALG_FORMAT_H__
#include <cstdint>
#include "dxgiformat.h"

enum class EALGFormat : uint8_t
{
    kALGFormatUnknown = 0,
	kALGFormatR8G8B8A8_UNORM,
	kALGFormatR24G8_TYPELESS, kALGFormatR32_FLOAT, kALGFormatD32_FLOAT, kALGFormatD24S8_UINT,
    kALGFormatR32G32B32A32_FLOAT, kALGFormatR32G32B32_FLOAT, kALGFormatR16G16B16A16_FLOAT
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
    }
    return DXGI_FORMAT_UNKNOWN;
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

#endif // !ALG_FORMAT_H__

