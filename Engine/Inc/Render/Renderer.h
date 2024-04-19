#pragma warning(push)
#pragma warning(disable: 4251)

#pragma once
#ifndef __RENDERER_H__
#define __RENDERER_H__

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
        inline static u32 kRendererWidth = 1600u;
        inline static u32 kRendererHeight = 900u;
    public:
        void BeginScene();
        void EndScene();
        int Initialize() override;
        void Finalize() override;
        void Tick(const float& delta_time) override;
        float GetDeltaTime() const;
        void TakeCapture();
        List<RenderPass*>& GetRenderPasses() { return _render_passes; };
        inline static RendererAPI::ERenderAPI GetAPI() { return RendererAPI::GetAPI(); }
    private:
        Ref<RenderTexture> _p_camera_color_attachment;
        Ref<RenderTexture> _p_camera_depth_attachment;
        void Render();
        void DrawRendererGizmo();
        void PrepareLight(Scene* p_scene);
        void PrepareCamera(Camera* p_camera);
    private:
        static inline List<RenderPass*> _p_task_render_passes{};
        Ref<ComputeShader> _p_test_cs;
        Ref<Texture> _p_test_texture;
        Scope<ConstantBuffer> _p_per_frame_cbuf;
        ScenePerFrameData _per_frame_cbuf_data;
        RenderingData _rendering_data;
        ConstantBuffer* _p_per_object_cbufs[RenderConstants::kMaxRenderObjectCount];
        GraphicsContext* _p_context = nullptr;
        Scope<OpaquePass> _p_opaque_pass;
        Scope<ResolvePass> _p_reslove_pass;
        Scope<ShadowCastPass> _p_shadowcast_pass;
        Scope<CubeMapGenPass> _p_cubemap_gen_pass;
        Scope<PostProcessPass> _p_postprocess_pass;
        Scope<DeferredGeometryPass> _p_gbuffer_pass;
        Scope<SkyboxPass> _p_skybox_pass;
        List<RenderPass*> _render_passes;
        bool _b_init = false;
        TimeMgr* _p_timemgr = nullptr;
        Camera* _p_scene_camera;
        Queue<int> _captures;
    };
    extern Renderer* g_pRenderer;
}
#pragma warning(pop)
#endif // !RENDERER_H__
