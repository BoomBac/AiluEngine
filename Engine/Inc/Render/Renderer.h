#pragma once
#ifndef __RENDERER_H__
#define __RENDERER_H__
#include "Framework/Interface/IRuntimeModule.h"
#include "RHI/DX12/D3DContext.h"
#include "Framework/Common/TimeMgr.h"
#include "RendererAPI.h"
#include "GlobalMarco.h"
#include "Material.h"

namespace Ailu
{
    class AILU_API Renderer : public IRuntimeModule
    {
    public:
        static void BeginScene();
        static void EndScene();
        static void Submit(const Ref<VertexBuffer>& vertex_buf, const Ref<IndexBuffer>& index_buffer,uint32_t instance_count = 1);
        static void Submit(const Ref<VertexBuffer>& vertex_buf, uint32_t instance_count = 1);
        static void Submit(const Ref<VertexBuffer>& vertex_buf, const Ref<IndexBuffer>& index_buffer, Ref<Material> mat, uint32_t instance_count = 1);
        static void Submit(const Ref<VertexBuffer>& vertex_buf, Ref<Material> mat ,uint32_t instance_count = 1);
        static void Submit(const Ref<VertexBuffer>& vertex_buf, const Ref<IndexBuffer>& index_buffer, Ref<Material> mat,Matrix4x4f transform ,uint32_t instance_count = 1);
        int Initialize() override;
        void Finalize() override;
        void Tick() override;
        float GetDeltaTime() const;
        inline static RendererAPI::ERenderAPI GetAPI() { return RendererAPI::GetAPI(); }
    private:

        void Render();
        GraphicsContext* _p_context = nullptr;
        bool _b_init = false;
        TimeMgr* _p_timemgr = nullptr;
    };
}

#endif // !RENDERER_H__
