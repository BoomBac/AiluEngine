#include "pch.h"
#include "Render/Texture.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DTexture.h"

namespace Ailu
{
	Ref<Texture2D> Texture2D::Create(const uint16_t& width, const uint16_t& height, EALGFormat format)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!")
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
	{
			return MakeRef<D3DTexture2D>(width, height, format);
		}
		}
		AL_ASSERT(false, "Unsupport render api!");
		return nullptr;
	}
	void Texture2D::FillData(uint8_t* data)
	{
		//_p_data = new uint8_t[_width * _height * _channel];
		//memcpy(_p_data,data, _width * _height * _channel);
		_p_data = data;
	}
	void Texture2D::Bind(uint8_t slot) const
	{

	}
	uint8_t* Texture2D::GetNativePtr()
	{
		return _p_data;
	}
	void Texture2D::Name(const std::string& name)
	{
		_name = name;
	}
	const std::string& Texture2D::Name() const
	{
		return _name;
	}
}
