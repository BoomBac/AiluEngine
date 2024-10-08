#include "Render/RenderPipeline.h"
#include "Framework/Common/Profiler.h"
#include "Render/CommandBuffer.h"
#include "pch.h"

#ifdef _PIX_DEBUG
#include "Ext/pix/Include/WinPixEventRuntime/pix3.h"
#endif// _PIX_DEBUG

namespace Ailu
{
    RenderPipeline::RenderPipeline()
    {
        Init();
    }
    void RenderPipeline::Init()
    {
        TIMER_BLOCK("RenderPipeline::Init()");
        _renderers.push_back(MakeScope<Renderer>());
        _cameras.emplace_back(Camera::sCurrent);
    }
    void RenderPipeline::Destory()
    {
    }
    void RenderPipeline::Setup()
    {
        _targets.clear();
        _cameras.clear();
        _cameras.emplace_back(Camera::sCurrent);
        for (auto &cam: g_pSceneMgr->ActiveScene()->GetRegister().View<CCamera>())
        {
            _cameras.push_back(&cam._camera);
        }
    }
    void RenderPipeline::Render()
    {
        Setup();
        for (auto cam: _cameras)
        {
            cam->SetRenderer(_renderers[0].get());
#ifdef _PIX_DEBUG
            PIXBeginEvent(cam->HashCode(),L"DeferedRenderer");
            RenderSingleCamera(*cam, *_renderers[0].get());
            PIXEndEvent();
#else
            RenderSingleCamera(*cam, *_renderers[0].get());
#endif
            _targets.push_back(_renderers[0]->TargetTexture());
        }
        RenderTexture::ResetRenderTarget();
    }
    RenderTexture *RenderPipeline::GetTarget(u16 index)
    {
        if (index < _targets.size())
            return _targets[index];
        return nullptr;
    }
    void RenderPipeline::RenderSingleCamera(const Camera &cam, Renderer &renderer)
    {
        renderer.Render(cam, *g_pSceneMgr->ActiveScene());
    }
    void RenderPipeline::FrameCleanUp()
    {
        g_pRenderTexturePool->RelesaeUnusedRT();
        for (auto &r: _renderers)
            r->FrameCleanup();
    }
}// namespace Ailu
