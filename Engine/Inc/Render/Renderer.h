#pragma warning(push)
#pragma warning(disable: 4251)

#pragma once
#ifndef __RENDERER_H__
#define __RENDERER_H__

#include <functional>
#include "GlobalMarco.h"
#include "Framework/Interface/IRuntimeModule.h"
#include "Framework/Common/TimeMgr.h"
#include "RendererAPI.h"
#include "Texture.h"
#include "Framework/Common/SceneMgr.h"
#include "./Pass/RenderPass.h"
#include "./Pass/PostprocessPass.h"
#include "./Pass/SSAOPass.h"
#include "RenderingData.h"


namespace Ailu
{
    DECLARE_ENUM(ERenderPassEvent,kAfterPostprocess,kBeforePostprocess)
    class Camera;
    class AILU_API Renderer
    {
    public:
        using BeforeTickEvent = std::function<void()>;
        using AfterTickEvent = std::function<void()>;
    public:
        Renderer();
        ~Renderer();
        void Render(const Camera& cam,Scene& s);
        void BeginScene(const Camera& cam,const Scene& s);
        void EndScene();
        float GetDeltaTime() const;
        void TakeCapture();
        void EnqueuePass(ERenderPassEvent::ERenderPassEvent event,Scope<RenderPass> pass);
        void SubmitTaskPass(Scope<RenderPass> task);
        List<Scope<RenderPass>>& GetRenderPasses() { return _render_passes; };
        inline static RendererAPI::ERenderAPI GetAPI() { return RendererAPI::GetAPI(); }
        void ResizeBuffer(u32 width, u32 height);
        void RegisterEventBeforeTick(BeforeTickEvent e);
        void RegisterEventAfterTick(AfterTickEvent e);
        void UnRegisterEventBeforeTick(BeforeTickEvent e);
        void UnRegisterEventAfterTick(AfterTickEvent e);
        RenderTexture* GetTargetTexture() const { return g_pRenderTexturePool->Get(_gameview_rt_handle); }
        const RenderingData& GetRenderingData() const { return _rendering_data; }
    private:
        RTHandle _camera_color_handle;
        RTHandle _gameview_rt_handle;
        RTHandle _camera_depth_handle;
        RTHandle _camera_depth_tex_handle;
        void PrepareScene(const Scene& s);
        void PrepareLight(const Scene& s);
        void PrepareCamera(const Camera& cam);
    private:
        static inline List<RenderPass*> _p_task_render_passes{};
        GraphicsContext* _p_context = nullptr;
        Scope<IConstantBuffer> _p_per_frame_cbuf;
        ScenePerFrameData _per_frame_cbuf_data;
        RenderingData _rendering_data;
        IConstantBuffer* _p_per_object_cbufs[RenderConstants::kMaxRenderObjectCount];
        List<Scope<RenderPass>> _render_passes;
        bool _b_init = false;
        TimeMgr* _p_timemgr = nullptr;
        Camera* _active_cam;
        Queue<int> _captures;
        Queue<Vector2f> _resize_events;
        List<BeforeTickEvent> _events_before_tick;
        List<AfterTickEvent> _events_after_tick;
    };

}
#pragma warning(pop)
#endif // !RENDERER_H__
