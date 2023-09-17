#pragma once
#ifndef __RENDERER_API_H__
#define __RENDERER_API_H__

#include "Buffer.h"
#include "Framework/Math/ALMath.hpp"
namespace Ailu
{

	struct Viewport
	{
		uint16_t left;
		uint16_t top;
		uint16_t width;
		uint16_t height;
		Viewport(uint16_t l, uint16_t t, uint16_t w, uint16_t h)
			: left(l), top(t), width(w), height(h)
		{
		}
	};

	using ScissorRect = Viewport;

	class RendererAPI
	{
	public:
		enum class ERenderAPI : uint8_t
		{
			kNone = 0, kDirectX12
		};
	public:
		static ERenderAPI GetAPI() { return sAPI; };

		virtual void SetClearColor(const Vector4f& color) = 0;
		virtual void Clear() = 0;
		virtual void DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, const Matrix4x4f& transform,uint32_t instance_count) = 0;
		virtual void DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, const Matrix4x4f& transform, uint32_t instance_count) = 0;
		virtual void SetViewMatrix(const Matrix4x4f& view) = 0;
		virtual void SetProjectionMatrix(const Matrix4x4f& proj) = 0;
		virtual void SetViewProjectionMatrices(const Matrix4x4f& view, const Matrix4x4f& proj) = 0;
		virtual void SetViewports(const std::initializer_list<Viewport>& viewports) = 0;
		virtual void SetScissorRects(const std::initializer_list<Viewport>& rects) = 0;

	private:
		inline static ERenderAPI sAPI = ERenderAPI::kDirectX12;
	};
}


#endif // !RENDERER_API_H__
