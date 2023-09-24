#include "pch.h"
#include "Render/Renderer.h"
#include "Framework/Common/Application.h"
#include "Render/RenderCommand.h"
#include "Render/RenderingData.h"

namespace Ailu
{
    void Renderer::BeginScene()
    {
    }
    void Renderer::EndScene()
    {
        if (Gizmo::s_color.a > 0.0f)
        {
            int gridSize = 100;
            int gridSpacing = 100;
            Vector3f cameraPosition = Camera::sCurrent->GetPosition();
            float grid_alpha = lerpf(0.0f, 1.0f, abs(cameraPosition.y) / 2000.0f);
            Color grid_color = Colors::kWhite;
            grid_color.a = 1.0f - grid_alpha;
            if (grid_color.a > 0)
            {
                Vector3f grid_center_mid(static_cast<float>(static_cast<int>(cameraPosition.x / gridSpacing) * gridSpacing),
                    0.0f,
                    static_cast<float>(static_cast<int>(cameraPosition.z / gridSpacing) * gridSpacing));
                Gizmo::DrawGrid(100, 100, grid_center_mid, grid_color);
            }
            grid_color.a = grid_alpha;
            if (grid_color.a > 0.7f)
            {
                gridSize = 10;
                gridSpacing = 1000;
                Vector3f grid_center_large(static_cast<float>(static_cast<int>(cameraPosition.x / gridSpacing) * gridSpacing),
                    0.0f,
                    static_cast<float>(static_cast<int>(cameraPosition.z / gridSpacing) * gridSpacing));
                grid_color = Colors::kGreen;
                Gizmo::DrawGrid(10, 1000, grid_center_large, grid_color);
            }

            Gizmo::DrawLine(Vector3f::Zero, Vector3f{ 500.f,0.0f,0.0f }, Colors::kRed);
            Gizmo::DrawLine(Vector3f::Zero, Vector3f{ 0.f,500.0f,0.0f }, Colors::kGreen);
            Gizmo::DrawLine(Vector3f::Zero, Vector3f{ 0.f,0.0f,500.0f }, Colors::kBlue);
        }
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
        INIT_CHECK(this, Renderer);
        RenderingStates::Reset();
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