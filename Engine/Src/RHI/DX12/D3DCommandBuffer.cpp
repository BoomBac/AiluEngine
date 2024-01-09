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
	void D3DCommandBuffer::ClearRenderTarget(Ref<RenderTexture> color, Ref<RenderTexture> depth, Vector4f clear_color, float clear_depth)
	{
		_commands.emplace_back([=]() {
			D3DContext::GetInstance()->GetCmdList()->ClearRenderTargetView(*reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(color->GetNativeCPUHandle()), clear_color, 0, nullptr);
			D3DContext::GetInstance()->GetCmdList()->ClearDepthStencilView(*reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(depth->GetNativeCPUHandle()), D3D12_CLEAR_FLAG_DEPTH, clear_depth, 0, 0, nullptr);
			});

	}
	void D3DCommandBuffer::ClearRenderTarget(Ref<RenderTexture>& color, Vector4f clear_color)
	{
		_commands.emplace_back([=]() {
			D3DContext::GetInstance()->GetCmdList()->ClearRenderTargetView(*reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(color->GetNativeCPUHandle()), clear_color, 1, nullptr);
			});
	}
	//void D3DCommandBuffer::ClearRenderTarget(Ref<RenderTexture> color, Ref<RenderTexture> depth, Vector4f clear_color, float clear_depth)
	//{
	//}
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
			//D3DContext::s_p_d3dcontext->_perframe_scene_data._MatrixVP = proj * view;
			D3DContext::s_p_d3dcontext->_perframe_scene_data._MatrixVP = view * proj;
			memcpy(D3DContext::s_p_d3dcontext->_p_cbuffer, &D3DContext::s_p_d3dcontext->_perframe_scene_data, sizeof(D3DContext::s_p_d3dcontext->_perframe_scene_data));
			});
	}
	void D3DCommandBuffer::SetViewports(const std::initializer_list<Rect>& viewports)
	{
		static CD3DX12_VIEWPORT d3d_viewports[8];
		_commands.emplace_back([=]() {
			u8 nums = 0u;
			for (auto it = viewports.begin(); it != viewports.end(); it++)
			{
				d3d_viewports[nums++] = CD3DX12_VIEWPORT(it->left, it->top, it->width, it->height);
			}
			D3DContext::s_p_d3dcontext->GetCmdList()->RSSetViewports(nums, d3d_viewports);
			});
	}
	void D3DCommandBuffer::SetViewport(const Rect& viewport)
	{
		_commands.emplace_back([=]() {
			auto rect = CD3DX12_VIEWPORT(viewport.left, viewport.top, viewport.width, viewport.height);
			D3DContext::s_p_d3dcontext->GetCmdList()->RSSetViewports(1, &rect);
			});
	}
	void D3DCommandBuffer::SetScissorRects(const std::initializer_list<Rect>& rects)
	{
		static CD3DX12_RECT d3d_rects[8];
		_commands.emplace_back([=]() {
			u8 nums = 0u;
			for (auto it = rects.begin(); it != rects.end(); it++)
			{
				d3d_rects[nums++] = CD3DX12_RECT(it->left, it->top, it->width, it->height);
			}
			D3DContext::s_p_d3dcontext->GetCmdList()->RSSetScissorRects(nums, d3d_rects);
			});
	}
	void D3DCommandBuffer::SetScissorRect(const Rect& rect)
	{
		_commands.emplace_back([=]() {
			auto d3d_rect = CD3DX12_RECT(rect.left, rect.top, rect.width, rect.height);
			D3DContext::s_p_d3dcontext->GetCmdList()->RSSetScissorRects(1, &d3d_rect);
			});
	}
	void D3DCommandBuffer::DrawRenderer(const Ref<Mesh>& mesh, const Matrix4x4f& transform, const Ref<Material>& material, uint32_t instance_count)
	{
		DrawRenderer(mesh.get(),material.get(),transform,instance_count);
	}
	void D3DCommandBuffer::DrawRenderer(Mesh* mesh, Material* material, const Matrix4x4f& transform, uint32_t instance_count)
	{
		if (mesh == nullptr)
		{
			LOG_WARNING("Mesh is null when draw renderer!");
			return;
		}
		_commands.emplace_back([=]() {
			if (material != nullptr)
			{
				mesh->GetVertexBuffer()->Bind(material->GetShader()->GetVSInputSemanticSeqences());
				mesh->GetIndexBuffer()->Bind();
				material->Bind();
			}
			else
			{
				static Material* error = MaterialLibrary::GetMaterial("Error").get();
				mesh->GetVertexBuffer()->Bind(error->GetShader()->GetVSInputSemanticSeqences());
				mesh->GetIndexBuffer()->Bind();
				error->Bind();
			}
			GraphicsPipelineStateMgr::EndConfigurePSO();
			if (GraphicsPipelineStateMgr::IsReadyForCurrentDrawCall())
			{
				D3DContext::s_p_d3dcontext->SubmitPerObjectBuffer(transform);
				D3DContext::s_p_d3dcontext->DrawIndexedInstanced(mesh->GetIndexBuffer()->GetCount(), instance_count, transform);
			}
			else
			{
				LOG_WARNING("GraphicsPipelineStateMgr is not ready for current draw call! with material: {}", material != nullptr ? material->Name() : "null");
			}
			});
	}
	void D3DCommandBuffer::SetPSO(GraphicsPipelineStateObject* pso)
	{
		_commands.emplace_back([=]() {
			pso->Bind();
			pso->SetPipelineResource(reinterpret_cast<void*>(D3DContext::s_p_d3dcontext->GetPerFrameCbufGPURes()), EBindResDescType::kConstBuffer);
			});
	}
	void D3DCommandBuffer::SetRenderTarget(Ref<RenderTexture>& color, Ref<RenderTexture>& depth)
	{
		GraphicsPipelineStateMgr::SetRenderTargetState(color->GetFormat(), depth->GetFormat(),0);
		_commands.emplace_back([=]() {
			color->Transition(ETextureResState::kColorTagret);
			depth->Transition(ETextureResState::kDepthTarget);
			D3DContext::GetInstance()->GetCmdList()->OMSetRenderTargets(1, reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(color->GetNativeCPUHandle()), 0,
				reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(depth->GetNativeCPUHandle()));
			});
	}
	void D3DCommandBuffer::SetRenderTarget(Ref<RenderTexture>& color)
	{
		GraphicsPipelineStateMgr::SetRenderTargetState(color->GetFormat(),0);
		_commands.emplace_back([=]() {
			color->Transition(ETextureResState::kColorTagret);
			D3DContext::GetInstance()->GetCmdList()->OMSetRenderTargets(1, reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(color->GetNativeCPUHandle()), 0,
				NULL);
			});
	}
	void Ailu::D3DCommandBuffer::ResolveToBackBuffer(Ref<RenderTexture>& color)
	{
		static const auto& mat = BuildIdentityMatrix();
		static const auto mesh = MeshPool::GetMesh("FullScreenQuad");
		static const auto material = MaterialLibrary::GetMaterial("Blit");
		_commands.emplace_back([=]() {
			D3DContext::s_p_d3dcontext->BeginBackBuffer();
			mesh->GetVertexBuffer()->Bind(material->GetShader()->GetVSInputSemanticSeqences());
			mesh->GetIndexBuffer()->Bind();
			color->Transition(ETextureResState::kShaderResource);
			material->SetTexture("_SourceTex", color);
			material->Bind();
			GraphicsPipelineStateMgr::EndConfigurePSO();
			D3DContext::s_p_d3dcontext->DrawIndexedInstanced(mesh->GetIndexBuffer()->GetCount(), 1, mat);
			D3DContext::s_p_d3dcontext->DrawOverlay();
			D3DContext::s_p_d3dcontext->EndBackBuffer();
			});
	}
	void D3DCommandBuffer::ResolveToBackBuffer(RenderTexture* color)
	{
		throw std::runtime_error("The method or operation is not implemented.");
	}
}
