#pragma once
#ifndef __ALG_FORMAT_H__
#define __ALG_FORMAT_H__
#include <cstdint>

enum class EALGFormat : uint8_t
{
	kALGFormatR8G8B8A8_UNORM = 0,
	kALGFormatR24G8_TYPELESS, kALGFormatR32_FLOAT, kALGFormatD32_FLOAT
};

#endif // !ALG_FORMAT_H__

