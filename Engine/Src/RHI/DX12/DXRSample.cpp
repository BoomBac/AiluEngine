#include "RHI/DX12/DXRSample.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/TimeMgr.h"
#include "RHI/DX12/D3DBuffer.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DShaderCompiler.h"
#include "RHI/DX12/dxhelper.h"
#include "RHI/DX12/dxrhelper.h"
#include "Render/Camera.h"
#include "Render/GraphicsContext.h"
#include "Render/Shader.h"
#include "Render/ShaderInterop.h"
#include "Render/Texture.h"
#include "pch.h"

using namespace Ailu::Render;
namespace Ailu::RHI::DX12
{
    Ref<ConstantBuffer> g_rayGenData;
    Ref<ConstantBuffer> g_perSceneData;
    Ref<ConstantBuffer> g_perCamData;
    CBufferPerSceneData g_sceneData;
    CBufferPerCameraData g_camData;

    struct InstanceGeometryDesc
    {
        u32 _triangle_offset;
        u32 _vertex_offset;
    };

    DXRSample::DXRSample(ID3D12Device5 *device, ID3D12CommandQueue *cmd_queue) : m_dxrDevice(device), m_commandQueue(cmd_queue)
    {
    }

    DXRSample::~DXRSample()
    {
    }

    void DXRSample::SetInstanceTransform(u32 instance_index, const Matrix4x4f &transform)
    {
        if (m_scene && instance_index < m_scene->InstanceCount())
        {
            m_scene->UpdateInstance(instance_index, transform);
            return;
        }

        if (instance_index < m_instanceDescs.size())
        {
            FillInstanceTransform(transform, m_instanceDescs[instance_index]);
            m_tlasDirty = true;
        }
    }

    void DXRSample::Init(u16 w, u16 h)
    {
        g_rayGenData.reset(ConstantBuffer::Create(sizeof(RayGenConstantBuffer), "RayGenData"));
        g_perSceneData.reset(ConstantBuffer::Create(sizeof(CBufferPerSceneData), "PerSceneData"));
        g_perCamData.reset(ConstantBuffer::Create(sizeof(CBufferPerCameraData), "PerCamData"));

        m_rayGenCB.viewport = {-1.0f, -1.0f, 1.0f, 1.0f};
        f32 border = 0.1f;
        f32 m_aspectRatio = static_cast<f32>(w) / static_cast<f32>(h);
        m_rayGenCB.stencil =
                {
                        -1 + border, -1 + border * m_aspectRatio,
                        1.0f - border, 1 - border * m_aspectRatio};
        u32 indices[] =
                {
                        0, 1, 2};

        float depthValue = 1.0;
        float offset = 0.7f;
        Vector3f vertices[] =
                {
                        // The sample raytraces in screen space coordinates.
                        // Since DirectX screen space coordinates are right handed (i.e. Y axis points down).
                        // Define the vertices in counter clockwise order ~ clockwise in left handed.
                        {0, -offset, depthValue},
                        {-offset, offset, depthValue},
                        {offset, offset, depthValue}};

        VertexBufferLayoutDesc layout_desc = {"POSITION", EShaderDateType::kFloat3, 0};
        VertexBufferLayout layout({layout_desc});
        m_vertexBuffer.reset(Render::VertexBuffer::Create(layout, "DXR_Sample_Vertex_Buffer"));
        m_vertexBuffer->SetStream(reinterpret_cast<u8 *>(vertices), sizeof(vertices), 0, false);
        m_indexBuffer.reset(Render::IndexBuffer::Create(indices, 3, "DXR_Sample_Index_Buffer"));
        m_vertexBuffer->ApplySync();
        m_indexBuffer->ApplySync();
        m_raytracing_shader = RayTracingShader::Create(ResourceMgr::GetResSysPath(L"Shaders/hlsl/DXR/Raytracing.hlsl"), "DXR_Sample_RayTracingShader");
        if (m_raytracing_shader == nullptr)
        {
            LOG_ERROR("DXRSample: failed to create raytracing shader.");
            return;
        }
        BuildRaytracingAccelerationStructures();
    }

    void DXRSample::MakesureOutput(u16 w, u16 h)
    {
        if (m_uav_output == nullptr || m_uav_output->Width() != w || m_uav_output->Height() != h)
        {
            if (m_uav_output)
                m_uav_output.reset();
            TextureDesc tex_desc;
            tex_desc._width = w;
            tex_desc._height = h;
            tex_desc._format = EALGFormat::kALGFormatR16G16B16A16_FLOAT;
            tex_desc._is_random_access = true;
            tex_desc._mip_num = 0;
            m_uav_output = Texture2D::Create(tex_desc);
            m_uav_output->ApplySync();
        }
    }

    static u64 s_frame = 0u;
    void DXRSample::Render(RHICommandBuffer *cmd, u16 w, u16 h)
    {
        if (s_frame < 5u)
        {
            s_frame++;
            return;
        }
        if (s_frame == 5u)
        {
            LOG_INFO("Start init DXR Sample");
            Init(w, h);
        }
        s_frame++;
        if (!m_scene->IsReady())
            return;
        MakesureOutput(w, h);
        auto cam = *Camera::sCurrent;
        f32 f = cam.Far(), n = cam.Near();
        u32 pixel_width = cam.OutputSize().x, pixel_height = cam.OutputSize().y;
        g_camData._ScreenParams = Vector4f(1.0f / (f32) pixel_width, 1.0f / (f32) pixel_height, (f32) pixel_width, (f32) pixel_height);
        Camera::CalculateZBUfferAndProjParams(cam, g_camData._ZBufferParams, g_camData._ProjectionParams);
        g_camData._CameraPos = cam.Position();
        g_camData._MatrixV = cam.GetView();
        g_camData._MatrixP = cam.GetProj();
        g_camData._MatrixVP = cam.GetViewProj();
        g_camData._MatrixVP_NoJitter = cam.GetViewProj();
        g_camData._MatrixIVP = MatrixInverse(g_camData._MatrixVP);

        const f32 animated_offset = 0.5f + std::sin(TimeMgr::TickTimeSinceLoad) * 0.25f;
        SetInstanceTransform(1u, MatrixRotationY(animated_offset) * MatrixTranslation(animated_offset, 0.0f, 0.0f));
        m_scene->Update();

        g_rayGenData->SetData(reinterpret_cast<u8 *>(&m_rayGenCB), sizeof(RayGenConstantBuffer));
        g_perSceneData->SetData(reinterpret_cast<u8 *>(&g_sceneData), sizeof(CBufferPerSceneData));
        g_perCamData->SetData(reinterpret_cast<u8 *>(&g_camData), sizeof(CBufferPerCameraData));
        m_raytracing_shader->SetScene(m_scene.get());
        m_raytracing_shader->SetBuffer("VertexBuffer", _vertex_data.get());
        m_raytracing_shader->SetBuffer("NormalBuffer", _normal_data.get());
        m_raytracing_shader->SetBuffer("IndexBuffer", _indices_data.get());
        m_raytracing_shader->SetBuffer("InstanceGeometryBuffer", _instance_geometry_data.get());
        m_raytracing_shader->SetBuffer("g_rayGenCB", g_rayGenData.get());
        m_raytracing_shader->SetBuffer("g_perSceneData", g_perSceneData.get());
        m_raytracing_shader->SetBuffer("g_perCamData", g_perCamData.get());
        m_raytracing_shader->SetTexture("RenderTarget", m_uav_output.get());
        m_raytracing_shader->PushState(m_scene.get());
        m_raytracing_shader->DispatchRays(cmd, w, h, 1u);
    }

    void DXRSample::BuildRaytracingAccelerationStructures()
    {
        auto mesh = Mesh::s_monkey.lock();
        auto plane = Mesh::s_plane.lock();
        if (!mesh || !plane)
            return;

        auto *vb = dynamic_cast<D3DVertexBuffer *>(mesh->GetVertexBuffer().get());
        auto *ib = dynamic_cast<D3DIndexBuffer *>(mesh->GetIndexBuffer().get());
        auto *plane_vb = dynamic_cast<D3DVertexBuffer *>(plane->GetVertexBuffer().get());
        auto *plane_ib = dynamic_cast<D3DIndexBuffer *>(plane->GetIndexBuffer().get());
        if (vb == nullptr || ib == nullptr || plane_vb == nullptr || plane_ib == nullptr)
            return;

        m_instanceCount = 3u;

        Vector<Vector3f> merged_vertices;
        Vector<Vector3f> merged_normals;
        Vector<Vector3UInt> merged_indices;
        Vector<InstanceGeometryDesc> instance_geometry_descs(m_instanceCount);

        auto append_mesh_data = [&](Render::Mesh *src_mesh, const Vector<u32> &instance_ids)
        {
            const auto vertices = src_mesh->GetVertices();
            const auto normals = src_mesh->GetNormals();
            const auto indices = src_mesh->GetIndices();
            if (vertices.empty() || indices.empty())
                return false;

            const u32 vertex_offset = static_cast<u32>(merged_vertices.size());
            const u32 triangle_offset = static_cast<u32>(merged_indices.size());
            const u32 vertex_count = static_cast<u32>(vertices.size());
            const u32 triangle_count = static_cast<u32>(indices.size() / 3u);

            merged_vertices.insert(merged_vertices.end(), vertices.begin(), vertices.end());
            if (normals.size() == vertices.size())
            {
                merged_normals.insert(merged_normals.end(), normals.begin(), normals.end());
            }
            else
            {
                merged_normals.insert(merged_normals.end(), vertex_count, Vector3f(0.0f, 1.0f, 0.0f));
            }

            for (u32 triangle_index = 0; triangle_index < triangle_count; ++triangle_index)
            {
                const u32 base_index = triangle_index * 3u;
                merged_indices.emplace_back(indices[base_index], indices[base_index + 1u], indices[base_index + 2u]);
            }

            for (u32 instance_id: instance_ids)
            {
                instance_geometry_descs[instance_id]._triangle_offset = triangle_offset;
                instance_geometry_descs[instance_id]._vertex_offset = vertex_offset;
            }

            return true;
        };

        if (!append_mesh_data(mesh.get(), {0u, 1u}) || !append_mesh_data(plane.get(), {2u}))
        {
            LOG_ERROR("DXRSample: failed to collect mesh data for merged raytracing buffers.");
            return;
        }

        BufferDesc mesh_data_desc = {};
        mesh_data_desc._target = EGPUBufferTarget::kStructured;
        mesh_data_desc._element_size = sizeof(Vector3f);
        mesh_data_desc._element_num = static_cast<u32>(merged_vertices.size());
        mesh_data_desc._is_random_write = true;
        _vertex_data = GPUBuffer::Create(mesh_data_desc, "DXR_Sample_Vertex_Data_Buffer");
        _vertex_data->SetData(reinterpret_cast<const u8 *>(merged_vertices.data()), mesh_data_desc._element_num * mesh_data_desc._element_size);
        _normal_data = GPUBuffer::Create(mesh_data_desc, "DXR_Sample_Normal_Data_Buffer");
        _normal_data->SetData(reinterpret_cast<const u8 *>(merged_normals.data()), mesh_data_desc._element_num * mesh_data_desc._element_size);
        mesh_data_desc._element_size = sizeof(Vector3UInt);
        mesh_data_desc._element_num = static_cast<u32>(merged_indices.size());
        _indices_data = GPUBuffer::Create(mesh_data_desc, "DXR_Sample_Index_Data_Buffer");
        _indices_data->SetData(reinterpret_cast<const u8 *>(merged_indices.data()), mesh_data_desc._element_num * mesh_data_desc._element_size);
        mesh_data_desc._element_size = sizeof(InstanceGeometryDesc);
        mesh_data_desc._element_num = m_instanceCount;
        _instance_geometry_data = GPUBuffer::Create(mesh_data_desc, "DXR_Sample_Instance_Geometry_Buffer");
        _instance_geometry_data->SetData(reinterpret_cast<const u8 *>(instance_geometry_descs.data()), mesh_data_desc._element_num * mesh_data_desc._element_size);
        _vertex_data->ApplySync();
        _normal_data->ApplySync();
        _indices_data->ApplySync();
        _instance_geometry_data->ApplySync();

        m_cube = RayTracingGeometry::Create(RayTracingGeometryDesc(mesh.get(), true), "blas_cube");
        m_plane = RayTracingGeometry::Create(RayTracingGeometryDesc(plane.get(), true), "blas_plane");

        m_scene = RayTracingScene::Create();
        for(u16 i = 0; i < m_instanceCount - 1; ++i)
        {
            RayTracingInstance instance = {};
            instance._geometry = m_cube.get();
            instance._instance_id = i;
            instance._hit_group_offset = i;
            instance._transform= MatrixTranslation((i == 0) ? -0.5f : 0.5f, 0.0f, 0.0f);
            m_scene->AddInstance(instance);
        }
        RayTracingInstance plane_instance = {};
        plane_instance._geometry = m_plane.get();
        plane_instance._instance_id = m_instanceCount - 1;
        plane_instance._hit_group_offset = 1;
        plane_instance._transform = MatrixScale(10.0f,1.0f,10.0f) * MatrixTranslation(0.0f, -1.0f, 0.0f);
        m_scene->Name("DXR_Sample_Scene");
        m_scene->AddInstance(plane_instance);
        m_scene->Apply();
        m_scene->Build();
    }

    void DXRSample::FillInstanceTransform(const Matrix4x4f &matrix, D3D12_RAYTRACING_INSTANCE_DESC &instance_desc)
    {
        ZeroMemory(instance_desc.Transform, sizeof(instance_desc.Transform));

        for (u32 row = 0; row < 3; ++row)
        {
            instance_desc.Transform[row][0] = matrix[0][row];
            instance_desc.Transform[row][1] = matrix[1][row];
            instance_desc.Transform[row][2] = matrix[2][row];
            instance_desc.Transform[row][3] = matrix[3][row];
        }
    }
}// namespace Ailu::RHI::DX12
