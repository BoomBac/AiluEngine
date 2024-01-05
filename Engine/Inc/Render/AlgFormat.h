#pragma once
#ifndef __ALG_FORMAT_H__
#define __ALG_FORMAT_H__
#include <cstdint>
#include "dxgiformat.h"

enum class EALGFormat : uint8_t
{
	kALGFormatR8G8B8A8_UNORM = 0,
	kALGFormatRGB32_FLOAT,
	kALGFormatRGBA32_FLOAT,
	kALGFormatR24G8_TYPELESS, kALGFormatR32_FLOAT, kALGFormatD32_FLOAT, kALGFormatD24S8_UINT
};

static DXGI_FORMAT ConvertToDXGIFormat(const EALGFormat& format)
{
    switch (format)
    {
    case EALGFormat::kALGFormatR8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case EALGFormat::kALGFormatR24G8_TYPELESS: return DXGI_FORMAT_R24G8_TYPELESS;
    case EALGFormat::kALGFormatD24S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case EALGFormat::kALGFormatR32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    case EALGFormat::kALGFormatD32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
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

#endif // !ALG_FORMAT_H__

