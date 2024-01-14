#include "pch.h"
#include "Render/Pass/RenderPass.h"
#include "Render/RenderingData.h"
#include "Render/RenderQueue.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/CommandBuffer.h"

namespace Ailu
{
	static Ref<RenderTexture> s_p_shadow_map = nullptr;

	OpaquePass::OpaquePass() : _name("OpaquePass")
	{
	}
	//-------------------------------------------------------------OpaqueRenderPass-------------------------------------------------------------
	void OpaquePass::Execute(GraphicsContext* context)
	{
		auto cmd = CommandBufferPool::GetCommandBuffer();
		if (RenderingStates::s_shadering_mode == EShaderingMode::kShader || RenderingStates::s_shadering_mode == EShaderingMode::kShaderedWireFrame)
		{
			//GraphicsPipelineStateMgr::ConfigureRenderTarget(RenderTargetState{}.Hash());
			for (auto& obj : RenderQueue::GetOpaqueRenderables())
			{
				//obj._mat->SetTexture("MainLightShadowMap", s_p_shadow_map);
				cmd->DrawRenderer(obj._mesh, obj._mat, obj._transform, obj._instance_count);
			}
		}
		if (RenderingStates::s_shadering_mode == EShaderingMode::kWireFrame || RenderingStates::s_shadering_mode == EShaderingMode::kShaderedWireFrame)
		{
			static auto wireframe_mat = MaterialLibrary::GetMaterial("Materials/WireFrame_new.alasset");
			for (auto& obj : RenderQueue::GetOpaqueRenderables())
			{
				cmd->DrawRenderer(obj._mesh, wireframe_mat.get(), obj._transform, obj._instance_count);
			}
		}
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::ReleaseCommandBuffer(cmd);
	}
	void OpaquePass::Execute(GraphicsContext* context, CommandBuffer* cmd)
	{
		if (RenderingStates::s_shadering_mode == EShaderingMode::kShader || RenderingStates::s_shadering_mode == EShaderingMode::kShaderedWireFrame)
		{
			//GraphicsPipelineStateMgr::ConfigureRenderTarget(RenderTargetState{}.Hash());
			for (auto& obj : RenderQueue::GetOpaqueRenderables())
			{
				//obj._mat->SetTexture("MainLightShadowMap", s_p_shadow_map);
				cmd->DrawRenderer(obj._mesh, obj._mat, obj._transform, obj._instance_count);
			}
		}
		if (RenderingStates::s_shadering_mode == EShaderingMode::kWireFrame || RenderingStates::s_shadering_mode == EShaderingMode::kShaderedWireFrame)
		{
			static auto wireframe_mat = MaterialLibrary::GetMaterial("Materials/WireFrame_new.alasset");
			for (auto& obj : RenderQueue::GetOpaqueRenderables())
			{
				cmd->DrawRenderer(obj._mesh, wireframe_mat.get(), obj._transform, obj._instance_count);
			}
		}
	}
	void OpaquePass::BeginPass(GraphicsContext* context)
	{
	}
	void OpaquePass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------OpaqueRenderPass-------------------------------------------------------------

	//-------------------------------------------------------------ReslovePass-------------------------------------------------------------
	ReslovePass::ReslovePass(Ref<RenderTexture>& source) : _name("ReslovePass")
	{
		_p_src_color = source;
	}
	void ReslovePass::Execute(GraphicsContext* context)
	{
		auto cmd = CommandBufferPool::GetCommandBuffer();
		cmd->Clear();
		cmd->ResolveToBackBuffer(_p_src_color);
		context->ExecuteCommandBuffer(cmd);
		CommandBufferPool::ReleaseCommandBuffer(cmd);
	}
	void ReslovePass::Execute(GraphicsContext* context, CommandBuffer* cmd)
	{
		cmd->ResolveToBackBuffer(_p_src_color);
	}
	void ReslovePass::BeginPass(GraphicsContext* context)
	{
	}

	void ReslovePass::BeginPass(GraphicsContext* context, Ref<RenderTexture>& source)
	{
		_p_src_color = source;
	}

	void ReslovePass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------ReslovePass-------------------------------------------------------------

	//-------------------------------------------------------------ShadowCastPass-------------------------------------------------------------
	ShadowCastPass::ShadowCastPass() : _name("ShadowCastPass"), _rect(Rect{ 0,0,kShadowMapSize,kShadowMapSize })
	{
		_p_shadow_map = RenderTexture::Create(kShadowMapSize, kShadowMapSize, "MainShadowMap", EALGFormat::kALGFormatD24S8_UINT);
		_p_shadowcast_material = MaterialLibrary::CreateMaterial(ShaderLibrary::Get("DepthOnly"), "ShadowCast");
		s_p_shadow_map = _p_shadow_map;
	}

	void ShadowCastPass::Execute(GraphicsContext* context)
	{
	}

	void ShadowCastPass::Execute(GraphicsContext* context, CommandBuffer* cmd)
	{
	}


	void ShadowCastPass::Execute(GraphicsContext* context, CommandBuffer* cmd, const RenderingData& rendering_data)
	{
		for (size_t i = 0; i < rendering_data._actived_shadow_count; i++)
		{
			auto shadow = rendering_data._shadow_data[i];
			cmd->SetShadowMatrix(shadow._shadow_view * shadow._shadow_proj, 0);
		}
		cmd->SetRenderTarget(nullptr, _p_shadow_map.get());
		cmd->ClearRenderTarget(_p_shadow_map.get(), 1.0f);
		cmd->SetScissorRect(_rect);
		cmd->SetViewport(_rect);
		for (auto& obj : RenderQueue::GetOpaqueRenderables())
		{
			cmd->DrawRenderer(obj._mesh, _p_shadowcast_material.get(), obj._transform, obj._instance_count);
		}
		Shader::SetGlobalTexture("MainLightShadowMap", _p_shadow_map.get());
	}
	void ShadowCastPass::BeginPass(GraphicsContext* context)
	{
	}
	void ShadowCastPass::EndPass(GraphicsContext* context)
	{
	}
	//-------------------------------------------------------------ShadowCastPass-------------------------------------------------------------
}
