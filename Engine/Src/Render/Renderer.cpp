#include "pch.h"
#include "Render/Renderer.h"
#include "Framework/Common/Application.h"

namespace Ailu
{
    ERenderAPI Renderer::GetAPI()
    {
        return sAPI;
    }
    int Renderer::Initialize()
    {
        _p_context = new D3DContext(dynamic_cast<WinWindow*>(Application::GetInstance()->GetWindowPtr()));
        _b_init = true;
        _p_context->Init();
        _p_timemgr = new TimeMgr();
        _p_timemgr->Initialize();
        return 0;
    }
    void Renderer::Finalize()
    {
        INIT_CHECK(this, Renderer)
        DESTORY_PTR(_p_context)
        _p_timemgr->Finalize();
        DESTORY_PTR(_p_timemgr)
    }
    void Renderer::Tick()
    {
        INIT_CHECK(this, Renderer)
        ModuleTimeStatics::RenderDeltatime = _p_timemgr->GetElapsedSinceLastMark();
        _p_timemgr->Mark();
        Render();
    }
    float Renderer::GetDeltaTime() const
    {
        return _p_timemgr->GetElapsedSinceLastMark();
    }
    void Renderer::Render()
    {
        _p_context->Present();
    }
}