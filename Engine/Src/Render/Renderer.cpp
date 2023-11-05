#include "pch.h"
#include "Render/Renderer.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/SceneMgr.h"
#include "Render/RenderCommand.h"
#include "Render/RenderingData.h"
#include "Render/Gizmo.h"
#include "Objects/StaticMeshComponent.h"
#include "Framework/Parser/AssetParser.h"
#include <Framework/Common/LogMgr.h>

namespace Ailu
{
    int Renderer::Initialize()
    {
        _p_context = new D3DContext(dynamic_cast<WinWindow*>(Application::GetInstance()->GetWindowPtr()));
        _b_init = true;
        _p_context->Init();
        _p_timemgr = new TimeMgr();
        _p_timemgr->Initialize();
        _p_per_frame_cbuf_data = static_cast<D3DContext*>(_p_context)->GetPerFrameCbufDataStruct();
        //{ 
        //    _p_scene_camera = MakeScope<Camera>(16.0F / 9.0F);
        //    _p_scene_camera->SetPosition(1356.43f, 604.0f, -613.45f);
        //    _p_scene_camera->Rotate(11.80f, -59.76f);
        //    _p_scene_camera->SetLens(1.57f, 16.f / 9.f, 1.f, 5000.f);
        //    Camera::sCurrent = _p_scene_camera.get();
        //}
        return 0;
    }
    void Renderer::Finalize()
    {
        INIT_CHECK(this, Renderer);
        DESTORY_PTR(_p_context);
        _p_timemgr->Finalize();
        DESTORY_PTR(_p_timemgr);
    }
    void Renderer::Tick(const float& delta_time)
    {
        INIT_CHECK(this, Renderer);
        RenderingStates::Reset();
        ModuleTimeStatics::RenderDeltatime = _p_timemgr->GetElapsedSinceLastMark();
        _p_timemgr->Mark();
        BeginScene();
        Render();
        EndScene();
    }

    void Renderer::BeginScene()
    {
        memset(reinterpret_cast<void*>(_p_per_frame_cbuf_data), 0, sizeof(ScenePerFrameData));
        _p_scene_camera = g_pSceneMgr->_p_current->GetActiveCamera();
        Camera::sCurrent = _p_scene_camera;
        auto& light_comps = g_pSceneMgr->_p_current->GetAllLight();
        uint16_t updated_light_num = 0u;
        uint16_t direction_light_index = 0, point_light_index = 0, spot_light_index = 0;
        for (auto light : light_comps)
        {
            auto& light_data = light->_light;
            Color color = light_data._light_color;
            color.r *= color.a;
            color.g *= color.a;
            color.b *= color.a;
            if (light->_light_type == ELightType::kDirectional)
            {
                if (!light->Active())
                {
                    _p_per_frame_cbuf_data->_DirectionalLights[direction_light_index]._LightDir = Vector3f::Zero;
                    _p_per_frame_cbuf_data->_DirectionalLights[direction_light_index++]._LightColor = Colors::kBlack.xyz;
                    continue;
                }
                _p_per_frame_cbuf_data->_DirectionalLights[direction_light_index]._LightColor = color.xyz;
                _p_per_frame_cbuf_data->_DirectionalLights[direction_light_index++]._LightDir = light_data._light_dir.xyz;
            }
            else if (light->_light_type == ELightType::kPoint)
            {
                if (!light->Active())
                {
                    _p_per_frame_cbuf_data->_PointLights[point_light_index]._LightParam0 = 0.0;
                    _p_per_frame_cbuf_data->_PointLights[point_light_index++]._LightColor = Colors::kBlack.xyz;
                    continue;
                }
                _p_per_frame_cbuf_data->_PointLights[point_light_index]._LightColor = color.xyz;
                _p_per_frame_cbuf_data->_PointLights[point_light_index]._LightPos = light_data._light_pos.xyz;
                _p_per_frame_cbuf_data->_PointLights[point_light_index]._LightParam0 = light_data._light_param.x;
                _p_per_frame_cbuf_data->_PointLights[point_light_index++]._LightParam1 = light_data._light_param.y;
            }
            else if (light->_light_type == ELightType::kSpot)
            {
                if (!light->Active())
                {
                    _p_per_frame_cbuf_data->_SpotLights[spot_light_index++]._LightColor = Colors::kBlack.xyz;
                    continue;
                }
                _p_per_frame_cbuf_data->_SpotLights[spot_light_index]._LightColor = color.xyz;
                _p_per_frame_cbuf_data->_SpotLights[spot_light_index]._LightPos = light_data._light_pos.xyz;
                _p_per_frame_cbuf_data->_SpotLights[spot_light_index]._LightDir = light_data._light_dir.xyz;
                _p_per_frame_cbuf_data->_SpotLights[spot_light_index]._Rdius = light_data._light_param.x;
                _p_per_frame_cbuf_data->_SpotLights[spot_light_index]._InnerAngle = light_data._light_param.y;
                _p_per_frame_cbuf_data->_SpotLights[spot_light_index]._OuterAngle = light_data._light_param.z;
            }
            ++updated_light_num;
        }
        if (updated_light_num == 0)
        {
            _p_per_frame_cbuf_data->_DirectionalLights[0]._LightColor = Colors::kBlack.xyz;
            _p_per_frame_cbuf_data->_DirectionalLights[0]._LightDir = { 0.0f,0.0f,0.0f };
        }
        _p_per_frame_cbuf_data->_CameraPos = _p_scene_camera->GetPosition();
        memcpy(D3DContext::GetInstance()->GetPerFrameCbufData(), _p_per_frame_cbuf_data, sizeof(ScenePerFrameData));

        auto cmd = D3DCommandBufferPool::GetCommandBuffer();
        static uint32_t w = 1600, h = 900;
        cmd->Clear();
        cmd->SetViewports({ Viewport{0,0,(uint16_t)w,(uint16_t)h} });
        cmd->SetScissorRects({ Viewport{0,0,(uint16_t)w,(uint16_t)h} });
        cmd->ClearRenderTarget({ 0.3f, 0.2f, 0.4f, 1.0f }, 1.0, true, true);
        cmd->SetViewProjectionMatrices(Transpose(_p_scene_camera->GetView()), Transpose(_p_scene_camera->GetProjection()));
        if (RenderingStates::s_shadering_mode == EShaderingMode::kShader || RenderingStates::s_shadering_mode == EShaderingMode::kShaderedWireFrame)
        {
            cmd->SetPSO(GraphicsPipelineStateMgr::s_standard_shadering_pso);
            for (auto& obj : _draw_call)
            {
                cmd->DrawRenderer(obj.mesh, obj.transform, obj.mat, obj.instance_count);
            }
        }
        if (RenderingStates::s_shadering_mode == EShaderingMode::kWireFrame || RenderingStates::s_shadering_mode == EShaderingMode::kShaderedWireFrame)
        {
            static auto wireframe_mat = MaterialPool::GetMaterial("Materials/WireFrame_new.alasset");
            cmd->SetPSO(GraphicsPipelineStateMgr::s_wireframe_pso);
            for (auto& obj : _draw_call)
            {
                cmd->DrawRenderer(obj.mesh, obj.transform, wireframe_mat, obj.instance_count);
            }
        }
        D3DContext::GetInstance()->ExecuteCommandBuffer(cmd);
        D3DCommandBufferPool::ReleaseCommandBuffer(cmd);
        DrawRendererGizmo();
    }
    void Renderer::EndScene()
    {
        _draw_call.clear();
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
    void Renderer::Submit(const Ref<Mesh>& mesh, Ref<Material>& mat, Matrix4x4f transform, uint32_t instance_count)
    {
        _draw_call.emplace_back(DrawInfo{ mesh,mat,transform,instance_count });
    }

    float Renderer::GetDeltaTime() const
    {
        return _p_timemgr->GetElapsedSinceLastMark();
    }
    void Renderer::Render()
    {
        _p_context->Present();
    }
    void Renderer::DrawRendererGizmo()
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
}