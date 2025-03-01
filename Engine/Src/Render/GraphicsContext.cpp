#include "Render/GraphicsContext.h"
#include "Framework/Common/Application.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/GPUResourceManager.h"
#include "Render/Texture.h"
#include "pch.h"
#include <Render/CommandBuffer.h>
#include "Render/GraphicsPipelineStateObject.h"

namespace Ailu
{
	GraphicsContext* g_pGfxContext;
	RenderTexturePool* g_pRenderTexturePool;
	void GraphicsContext::InitGlobalContext()
	{
        TimerBlock b("----------------------------------------------------------- GraphicsContext::InitGlobalContext");
		g_pGfxContext = new D3DContext(dynamic_cast<WinWindow*>(Application::GetInstance()->GetWindowPtr()));
		g_pGfxContext->Init();
		g_pRenderTexturePool = new RenderTexturePool();
        GpuResourceManager::Init();
        CommandBufferPool::Init();
		GraphicsPipelineStateMgr::Init();
	}
	void GraphicsContext::FinalizeGlobalContext()
	{
        GpuResourceManager::Shutdown();
        CommandBufferPool::Shutdown();
		GraphicsPipelineStateMgr::Shutdown();
		DESTORY_PTR(g_pRenderTexturePool);
		DESTORY_PTR(g_pGfxContext);
	}
}