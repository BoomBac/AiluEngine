#include "Render/GraphicsContext.h"
#include "Framework/Common/Application.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/GPUResourceManager.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/Texture.h"
#include "Render/FrameResource.h"
#include "Render/ImGuiRenderer.h"
#include "pch.h"
#include <Render/CommandBuffer.h>

namespace Ailu::Render
{
	GraphicsContext* g_pGfxContext;
	RenderTexturePool* g_pRenderTexturePool;
	void GraphicsContext::InitGlobalContext()
	{
        TimerBlock b("----------------------------------------------------------- GraphicsContext::InitGlobalContext");
		g_pRenderTexturePool = new RenderTexturePool();//在ctx之前，使得backbuffer可以被注册到池中
		g_pGfxContext = new RHI::DX12::D3DContext();
		g_pGfxContext->Init();
		CommandPool::Init();
        RHI::DX12::GpuResourceManager::Init();
        CommandBufferPool::Init();
        RHICommandBufferPool::Init();
		GraphicsPipelineStateMgr::Init();
        ConstBufferPool::Init();
        FrameResourceManager::Init();
        ImGuiRenderer::Init();
	}
	void GraphicsContext::FinalizeGlobalContext()
	{
        ImGuiRenderer::Shutdown();
        ConstBufferPool::ShutDown();
		GraphicsPipelineStateMgr::Shutdown();
        RHICommandBufferPool::Shutdown();
        CommandBufferPool::Shutdown();
        FrameResourceManager::Shutdown();
        RHI::DX12::GpuResourceManager::Shutdown();
		CommandPool::Shutdown();
		DESTORY_PTR(g_pRenderTexturePool);
		DESTORY_PTR(g_pGfxContext);
	}
    GraphicsContext &GraphicsContext::Get()
    {
        return *g_pGfxContext;
    }
}