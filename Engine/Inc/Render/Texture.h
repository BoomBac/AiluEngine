#pragma once
#ifndef __TEXTURE_H__
#define __TEXTURE_H__

#include <stdint.h>
#include "GlobalMarco.h"
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
		virtual void Bind(uint8_t slot) const = 0;
		virtual void Name(const std::string& name) = 0;
		virtual const std::string& Name() const = 0;
	};

	class Texture2D : public Texture
	{
	public:
		static Ref<Texture2D> Create(const uint16_t& width, const uint16_t& height,EALGFormat format = EALGFormat::kALGFormatRGB32_FLOAT);
		virtual void FillData(uint8_t* data);
		virtual void Bind(uint8_t slot) const override;
		const uint16_t& GetWidth() const final { return _width; };
		const uint16_t& GetHeight() const final { return _height; };
		uint8_t* GetNativePtr() override;
	public:
		void Name(const std::string& name) override;
		const std::string& Name() const override;
	protected:
		std::string _name;
		uint8_t* _p_data;
		uint16_t _width,_height;
		uint8_t _channel;
		EALGFormat _format;
	};
}

#endif // !TEXTURE_H__

