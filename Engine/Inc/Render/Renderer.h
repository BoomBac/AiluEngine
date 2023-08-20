#pragma once
#ifndef __RENDERER_H__
#define __RENDERER_H__
#include "Framework/Interface/IRuntimeModule.h"
#include "RHI/DX12/BaseRenderer.h"

namespace Ailu
{
    class AILU_API Renderer : public IRuntimeModule
    {
    public:
        int Initialize() override;
        void Finalize() override;
        void Tick() override;
    private:
        void Render();
        DXBaseRenderer* _p_renderer = nullptr;
        bool _b_init = false;
    };
}

#endif // !RENDERER_H__
