#include "pch.h"
#include "Render/Renderer.h"

namespace Ailu
{
    int Renderer::Initialize()
    {
        _p_renderer = new DXBaseRenderer(1280, 720);
        return 0;
    }
    void Renderer::Finalize()
    {
        delete _p_renderer;
    }
    void Renderer::Tick()
    {
        Render();
    }
    void Renderer::Render()
    {

    }
}