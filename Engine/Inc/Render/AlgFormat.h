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
	kALGFormatR24G8_TYPELESS, kALGFormatR32_FLOAT, kALGFormatD32_FLOAT
};

static DXGI_FORMAT ConvertToDXGIFormat(const EALGFormat& format)
{
    switch (format)
    {
    case EALGFormat::kALGFormatR8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case EALGFormat::kALGFormatR24G8_TYPELESS: return DXGI_FORMAT_R24G8_TYPELESS;
    case EALGFormat::kALGFormatR32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    case EALGFormat::kALGFormatD32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

#endif // !ALG_FORMAT_H__

