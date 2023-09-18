#pragma once
#ifndef __TEXTURE_H__
#define __TEXTURE_H__

#include <stdint.h>
#include "AlgFormat.h"

namespace Ailu
{
	class Texture
	{
	public:
		virtual ~Texture() = default;
		virtual uint8_t* GetNativePtr() = 0;
		virtual const uint16_t& GetWidth() const = 0;
		virtual const uint16_t& GetHeight() const = 0;
		virtual void Release() = 0;
	};

	class Texture2D : public Texture
	{
	public:
		static Texture2D* Create(const uint16_t& width, const uint16_t& height,EALGFormat format = EALGFormat::kALGFormatRGB32_FLOAT);
		virtual void FillData(uint8_t* data);
		uint8_t* GetNativePtr() override;
	protected:
		uint8_t* _p_data;
		uint16_t _width,_height;
		uint8_t _channel;
		EALGFormat _format;
	};
}

#endif // !TEXTURE_H__

