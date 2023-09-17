#include "pch.h"
#include "Render/Renderer.h"
#include "Framework/Common/Application.h"
#include "Render/RenderCommand.h"

namespace Ailu
{
    void Renderer::BeginScene()
    {
    }
    void Renderer::EndScene()
    {
    }
    void Renderer::Submit(const Ref<VertexBuffer>& vertex_buf, const Ref<IndexBuffer>& index_buffer, uint32_t instance_count)
    {
        vertex_buf->Bind();
        index_buffer->Bind();
        RenderCommand::DrawIndexedInstanced(index_buffer, instance_count);
    }
    void Renderer::Submit(const Ref<VertexBuffer>& vertex_buf, uint32_t instance_count)
    {
        vertex_buf->Bind();
        RenderCommand::DrawInstanced(vertex_buf, instance_count);
    }
    void Renderer::Submit(const Ref<VertexBuffer>& vertex_buf, const Ref<IndexBuffer>& index_buffer, Ref<Material> mat, uint32_t instance_count)
    {
        vertex_buf->Bind();
        index_buffer->Bind();
        mat->Bind();
        RenderCommand::DrawIndexedInstanced(index_buffer, instance_count);
    }
    void Renderer::Submit(const Ref<VertexBuffer>& vertex_buf, Ref<Material> mat, uint32_t instance_count)
    {
        vertex_buf->Bind();
        mat->Bind();
        RenderCommand::DrawInstanced(vertex_buf, instance_count);
    }
    void Renderer::Submit(const Ref<VertexBuffer>& vertex_buf, const Ref<IndexBuffer>& index_buffer, Ref<Material> mat, Matrix4x4f transform, uint32_t instance_count)
    {
        vertex_buf->Bind();
        index_buffer->Bind();
        mat->Bind();
        RenderCommand::DrawIndexedInstanced(index_buffer, instance_count, transform);
    }
    void Renderer::Submit(const Ref<Mesh> mesh, Ref<Material> mat, Matrix4x4f transform, uint32_t instance_count)
    {
        mesh->GetVertexBuffer()->Bind();
        mesh->GetIndexBuffer()->Bind();
        mat->Bind();
        //RenderCommand::DrawInstanced(mesh->GetVertexBuffer(), instance_count, transform);
        RenderCommand::DrawIndexedInstanced(mesh->GetIndexBuffer(), 1, transform);
    }
    int Renderer::Initialize()
    {
        _p_context = new D3DContext(dynamic_cast<WinWindow*>(Application::GetInstance()->GetWindowPtr()));
        _b_init = true;
        _p_context->Init();
        _p_timemgr = new TimeMgr();
        _p_timemgr->Initialize();
        return 0;
    }
    void Renderer::Finalize()
    {
        INIT_CHECK(this, Renderer)
        DESTORY_PTR(_p_context)
        _p_timemgr->Finalize();
        DESTORY_PTR(_p_timemgr)
    }
    void Renderer::Tick()
    {
        INIT_CHECK(this, Renderer)
        ModuleTimeStatics::RenderDeltatime = _p_timemgr->GetElapsedSinceLastMark();
        _p_timemgr->Mark();
        Render();
    }
    float Renderer::GetDeltaTime() const
    {
        return _p_timemgr->GetElapsedSinceLastMark();
    }
    void Renderer::Render()
    {
        _p_context->Present();
    }
}