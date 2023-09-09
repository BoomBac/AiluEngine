#pragma once
#ifndef __RENDERER_H__
#define __RENDERER_H__
#include "Framework/Interface/IRuntimeModule.h"
#include "RHI/DX12/D3DContext.h"
#include "Framework/Common/TimeMgr.h"

namespace Ailu
{
    enum class ERenderAPI
    {
        kNone,kDirectX12
    };

    class AILU_API Renderer : public IRuntimeModule
    {
    public:
        static ERenderAPI GetAPI();
        int Initialize() override;
        void Finalize() override;
        void Tick() override;
        float GetDeltaTime() const;
    private:
        inline static ERenderAPI sAPI = ERenderAPI::kDirectX12;
        void Render();
        GraphicsContext* _p_context = nullptr;
        bool _b_init = false;
        TimeMgr* _p_timemgr = nullptr;
    };
}

#endif // !RENDERER_H__
