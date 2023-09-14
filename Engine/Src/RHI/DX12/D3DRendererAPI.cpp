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
		D3DContext::s_p_d3dcontext->Clear(_clear_color, 0, true, false);
	}
	void D3DRendererAPI::DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, const Matrix4x4f& transform,uint32_t instance_count)
	{
		D3DContext::s_p_d3dcontext->DrawIndexedInstanced(index_buffer->GetCount(),instance_count, transform);
	}
	void D3DRendererAPI::DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, const Matrix4x4f& transform, uint32_t instance_count)
	{
		D3DContext::s_p_d3dcontext->GetCmdList()->DrawInstanced(vertex_buf->GetVertexCount(), instance_count, 0, 0);
	}
	void Ailu::D3DRendererAPI::SetViewMatrix(const Matrix4x4f& view)
	{
		D3DContext::s_p_d3dcontext->_perframe_scene_data._MatrixV = view;
	}
	void D3DRendererAPI::SetProjectionMatrix(const Matrix4x4f& proj)
	{
		D3DContext::s_p_d3dcontext->_perframe_scene_data._MatrixP = proj;
	}
	void D3DRendererAPI::SetViewProjectionMatrices(const Matrix4x4f& view, const Matrix4x4f& proj)
	{
		D3DContext::s_p_d3dcontext->_perframe_scene_data._MatrixV = view;
		D3DContext::s_p_d3dcontext->_perframe_scene_data._MatrixP = proj;
	}
}
