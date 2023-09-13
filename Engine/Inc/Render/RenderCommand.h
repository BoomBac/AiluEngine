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
		static void SetClearColor(const Vector4f& color)
		{
			s_p_renderer_api->SetClearColor(color);
		}
		static void Clear()
		{
			s_p_renderer_api->Clear();
		}
		static void DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, uint32_t instance_count,Matrix4x4f transform = BuildIdentityMatrix())
		{
			s_p_renderer_api->DrawIndexedInstanced(index_buffer, transform, instance_count);
		}

		static inline void DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, uint32_t instance_count,Matrix4x4f transform = BuildIdentityMatrix())
		{
			s_p_renderer_api->DrawInstanced(vertex_buf, transform, instance_count);
		}
	private:
		static inline RendererAPI* s_p_renderer_api = new D3DRendererAPI();
	};
}


#endif // !__RENDER_CMD_H__

