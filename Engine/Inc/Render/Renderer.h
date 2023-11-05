#pragma warning(push)
#pragma warning(disable: 4251)

#pragma once
#ifndef __RENDERER_H__
#define __RENDERER_H__
#include <list>
#include "Framework/Interface/IRuntimeModule.h"
#include "RHI/DX12/D3DContext.h"
#include "Framework/Common/TimeMgr.h"
#include "RendererAPI.h"
#include "GlobalMarco.h"
#include "Render/Camera.h"
#include "Material.h"
#include "Framework/Assets/Mesh.h"

namespace Ailu
{
    struct DrawInfo
    {
        Ref<Mesh> mesh;
        Ref<Material> mat;
        Matrix4x4f transform;
        uint32_t instance_count;
    };
    class AILU_API Renderer : public IRuntimeModule
    {
    public:
        void BeginScene();
        void EndScene();
        static void Submit(const Ref<VertexBuffer>& vertex_buf, const Ref<IndexBuffer>& index_buffer,uint32_t instance_count = 1);
        static void Submit(const Ref<VertexBuffer>& vertex_buf, uint32_t instance_count = 1);
        static void Submit(const Ref<VertexBuffer>& vertex_buf, const Ref<IndexBuffer>& index_buffer, Ref<Material> mat, uint32_t instance_count = 1);
        static void Submit(const Ref<VertexBuffer>& vertex_buf, Ref<Material> mat ,uint32_t instance_count = 1);
        static void Submit(const Ref<VertexBuffer>& vertex_buf, const Ref<IndexBuffer>& index_buffer, Ref<Material> mat,Matrix4x4f transform ,uint32_t instance_count = 1);
        static void Submit(const Ref<Mesh>& mesh, Ref<Material>& mat,Matrix4x4f transform ,uint32_t instance_count = 1);
        int Initialize() override;
        void Finalize() override;
        void Tick(const float& delta_time) override;
        float GetDeltaTime() const;
        inline static RendererAPI::ERenderAPI GetAPI() { return RendererAPI::GetAPI(); }
    private:
        inline static std::list<DrawInfo> _draw_call{};
        ScenePerFrameData* _p_per_frame_cbuf_data;
        void Render();
        void DrawRendererGizmo();
        GraphicsContext* _p_context = nullptr;
        bool _b_init = false;
        TimeMgr* _p_timemgr = nullptr;
        Camera* _p_scene_camera;
    };
}
#pragma warning(pop)
#endif // !RENDERER_H__
