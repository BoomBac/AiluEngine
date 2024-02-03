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
#include "Material.h"
#include "Mesh.h"
#include "Texture.h"
#include "Framework/Common/SceneMgr.h"
#include "./Pass/RenderPass.h"
#include "RenderingData.h"


namespace Ailu
{
    class AILU_API Renderer : public IRuntimeModule
    {
    public:
        void BeginScene();
        void EndScene();
        int Initialize() override;
        void Finalize() override;
        void Tick(const float& delta_time) override;
        float GetDeltaTime() const;
        inline static RendererAPI::ERenderAPI GetAPI() { return RendererAPI::GetAPI(); }
    private:
        Ref<RenderTexture> _p_camera_color_attachment;
        Ref<RenderTexture> _p_camera_depth_attachment;
        void Render();
        void DrawRendererGizmo();
        void PrepareLight(Scene* p_scene);
        void PrepareCamera(Camera* p_camera);
    private:
        Ref<ComputeShader> _p_test_cs;
        Ref<Texture> _p_test_texture;
        static inline List<RenderPass*> _p_task_render_passes{};
        //ConstantBuffer* 
        RenderingData _rendering_data;
        ScenePerFrameData* _p_per_frame_cbuf_data;
        GraphicsContext* _p_context = nullptr;
        Scope<OpaquePass> _p_opaque_pass;
        Scope<ResolvePass> _p_reslove_pass;
        Scope<ShadowCastPass> _p_shadowcast_pass;
        Scope<CubeMapGenPass> _p_cubemap_gen_pass;
        List<RenderPass*> _p_render_passes;
        bool _b_init = false;
        TimeMgr* _p_timemgr = nullptr;
        Camera* _p_scene_camera;
    };
}
#pragma warning(pop)
#endif // !RENDERER_H__
