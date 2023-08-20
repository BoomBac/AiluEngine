#include "pch.h"
#include "Render/Renderer.h"
#include "Framework/Common/GfxConfiguration.h"

namespace Ailu
{
    int Renderer::Initialize()
    {
        auto config = GfxConfiguration();
        _p_renderer = new DXBaseRenderer(config.viewport_width_, config.viewport_height_);
        _b_init = true;
        _p_renderer->OnInit();
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