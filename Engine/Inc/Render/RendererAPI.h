#pragma once
#ifndef __RENDERER_API_H__
#define __RENDERER_API_H__

#include "Buffer.h"
#include "Framework/Math/ALMath.hpp"
namespace Ailu
{
	class RendererAPI
	{
	public:
		enum class ERenderAPI : uint8_t
		{
			kNone = 0, kDirectX12
		};
	public:
		virtual void SetClearColor(const Vector4f& color) = 0;
		virtual void Clear() = 0;
		virtual void DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, const Matrix4x4f& transform,uint32_t instance_count) = 0;
		virtual void DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, const Matrix4x4f& transform, uint32_t instance_count) = 0;
		static ERenderAPI GetAPI() { return sAPI; };
	private:
		inline static ERenderAPI sAPI = ERenderAPI::kDirectX12;
	};
}


#endif // !RENDERER_API_H__
