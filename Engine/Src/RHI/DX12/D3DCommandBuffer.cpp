#include "pch.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"
#include "Render/RenderingData.h"
#include "Render/Gizmo.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Ext/pix/Include/WinPixEventRuntime/pix3.h"

namespace Ailu
{
	D3DCommandBuffer::D3DCommandBuffer()
	{
		auto dev = D3DContext::Get()->GetDevice();
		ThrowIfFailed(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(_p_alloc.GetAddressOf())));
		ThrowIfFailed(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _p_alloc.Get(), nullptr, IID_PPV_ARGS(_p_cmd.GetAddressOf())));
		_b_cmd_closed = false;
	}
	D3DCommandBuffer::D3DCommandBuffer(u32 id) : D3DCommandBuffer()
	{
		_id = id;
	}

	void D3DCommandBuffer::SubmitBindResource(void* res, const EBindResDescType& res_type, u8 slot)
	{
		GraphicsPipelineStateMgr::SubmitBindResource(res, res_type, slot);
	}

	void D3DCommandBuffer::Clear()
	{
		if (_b_cmd_closed)
		{
			ThrowIfFailed(_p_alloc->Reset());
			ThrowIfFailed(_p_cmd->Reset(_p_alloc.Get(), nullptr));
			_b_cmd_closed = false;
		}
		_cur_cbv_heap_id = 65535;
	}
	void D3DCommandBuffer::Close()
	{
		if (!_b_cmd_closed)
		{
			ThrowIfFailed(_p_cmd->Close());
			_b_cmd_closed = true;
		}
	}
	void D3DCommandBuffer::ClearRenderTarget(Vector4f color, float depth, bool clear_color, bool clear_depth)
	{
		throw std::runtime_error("error");
	}
	void D3DCommandBuffer::ClearRenderTarget(Ref<RenderTexture>& color, Ref<RenderTexture>& depth, Vector4f clear_color, float clear_depth)
	{
		_p_cmd->ClearRenderTargetView(*reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(color->GetNativeCPUHandle()), clear_color, 0, nullptr);
		_p_cmd->ClearDepthStencilView(*reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(depth->GetNativeCPUHandle()), D3D12_CLEAR_FLAG_DEPTH, clear_depth, 0, 0, nullptr);
	}
	void D3DCommandBuffer::ClearRenderTarget(Vector<Ref<RenderTexture>>& colors, Ref<RenderTexture>& depth, Vector4f clear_color, float clear_depth)
	{
		for (auto& color : colors)
			_p_cmd->ClearRenderTargetView(*reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(color->GetNativeCPUHandle()), clear_color, 0, nullptr);
		_p_cmd->ClearDepthStencilView(*reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(depth->GetNativeCPUHandle()), D3D12_CLEAR_FLAG_DEPTH, clear_depth, 0, 0, nullptr);
	}
	void D3DCommandBuffer::ClearRenderTarget(Ref<RenderTexture>& color, Vector4f clear_color, u16 index)
	{
		_p_cmd->ClearRenderTargetView(*reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(color->GetNativeCPUHandle(index)), clear_color, 0, nullptr);
	}
	void D3DCommandBuffer::ClearRenderTarget(RenderTexture* depth, float depth_value, u8 stencil_value)
	{
		_p_cmd->ClearDepthStencilView(*reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(depth->GetNativeCPUHandle()), D3D12_CLEAR_FLAG_DEPTH, depth_value, stencil_value, 0, nullptr);
	}

	void D3DCommandBuffer::DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, const Matrix4x4f& transform, u32 instance_count)
	{
		++RenderingStates::s_draw_call;
		_p_cmd->DrawIndexedInstanced(index_buffer->GetCount(), instance_count, 0, 0, 0);
	}
	void D3DCommandBuffer::DrawIndexedInstanced(u32 index_count, u32 instance_count)
	{
		++RenderingStates::s_draw_call;
		_p_cmd->DrawIndexedInstanced(index_count, instance_count, 0, 0, 0);
	}
	void D3DCommandBuffer::DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, const Matrix4x4f& transform, u32 instance_count)
	{
		++RenderingStates::s_draw_call;
		_p_cmd->DrawInstanced(vertex_buf->GetVertexCount(), instance_count, 0, 0);
	}
	void D3DCommandBuffer::DrawInstanced(u32 vert_count, u32 instance_count)
	{
		++RenderingStates::s_draw_call;
		_p_cmd->DrawInstanced(vert_count, instance_count, 0, 0);
	}

	void D3DCommandBuffer::SetViewports(const std::initializer_list<Rect>& viewports)
	{
		static CD3DX12_VIEWPORT d3d_viewports[8];
		u8 nums = 0u;
		for (auto it = viewports.begin(); it != viewports.end(); it++)
		{
			d3d_viewports[nums++] = CD3DX12_VIEWPORT(it->left, it->top, it->width, it->height);
		}
		_p_cmd->RSSetViewports(nums, d3d_viewports);
	}
	void D3DCommandBuffer::SetViewport(const Rect& viewport)
	{
		auto rect = CD3DX12_VIEWPORT(viewport.left, viewport.top, viewport.width, viewport.height);
		_p_cmd->RSSetViewports(1, &rect);
	}
	void D3DCommandBuffer::SetScissorRects(const std::initializer_list<Rect>& rects)
	{
		static CD3DX12_RECT d3d_rects[8];
		u8 nums = 0u;
		for (auto it = rects.begin(); it != rects.end(); it++)
		{
			d3d_rects[nums++] = CD3DX12_RECT(it->left, it->top, it->width, it->height);
		}
		_p_cmd->RSSetScissorRects(nums, d3d_rects);
	}
	void D3DCommandBuffer::SetScissorRect(const Rect& rect)
	{
		auto d3d_rect = CD3DX12_RECT(rect.left, rect.top, rect.width, rect.height);
		_p_cmd->RSSetScissorRects(1, &d3d_rect);
	}
	u16 D3DCommandBuffer::DrawRenderer(const Ref<Mesh>& mesh, const Matrix4x4f& transform, const Ref<Material>& material, u32 instance_count)
	{
		throw std::runtime_error("error");
		return 0;
	}
	u16 D3DCommandBuffer::DrawRenderer(Mesh* mesh, Material* material, const Matrix4x4f& transform, u32 instance_count)
	{
		throw std::runtime_error("error");
		return 0;
	}
	u16 D3DCommandBuffer::DrawRenderer(Mesh* mesh, Material* material, ConstantBuffer* per_obj_cbuf, u32 instance_count)
	{
		if (mesh)
		{
			for (int i = 0; i < mesh->SubmeshCount(); ++i)
			{
				DrawRenderer(mesh, material, per_obj_cbuf, i, instance_count);
			}
		}
		return 0;
	}

	u16 D3DCommandBuffer::DrawRenderer(Mesh* mesh, Material* material, ConstantBuffer* per_obj_cbuf, u16 submesh_index, u32 instance_count)
	{
		if (mesh == nullptr || !mesh->_is_rhi_res_ready || material == nullptr || material->GetShader()->IsCompileError())
		{
			//LOG_WARNING("Mesh/Material is null or is not ready when draw renderer!");
			return 1;
		}
		material->Bind();
		GraphicsPipelineStateMgr::EndConfigurePSO(this);
		if (GraphicsPipelineStateMgr::IsReadyForCurrentDrawCall())
		{
			mesh->GetVertexBuffer()->Bind(this, material->GetShader()->PipelineInputLayout());
			mesh->GetIndexBuffer(submesh_index)->Bind(this);
			per_obj_cbuf->Bind(this, 0);
			++RenderingStates::s_draw_call;
			_p_cmd->DrawIndexedInstanced(mesh->GetIndicesCount(submesh_index), instance_count, 0, 0, 0);
		}
		else
		{
			LOG_WARNING("GraphicsPipelineStateMgr is not ready for current draw call! with material: {}", material != nullptr ? material->Name() : "null");
			return 2;
		}
		return 0;
	}

	u16 D3DCommandBuffer::DrawRenderer(Mesh* mesh, Material* material, u32 instance_count)
	{
		if (mesh == nullptr || !mesh->_is_rhi_res_ready || material == nullptr || material->GetShader()->IsCompileError())
		{
			LOG_WARNING("Mesh/Material is null or is not ready when draw renderer!");
			return 1;
		}
		material->Bind();
		GraphicsPipelineStateMgr::EndConfigurePSO(this);
		if (GraphicsPipelineStateMgr::IsReadyForCurrentDrawCall())
		{
			mesh->GetVertexBuffer()->Bind(this, material->GetShader()->PipelineInputLayout());
			mesh->GetIndexBuffer()->Bind(this);
			++RenderingStates::s_draw_call;
			_p_cmd->DrawIndexedInstanced(mesh->GetIndexBuffer()->GetCount(), instance_count, 0, 0, 0);
		}
		else
		{
			LOG_WARNING("GraphicsPipelineStateMgr is not ready for current draw call! with material: {}", material != nullptr ? material->Name() : "null");
			return 2;
		}
		return 0;
	}

	void D3DCommandBuffer::SetRenderTarget(Ref<RenderTexture>& color, Ref<RenderTexture>& depth)
	{
		SetRenderTarget(color.get(), depth.get());
	}

	void D3DCommandBuffer::SetRenderTargets(Vector<Ref<RenderTexture>>& colors, Ref<RenderTexture>& depth)
	{
		GraphicsPipelineStateMgr::ResetRenderTargetState();
		u16 rt_num = static_cast<u16>(colors.size());
		static D3D12_CPU_DESCRIPTOR_HANDLE handles[8];
		for (int i = 0; i < rt_num; i++)
		{
			GraphicsPipelineStateMgr::SetRenderTargetState(colors[i]->GetFormat(), depth->GetFormat(), i);
			colors[i]->Transition(this, ETextureResState::kColorTagret);
			depth->Transition(this, ETextureResState::kDepthTarget);
			handles[i] = *reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(colors[i]->GetNativeCPUHandle());
		}
		_p_cmd->OMSetRenderTargets(rt_num, handles, false, reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(depth->GetNativeCPUHandle()));
	}

	void D3DCommandBuffer::SetRenderTarget(Ref<RenderTexture>& color, u16 index)
	{
		GraphicsPipelineStateMgr::ResetRenderTargetState();
		GraphicsPipelineStateMgr::SetRenderTargetState(color->GetFormat(), EALGFormat::kALGFormatUnknown, 0);
		color->Transition(this, ETextureResState::kColorTagret);
		_p_cmd->OMSetRenderTargets(1, reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(color->GetNativeCPUHandle(index)), 0,
			NULL);
	}

	void D3DCommandBuffer::SetRenderTarget(RenderTexture* color, RenderTexture* depth)
	{
		GraphicsPipelineStateMgr::ResetRenderTargetState();
		if (color && depth)
		{
			//if (pre_color  && *color == *pre_color)
			//	return;
			GraphicsPipelineStateMgr::SetRenderTargetState(color->GetFormat(), depth->GetFormat(), 0);
			color->Transition(this, ETextureResState::kColorTagret);
			depth->Transition(this, ETextureResState::kDepthTarget);
			_p_cmd->OMSetRenderTargets(1, reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(color->GetNativeCPUHandle()), 0,
				reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(depth->GetNativeCPUHandle()));
		}
		else
		{
			if (color == nullptr)
			{
				GraphicsPipelineStateMgr::SetRenderTargetState(EALGFormat::kALGFormatUnknown, depth->GetFormat(), 0);
				depth->Transition(this, ETextureResState::kDepthTarget);
				_p_cmd->OMSetRenderTargets(0, nullptr, 0, reinterpret_cast<D3D12_CPU_DESCRIPTOR_HANDLE*>(depth->GetNativeCPUHandle()));
			}
		}
	}
	void D3DCommandBuffer::ResolveToBackBuffer(Ref<RenderTexture>& color)
	{
		static const auto& mat = BuildIdentityMatrix();
		static const auto mesh = MeshPool::GetMesh("FullScreenQuad");
		static const auto material = MaterialLibrary::GetMaterial("Blit");
		D3DContext::Get()->BeginBackBuffer(this);
		mesh->GetVertexBuffer()->Bind(this, material->GetShader()->PipelineInputLayout());
		mesh->GetIndexBuffer()->Bind(this);
		material->SetTexture("_SourceTex", color);
		material->Bind();
		GraphicsPipelineStateMgr::EndConfigurePSO(this);
		if (GraphicsPipelineStateMgr::IsReadyForCurrentDrawCall())
		{
#ifdef _PIX_DEBUG
			PIXBeginEvent(_p_cmd.Get(), 100, "FinalBlitPass");
			DrawIndexedInstanced(mesh->GetIndexBuffer()->GetCount(), 1);
			PIXEndEvent(_p_cmd.Get());
			PIXBeginEvent(_p_cmd.Get(), 105, "GizmoPass");
			GraphicsPipelineStateMgr::s_gizmo_pso->Bind(this);
			GraphicsPipelineStateMgr::s_gizmo_pso->SetPipelineResource(this, Shader::GetPerFrameConstBuffer(), EBindResDescType::kConstBuffer);
			Gizmo::Submit(this);
			PIXEndEvent(_p_cmd.Get());
			PIXBeginEvent(_p_cmd.Get(), 110, "GUIPass");
			D3DContext::Get()->DrawOverlay(this);
			PIXEndEvent(_p_cmd.Get());
#else
			DrawIndexedInstanced(mesh->GetIndexBuffer()->GetCount(), 1);
			GraphicsPipelineStateMgr::s_gizmo_pso->Bind(this);
			GraphicsPipelineStateMgr::s_gizmo_pso->SetPipelineResource(this, Shader::GetPerFrameConstBuffer(), EBindResDescType::kConstBuffer);
			Gizmo::Submit(this);
			D3DContext::s_p_d3dcontext->DrawOverlay(this);
#endif // _PIX_DEBUG


		}
		D3DContext::Get()->EndBackBuffer(this);
	}

	void D3DCommandBuffer::Dispatch(ComputeShader* cs, u16 thread_group_x, u16 thread_group_y, u16 thread_group_z)
	{
		cs->Bind(this, thread_group_x, thread_group_y, thread_group_z);
	}
}
