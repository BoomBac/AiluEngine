#pragma once
#ifndef __RENDER_PIPELINE__
#define __RENDER_PIPELINE__
#include "Camera.h"
#include "FrameResource.h"
#include "Renderer.h"

namespace Ailu
{
    class AILU_API RenderPipeline
    {
        DISALLOW_COPY_AND_ASSIGN(RenderPipeline)
    public:
        using RenderEvent = std::function<void>();
        RenderPipeline();
        void Init();
        void Destory();
        virtual void Setup();
        void Render();
        Renderer *GetRenderer(u16 index = 0) { return index < _renderers.size() ? _renderers[index].get() : nullptr; };
        RenderTexture *GetTarget(u16 index = 0);
        void FrameCleanUp();
        FrameResource *CurFrameResource() { return _cur_frame_res; }

    private:
        void RenderSingleCamera(const Camera &cam, Renderer &renderer);

    protected:
        virtual void BeforeReslove() {};

    protected:
        Array<FrameResource, RenderConstants::kFrameCount + 1> _frame_res;
        Vector<Camera *> _cameras;
        Vector<Scope<Renderer>> _renderers;
        Vector<RenderTexture *> _targets;
        FrameResource *_cur_frame_res;
    };
}// namespace Ailu

#endif// !RENDER_PIPELINE__
