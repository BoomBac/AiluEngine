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
		static void DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, uint32_t instance_count)
		{
			s_p_renderer_api->DrawIndexedInstanced(index_buffer, instance_count);
		}

		static inline void DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, uint32_t instance_count)
		{
			s_p_renderer_api->DrawInstanced(vertex_buf, instance_count);
		}
	private:
		static inline RendererAPI* s_p_renderer_api = new D3DRendererAPI();
	};
}


#endif // !__RENDER_CMD_H__

