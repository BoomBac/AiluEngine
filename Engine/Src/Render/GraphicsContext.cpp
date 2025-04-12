#include "Render/GraphicsContext.h"
#include "Framework/Common/Application.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/GPUResourceManager.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/Texture.h"
#include "pch.h"
#include <Render/CommandBuffer.h>

namespace Ailu
{
	GraphicsContext* g_pGfxContext;
	RenderTexturePool* g_pRenderTexturePool;
	void GraphicsContext::InitGlobalContext()
	{
        TimerBlock b("----------------------------------------------------------- GraphicsContext::InitGlobalContext");
		g_pGfxContext = new D3DContext(dynamic_cast<WinWindow*>(Application::Get().GetWindowPtr()));
		g_pGfxContext->Init();
		g_pRenderTexturePool = new RenderTexturePool();
		CommandPool::Init();
        GpuResourceManager::Init();
        CommandBufferPool::Init();
        RHICommandBufferPool::Init();
		GraphicsPipelineStateMgr::Init();
	}
	void GraphicsContext::FinalizeGlobalContext()
	{
		GraphicsPipelineStateMgr::Shutdown();
        RHICommandBufferPool::Shutdown();
        CommandBufferPool::Shutdown();
        GpuResourceManager::Shutdown();
		CommandPool::Shutdown();
		DESTORY_PTR(g_pRenderTexturePool);
		DESTORY_PTR(g_pGfxContext);
	}
    GraphicsContext &GraphicsContext::Get()
    {
        return *g_pGfxContext;
    }
}