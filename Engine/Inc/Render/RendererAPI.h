#pragma once
#ifndef __RENDERER_API_H__
#define __RENDERER_API_H__

#include "Buffer.h"
#include "Framework/Math/ALMath.hpp"
namespace Ailu
{

	struct Rect
	{
		uint16_t left;
		uint16_t top;
		uint16_t width;
		uint16_t height;
		Rect(uint16_t l, uint16_t t, uint16_t w, uint16_t h)
			: left(l), top(t), width(w), height(h)
		{
		}
		Rect() : Rect(0, 0, 0, 0) {};
	};

	using ScissorRect = Rect;

	class RendererAPI
	{
	public:
		enum class ERenderAPI : u8
		{
			kNone = 0, kDirectX12
		};
	public:
		static ERenderAPI GetAPI() { return sAPI; };
	private:
		inline static ERenderAPI sAPI = ERenderAPI::kDirectX12;
	};
}


#endif // !RENDERER_API_H__
