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
#include "Camera.h"
#include "Texture.h"
#include "Framework/Common/SceneMgr.h"
#include "./Pass/RenderPass.h"
#include "./Pass/PostprocessPass.h"
#include "RenderingData.h"


namespace Ailu
{
    class AILU_API Renderer : public IRuntimeModule
    {
    public:
        using BeforeTickEvent = std::function<void()>;
        using AfterTickEvent = std::function<void()>;
    public:
        void BeginScene();
        void EndScene();
        int Initialize() override;
        int Initialize(u32 width,u32 height);
        void Finalize() override;
        void Tick(const float& delta_time) override;
        float GetDeltaTime() const;
        void TakeCapture();
        void SubmitTaskPass(Scope<RenderPass> task);
        List<RenderPass*>& GetRenderPasses() { return _render_passes; };
        inline static RendererAPI::ERenderAPI GetAPI() { return RendererAPI::GetAPI(); }
        void ResizeBuffer(u32 width, u32 height);
        void RegisterEventBeforeTick(BeforeTickEvent e);
        void RegisterEventAfterTick(AfterTickEvent e);
        void UnRegisterEventBeforeTick(BeforeTickEvent e);
        void UnRegisterEventAfterTick(AfterTickEvent e);
        RenderTexture* GetTargetTexture() const { return g_pRenderTexturePool->Get(_gameview_rt_handle); }
        bool _is_offscreen = true;
        float _shadow_distance = 1000;
    private:
        RTHandle _camera_color_handle;
        RTHandle _gameview_rt_handle;
        RTHandle _camera_depth_handle;

        void Render();
        void PrepareLight(Scene* p_scene);
        void PrepareCamera(Camera* p_camera);
        void DoResize();
    private:
        static inline List<RenderPass*> _p_task_render_passes{};
        GraphicsContext* _p_context = nullptr;
        u32 _width, _height;
        Scope<ConstantBuffer> _p_per_frame_cbuf;
        ScenePerFrameData _per_frame_cbuf_data;
        RenderingData _rendering_data;
        ConstantBuffer* _p_per_object_cbufs[RenderConstants::kMaxRenderObjectCount];
        Scope<OpaquePass> _p_opaque_pass;
        Scope<ResolvePass> _p_reslove_pass;
        Scope<ShadowCastPass> _p_shadowcast_pass;
        Scope<CubeMapGenPass> _p_cubemap_gen_pass;
        Scope<PostProcessPass> _p_postprocess_pass;
        Scope<DeferredGeometryPass> _p_gbuffer_pass;
        Scope<SkyboxPass> _p_skybox_pass;
        Scope<CopyColorPass> _p_copycolor_pass;
        Scope<GizmoPass> _p_gizmo_pass;
        List<RenderPass*> _render_passes;
        bool _b_init = false;
        TimeMgr* _p_timemgr = nullptr;
        Camera* _p_scene_camera;
        Queue<int> _captures;
        Queue<Vector2f> _resize_events;
        List<BeforeTickEvent> _events_before_tick;
        List<AfterTickEvent> _events_after_tick;

    };
    extern AILU_API Renderer* g_pRenderer;
}
#pragma warning(pop)
#endif // !RENDERER_H__
