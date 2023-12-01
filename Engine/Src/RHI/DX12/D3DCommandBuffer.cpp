#include "pch.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DContext.h"

namespace Ailu
{
	void D3DCommandBuffer::SetClearColor(const Vector4f& color)
	{
		_commands.emplace_back([&]() {
			_clear_color = color;
			});
	}
	void D3DCommandBuffer::Clear()
	{
		_commands.clear();
	}
	void D3DCommandBuffer::ClearRenderTarget(Vector4f color, float depth, bool clear_color, bool clear_depth)
	{
		_commands.emplace_back([=]() {
			D3DContext::s_p_d3dcontext->Clear(color, depth, clear_color, clear_depth);
			});
	}
	void D3DCommandBuffer::DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, const Matrix4x4f& transform, uint32_t instance_count)
	{
		_commands.emplace_back([=]() {
			D3DContext::s_p_d3dcontext->DrawIndexedInstanced(index_buffer->GetCount(), instance_count, transform);
			});
	}
	void D3DCommandBuffer::DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, const Matrix4x4f& transform, uint32_t instance_count)
	{
		_commands.emplace_back([=]() {
			D3DContext::s_p_d3dcontext->DrawInstanced(vertex_buf->GetVertexCount(), instance_count, transform);
			});
	}
	void D3DCommandBuffer::SetViewMatrix(const Matrix4x4f& view)
	{
		_commands.emplace_back([&]() {
			D3DContext::s_p_d3dcontext->_perframe_scene_data._MatrixV = view;
			});
	}
	void D3DCommandBuffer::SetProjectionMatrix(const Matrix4x4f& proj)
	{
		_commands.emplace_back([&]() {
			D3DContext::s_p_d3dcontext->_perframe_scene_data._MatrixP = proj;
			});
	}
	void D3DCommandBuffer::SetViewProjectionMatrices(const Matrix4x4f& view, const Matrix4x4f& proj)
	{
		_commands.emplace_back([=]() {
			D3DContext::s_p_d3dcontext->_perframe_scene_data._MatrixV = view;
			D3DContext::s_p_d3dcontext->_perframe_scene_data._MatrixP = proj;
			D3DContext::s_p_d3dcontext->_perframe_scene_data._MatrixVP = proj * view;
			memcpy(D3DContext::s_p_d3dcontext->_p_cbuffer, &D3DContext::s_p_d3dcontext->_perframe_scene_data, sizeof(D3DContext::s_p_d3dcontext->_perframe_scene_data));
			});
	}
	void D3DCommandBuffer::SetViewports(const std::initializer_list<Viewport>& viewports)
	{
		static CD3DX12_VIEWPORT d3d_viewports[8];
		_commands.emplace_back([=]() {
			uint8_t nums = 0u;
			for (auto it = viewports.begin(); it != viewports.end(); it++)
			{
				d3d_viewports[nums++] = CD3DX12_VIEWPORT(it->left, it->top, it->width, it->height);
			}
			D3DContext::s_p_d3dcontext->GetCmdList()->RSSetViewports(nums, d3d_viewports);
			});
	}
	void D3DCommandBuffer::SetScissorRects(const std::initializer_list<Viewport>& rects)
	{
		static CD3DX12_RECT d3d_rects[8];
		_commands.emplace_back([=]() {
			uint8_t nums = 0u;
			for (auto it = rects.begin(); it != rects.end(); it++)
			{
				d3d_rects[nums++] = CD3DX12_RECT(it->left, it->top, it->width, it->height);
			}
			D3DContext::s_p_d3dcontext->GetCmdList()->RSSetScissorRects(nums, d3d_rects);
			});
	}
	void D3DCommandBuffer::DrawRenderer(const Ref<Mesh>& mesh, const Matrix4x4f& transform, const Ref<Material>& material, uint32_t instance_count)
	{
		_commands.emplace_back([=]() {
			if (material != nullptr)
			{
				mesh->GetVertexBuffer()->Bind(material->GetShader()->GetVSInputSemanticSeqences());
				mesh->GetIndexBuffer()->Bind();
				material->Bind();
				D3DContext::s_p_d3dcontext->DrawIndexedInstanced(mesh->GetIndexBuffer()->GetCount(), instance_count, transform);
			}
			else
			{
				static Material* error = MaterialPool::GetMaterial("Error").get();
				mesh->GetVertexBuffer()->Bind(error->GetShader()->GetVSInputSemanticSeqences());
				mesh->GetIndexBuffer()->Bind();
				error->Bind();
				D3DContext::s_p_d3dcontext->DrawIndexedInstanced(mesh->GetIndexBuffer()->GetCount(), instance_count, transform);
			}
			});
	}
	void D3DCommandBuffer::SetPSO(GraphicsPipelineState* pso)
	{
		_commands.emplace_back([=]() {
			pso->Bind();
			pso->SubmitBindResource(reinterpret_cast<void*>(D3DContext::s_p_d3dcontext->GetPerFrameCbufGPURes()), EBindResDescType::kConstBuffer);
			});
	}
	void D3DCommandBuffer::SetRenderTarget(Ref<RenderTexture> color, Ref<RenderTexture> depth)
	{
		_commands.emplace_back([=]() {
			if(color) color->Transition(ETextureResState::kRenderTagret);
			if(depth) depth->Transition(ETextureResState::kRenderTagret);
			//D3DContext::GetInstance()->GetCmdList()->OMSetRenderTargets(1,color.);
			});
	}
}
