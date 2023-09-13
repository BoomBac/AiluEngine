#pragma once
#ifndef __D3DRENDERER_API_H__
#define __D3DRENDERER_API_H__
#include "Render/RendererAPI.h"
#include "RHI/DX12/D3DContext.h"

namespace Ailu
{
	class D3DRendererAPI : public RendererAPI
	{
	public:
		void SetClearColor(const Vector4f& color) override;
		void Clear() override;
		void DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, const Matrix4x4f& transform,uint32_t instance_count) override;
		void DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, const Matrix4x4f& transform,uint32_t instance_count) override;
	private:
		inline static Vector4f _clear_color = { 0.3f, 0.2f, 0.4f, 1.0f };
		D3DContext* _p_d3dcontext = nullptr;
	};
}


#endif // !D3DRENDERER_API_H__

