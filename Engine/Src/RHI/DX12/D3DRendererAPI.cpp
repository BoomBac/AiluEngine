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
		D3DContext::s_p_d3dcontext->Clear(_clear_color, 1.0f, true, true);
	}
	void D3DRendererAPI::DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, const Matrix4x4f& transform,uint32_t instance_count)
	{
		D3DContext::s_p_d3dcontext->DrawIndexedInstanced(index_buffer->GetCount(),instance_count, transform);
	}
	void D3DRendererAPI::DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, const Matrix4x4f& transform, uint32_t instance_count)
	{
		D3DContext::s_p_d3dcontext->DrawInstanced(vertex_buf->GetVertexCount(), instance_count, transform);
	}
	void Ailu::D3DRendererAPI::SetViewMatrix(const Matrix4x4f& view)
	{
		auto device = D3DContext::s_p_d3dcontext;
		device->_perframe_scene_data._MatrixV = view;
		device->_perframe_scene_data._MatrixVP = device->_perframe_scene_data._MatrixP * view;
		memcpy(device->_p_cbuffer, &device->_perframe_scene_data, sizeof(device->_perframe_scene_data));
	}
	void D3DRendererAPI::SetProjectionMatrix(const Matrix4x4f& proj)
	{
		auto device = D3DContext::s_p_d3dcontext;
		device->_perframe_scene_data._MatrixP = proj;
		device->_perframe_scene_data._MatrixVP = proj * device->_perframe_scene_data._MatrixV;
		memcpy(device->_p_cbuffer, &device->_perframe_scene_data, sizeof(device->_perframe_scene_data));
	}
	void D3DRendererAPI::SetViewProjectionMatrices(const Matrix4x4f& view, const Matrix4x4f& proj)
	{
		auto device = D3DContext::s_p_d3dcontext;
		device->_perframe_scene_data._MatrixV = view;
		device->_perframe_scene_data._MatrixP = proj;
		device->_perframe_scene_data._MatrixVP = proj * view;
		memcpy(device->_p_cbuffer, &device->_perframe_scene_data, sizeof(device->_perframe_scene_data));

	}
	void D3DRendererAPI::SetViewports(const std::initializer_list<Rect>& viewports)
	{
		static CD3DX12_VIEWPORT d3d_viewports[8];
		uint8_t nums = 0u;
		for (auto it = viewports.begin(); it != viewports.end(); it++)
		{
			d3d_viewports[nums++] = CD3DX12_VIEWPORT(it->left,it->top,it->width,it->height);
		}
		D3DContext::s_p_d3dcontext->GetCmdList()->RSSetViewports(nums, d3d_viewports);
	}
	void D3DRendererAPI::SetScissorRects(const std::initializer_list<Rect>& rects)
	{
		static CD3DX12_RECT d3d_rects[8];
		uint8_t nums = 0u;
		for (auto it = rects.begin(); it != rects.end(); it++)
		{
			d3d_rects[nums++] = CD3DX12_RECT(it->left, it->top, it->width, it->height);
		}
		D3DContext::s_p_d3dcontext->GetCmdList()->RSSetScissorRects(nums, d3d_rects);
	}
}
