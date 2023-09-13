#include "pch.h"
#include "RHI/DX12/D3DRendererAPI.h"


namespace Ailu
{
	void D3DRendererAPI::SetClearColor(const Vector4f& color)
	{
		_clear_color = color;
	}
	void D3DRendererAPI::Clear()
	{
		D3DContext::GetInstance()->Clear(_clear_color, 0, true, false);
	}
	void D3DRendererAPI::DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, const Matrix4x4f& transform,uint32_t instance_count)
	{
		D3DContext::GetInstance()->DrawIndexedInstanced(index_buffer->GetCount(),instance_count, transform);
		//D3DContext::GetInstance()->GetCmdList()->DrawIndexedInstanced(index_buffer->GetCount(), instance_count, 0, 0, 0);
	}
	void D3DRendererAPI::DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, const Matrix4x4f& transform, uint32_t instance_count)
	{
		D3DContext::GetInstance()->GetCmdList()->DrawInstanced(vertex_buf->GetVertexCount(), instance_count, 0, 0);
	}
}
