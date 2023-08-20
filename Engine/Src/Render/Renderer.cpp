#include "pch.h"
#include "Render/Renderer.h"

namespace Ailu
{
    int Renderer::Initialize()
    {
        _p_renderer = new DXBaseRenderer(1280, 720);
        _b_init = true;
        return 0;
    }
    void Renderer::Finalize()
    {
        INIT_CHECK(this, Renderer)
        _p_renderer->OnDestroy();
        DESTORY_PTR(_p_renderer)
    }
    void Renderer::Tick()
    {
        INIT_CHECK(this,Renderer)
        _p_renderer->OnUpdate();
        Render();
    }
    void Renderer::Render()
    {
        _p_renderer->OnRender();
    }
}