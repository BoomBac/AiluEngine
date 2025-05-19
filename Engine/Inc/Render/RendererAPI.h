#pragma once
#ifndef __RENDERER_API_H__
#define __RENDERER_API_H__

#include "Buffer.h"
#include "Framework/Math/ALMath.hpp"
namespace Ailu::Render
{
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
