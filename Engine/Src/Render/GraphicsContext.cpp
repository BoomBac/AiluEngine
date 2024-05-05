#include "pch.h"
#include "Render/GraphicsContext.h"
#include "RHI/DX12/D3DContext.h"
#include "Framework/Common/Application.h"
#include "Render/Texture.h"

namespace Ailu
{
	GraphicsContext* g_pGfxContext;
	TexturePool* g_pTexturePool;
	RenderTexturePool* g_pRenderTexturePool;
	void GraphicsContext::InitGlobalContext()
	{
		g_pGfxContext = new D3DContext(dynamic_cast<WinWindow*>(Application::GetInstance()->GetWindowPtr()));
		g_pGfxContext->Init();
		g_pTexturePool = new TexturePool();
		g_pRenderTexturePool = new RenderTexturePool();
	}
	void GraphicsContext::FinalizeGlobalContext()
	{
		DESTORY_PTR(g_pTexturePool);
		DESTORY_PTR(g_pRenderTexturePool);
		DESTORY_PTR(g_pGfxContext);
	}
}