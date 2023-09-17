#pragma once
#ifndef __RENDER_CMD_H__
#define __RENDER_CMD_H__

#include "RendererAPI.h"
#include "Framework/Math/ALMath.hpp"
#include "RHI/DX12/D3DRendererAPI.h"
namespace Ailu
{
	class RenderCommand
	{
	public:
		static void SetClearColor(const Vector4f& color);
		static void Clear();
		static void DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, uint32_t instance_count, Matrix4x4f transform = BuildIdentityMatrix());
		static void DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, uint32_t instance_count, Matrix4x4f transform = BuildIdentityMatrix());
		static void SetViewProjectionMatrices(const Matrix4x4f& view, const Matrix4x4f& proj);
		static void SetProjectionMatrix(const Matrix4x4f& proj);
		static void SetViewMatrix(const Matrix4x4f& view);
		static void SetViewports(const std::initializer_list<Viewport>& viewports);
		static void SetScissorRects(const std::initializer_list<Viewport>& rects);
	private:
		static inline RendererAPI* s_p_renderer_api = new D3DRendererAPI();
	};
}


#endif // !__RENDER_CMD_H__

