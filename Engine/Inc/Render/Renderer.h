#pragma warning(push)
#pragma warning(disable : 4251)

#pragma once
#ifndef __RENDERER_H__
#define __RENDERER_H__

#include "./Pass/RenderPass.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Interface/IRuntimeModule.h"
#include "GlobalMarco.h"
#include "RendererAPI.h"
#include "Texture.h"
#include <functional>
// #include "./Pass/PostprocessPass.h"
// #include "./Pass/SSAOPass.h"
#include "RenderingData.h"
#include "Scene/Scene.h"

namespace Ailu
{
    DECLARE_ENUM(EShadingMode, kLit = 1, kWireframe = 2, kLitWireframe = 3);
    class PostProcessPass;
    class SSAOPass;
    class Camera;
    class FrameResource;
    class RenderPipeline;
    class AILU_API Renderer
    {
    public:
        using BeforeTickEvent = std::function<void()>;
        using AfterTickEvent = std::function<void()>;
        DISALLOW_COPY_AND_ASSIGN(Renderer)
        public:
        inline static RendererAPI::ERenderAPI GetAPI() { return RendererAPI::GetAPI(); }
        Renderer();
        ~Renderer();
        void Render(const Camera &cam, const Scene &s);
        void BeginScene(const Camera &cam, const Scene &s);
        void EndScene(const Scene &s);
        void FrameCleanup();
        float GetDeltaTime() const;
        void EnqueuePass(RenderPass *pass);
        void SubmitTaskPass(RenderPass *task);
        Vector<RenderPass *> &GetRenderPasses() { return _render_passes; };
        Vector<RenderFeature *> GetFeatures();
        void ResizeBuffer(u32 width, u32 height);
        void RegisterEventBeforeTick(BeforeTickEvent e);
        void RegisterEventAfterTick(AfterTickEvent e);
        void UnRegisterEventBeforeTick(BeforeTickEvent e);
        void UnRegisterEventAfterTick(AfterTickEvent e);
        RenderTexture *GetTargetTexture() const { return g_pRenderTexturePool->Get(_gameview_rt_handle); }
        const RenderingData &GetRenderingData() const { return _rendering_data; }
        RenderTexture *TargetTexture();
        void SetViewProjectionMatrix(const Matrix4x4f &view, const Matrix4x4f &proj);
        void AddFeature(RenderFeature *feature) { _features.emplace_back(feature); };
        void SetShadingMode(EShadingMode::EShadingMode mode) { _mode = mode; }
        void SetupFrameResource(FrameResource* prev_fr,FrameResource *cur_fr) { _prev_fs = prev_fr;_cur_fs = cur_fr; }
    public:
        bool _is_render_light_probe = false;
    private:
        void PrepareScene(const Scene &s);
        void PrepareLight(const Scene &s);
        void PrepareCamera(const Camera &cam);
        void DoRender(const Camera &cam, const Scene &s);
        void Cull(const Scene &s, const Camera &cam);
        static void StableSort(Vector<RenderPass *> list);
    
    private:
        static inline List<RenderPass *> _p_task_render_passes{};
        GraphicsContext *_p_context = nullptr;
        RenderPipeline *_p_cur_pipeline;
        FrameResource *_cur_fs;
        FrameResource *_prev_fs;
        EShadingMode::EShadingMode _mode = EShadingMode::kLit;
        RTHandle _camera_color_handle;
        RTHandle _gameview_rt_handle;
        RTHandle _camera_depth_handle;
        RTHandle _camera_depth_tex_handle;
        Map<u64, CullResult> _cull_results;
        //CBufferPerSceneData _per_scene_cbuf_data;
        //CBufferPerCameraData _per_cam_cbuf_data;


        Scope<ShadowCastPass> _shadowcast_pass;
        Scope<DeferredGeometryPass> _gbuffer_pass;
        Scope<CopyDepthPass> _coptdepth_pass;
        Scope<DeferredLightingPass> _lighting_pass;
        Scope<SkyboxPass> _skybox_pass;
        Scope<MotionVectorPass> _motion_vector_pass;
        Scope<ForwardPass> _forward_pass;
        Scope<CopyColorPass> _copycolor_pass;
        Scope<PostProcessPass> _postprocess_pass;
        Scope<GizmoPass> _gizmo_pass;
        Scope<GUIPass> _gui_pass;
        Scope<WireFramePass> _wireframe_pass;
        Vector<RenderPass *> _render_passes;
        RenderFeature *_vxgi;
        RenderFeature *_cloud;
        RenderFeature *_taa;
        RenderFeature *_ssao;
        u64 _active_camera_hash;

        RenderingData _rendering_data;

        Vector<Scope<RenderFeature>> _owned_features;
        //存储一份当前renderer使用的所有feature，不对元素的生命周期负责
        Vector<RenderFeature *> _features;
        bool _b_init = false;
        TimeMgr *_p_timemgr = nullptr;
        Camera *_active_cam;
        Queue<Vector2f> _resize_events;
        List<BeforeTickEvent> _events_before_tick;
        List<AfterTickEvent> _events_after_tick;
        RenderTexture *_target_tex;
    };

}// namespace Ailu
#pragma warning(pop)
#endif// !RENDERER_H__
