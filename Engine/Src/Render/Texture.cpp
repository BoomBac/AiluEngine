#include "pch.h"
#include "Render/Texture.h"

namespace Ailu
{
	Texture2D* Texture2D::Create(const uint16_t& width, const uint16_t& height, EALGFormat format)
	{
		return nullptr;
	}
	void Texture2D::FillData(uint8_t* data)
	{

	}
	uint8_t* Texture2D::GetNativePtr()
	{
		return _p_data;
	}
}
