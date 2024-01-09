#include "pch.h"
#include "Render/Pass/RenderPass.h"
#include "Render/RenderingData.h"
#include "Render/RenderQueue.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/CommandBuffer.h"

namespace Ailu
{
    OpaquePass::OpaquePass() : _name("OpaquePass")
    {
    }
    //-------------------------------------------------------------OpaqueRenderPass-------------------------------------------------------------
	void OpaquePass::Execute(GraphicsContext* context)
	{
        auto cmd = CommandBufferPool::GetCommandBuffer(); 
        if (RenderingStates::s_shadering_mode == EShaderingMode::kShader || RenderingStates::s_shadering_mode == EShaderingMode::kShaderedWireFrame)
        {
            GraphicsPipelineStateMgr::ConfigureRenderTarget(RenderTargetState{}.Hash());

            for (auto& obj : RenderQueue::GetOpaqueRenderables())
            {
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
            GraphicsPipelineStateMgr::ConfigureRenderTarget(RenderTargetState{}.Hash());

            for (auto& obj : RenderQueue::GetOpaqueRenderables())
            {
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
    ShadowCastPass::ShadowCastPass() : _name("ShadowCastPass"), _rect(Rect{ 0,0,2048,2048 })
    {
        _p_shadow_map = RenderTexture::Create(2048, 2048, "MainShadowMap", EALGFormat::kALGFormatD32_FLOAT);
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
            cmd->SetViewProjectionMatrices(shadow._shadow_view, shadow._shadow_proj);
        }
        cmd->SetRenderTarget(_p_shadow_map);
        cmd->ClearRenderTarget(_p_shadow_map, Color{ 0.0f, 0.0f, 0.0f, 0.0f });
        cmd->SetScissorRect(_rect);
        cmd->SetViewport(_rect);
        static auto shadowcast_mat = MaterialLibrary::GetMaterial("Materials/ShadowCast.alasset");
        for (auto& obj : RenderQueue::GetOpaqueRenderables())
        {
            cmd->DrawRenderer(obj._mesh, shadowcast_mat.get(), obj._transform, obj._instance_count);
        }
    }
    void ShadowCastPass::BeginPass(GraphicsContext* context)
    {
    }
    void ShadowCastPass::EndPass(GraphicsContext* context)
    {
    }
    //-------------------------------------------------------------ShadowCastPass-------------------------------------------------------------
}
