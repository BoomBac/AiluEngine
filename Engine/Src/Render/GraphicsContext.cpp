#include "pch.h"
#include "Render/GraphicsContext.h"
#include "RHI/DX12/D3DContext.h"
#include "Framework/Common/Application.h"

namespace Ailu
{
	GraphicsContext* g_pGfxContext;
	void GraphicsContext::InitGlobalContext()
	{
		g_pGfxContext = new D3DContext(dynamic_cast<WinWindow*>(Application::GetInstance()->GetWindowPtr()));
		g_pGfxContext->Init();
	}
	void GraphicsContext::FinalizeGlobalContext()
	{
		delete g_pGfxContext;
	}
}