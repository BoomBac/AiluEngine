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
    const WString c_hitGroupName0 = L"MyHitGroup0";
    const WString c_hitGroupName1 = L"MyHitGroup1";
    const WString c_raygenShaderName = L"MyRaygenShader";
    const WString c_closestHitShaderName0 = L"MyClosestHitShader0";
    const WString c_closestHitShaderName1 = L"MyClosestHitShader1";
    const WString c_missShaderName = L"MyMissShader";
    const WString c_shadowMissShaderName = L"MyShadowMissShader";

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
        if (instance_index >= m_instanceDescs.size())
            return;

        FillInstanceTransform(transform, m_instanceDescs[instance_index]);
        m_tlasDirty = true;
    }

    void DXRSample::Init(u16 w, u16 h)
    {
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
        D3DShaderCompileDesc desc = {};
        D3DShaderCompileOutput output;
        desc._filename = ResourceMgr::GetResSysPath(L"Shaders/hlsl/DXR/Raytracing.hlsl");
        desc._target = RenderConstants::kLibModel_6_3;
        if (!CreateFromFileDXC(desc, output) || output._byte_code == nullptr || output._library_reflection == nullptr)
        {
            LOG_ERROR("DXRSample: failed to compile raytracing shader library: {}", ToChar(desc._filename));
            return;
        }
        m_byte_code = output._byte_code;

        LoadLibraryReflection(output._library_reflection.Get());

        CreateRootSignatures();
        CreateRaytracingPipelineStateObject();
        BuildRaytracingAccelerationStructures();
        BuildShaderTables();
    }

    void DXRSample::LoadLibraryReflection(ID3D12LibraryReflection *library_reflection)
    {
        D3D12_LIBRARY_DESC lib_desc = {};
        library_reflection->GetDesc(&lib_desc);
        for (UINT i = 0; i < lib_desc.FunctionCount; ++i)
        {
            ID3D12FunctionReflection *func = library_reflection->GetFunctionByIndex(i);

            D3D12_FUNCTION_DESC func_desc = {};
            func->GetDesc(&func_desc);

            const char *name = func_desc.Name;
            for (UINT r = 0; r < func_desc.BoundResources; ++r)
            {
                D3D12_SHADER_INPUT_BIND_DESC bind_desc = {};
                func->GetResourceBindingDesc(r, &bind_desc);
                Render::ShaderBindResourceInfo info;
                info._name = bind_desc.Name;
                info._bind_slot = bind_desc.BindPoint;
                info._res_type = static_cast<Render::EBindResDescType>(bind_desc.Type);
            }
        }
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
        return;
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

        g_perSceneData->SetData(reinterpret_cast<u8 *>(&g_sceneData), sizeof(CBufferPerSceneData));
        g_perCamData->SetData(reinterpret_cast<u8 *>(&g_camData), sizeof(CBufferPerCameraData));
        UpdateTopLevelAccelerationStructure(cmd);
        ID3D12GraphicsCommandList4 *commandList = static_cast<D3DCommandBuffer *>(cmd)->NativeCmdList();

        commandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());

        // Bind the heaps, acceleration structure and dispatch rays.
        D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
        BindParams params;
        params._is_compute_pipeline = true;
        params._is_random_access = false;
        params._slot = 0;
        //commandList->SetDescriptorHeaps(1, m_descriptorHeap.GetAddressOf());
        m_topLevelAS->Bind(cmd, params);
        //m_scene->Bind(cmd, params);
        params._slot = 2;

        _vertex_data->Bind(cmd, params);
        params._slot = 3;
        _normal_data->Bind(cmd, params);
        params._slot = 4;
        _indices_data->Bind(cmd, params);
        params._slot = 5;
        _instance_geometry_data->Bind(cmd, params);

        params._slot = 6;
        g_perSceneData->Bind(cmd, params);
        params._slot = 7;
        g_perCamData->Bind(cmd, params);

        params._is_random_access = true;
        params._slot = 1;
        params._params._texture_binder._view_idx = m_uav_output->CalculateViewIndex(Render::Texture::ETextureViewType::kUAV, 0, 0);
        params._params._texture_binder._sub_res = UINT32_MAX;
        m_uav_output->Bind(cmd, params);

        //commandList->SetComputeRootDescriptorTable(0, m_raytracingOutputResourceUAVGpuDescriptor);
        //commandList->SetComputeRootShaderResourceView(1, m_topLevelAccelerationStructure->GetGPUVirtualAddress());
        //DispatchRays(m_dxrCommandList.Get(), m_dxrStateObject.Get(), &dispatchDesc);
        // Since each shader table has only one shader record, the stride is same as the size.
        dispatchDesc.HitGroupTable.StartAddress = m_hitGroupShaderTable->GetGPUVirtualAddress();
        dispatchDesc.HitGroupTable.SizeInBytes = m_hitGroupShaderTable->GetDesc().Width;
        dispatchDesc.HitGroupTable.StrideInBytes = m_hitGroupShaderRecordSize;
        dispatchDesc.MissShaderTable.StartAddress = m_missShaderTable->GetGPUVirtualAddress();
        dispatchDesc.MissShaderTable.SizeInBytes = m_missShaderTable->GetDesc().Width;
        dispatchDesc.MissShaderTable.StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        dispatchDesc.RayGenerationShaderRecord.StartAddress = m_rayGenShaderTable->GetGPUVirtualAddress();
        dispatchDesc.RayGenerationShaderRecord.SizeInBytes = m_rayGenShaderTable->GetDesc().Width;
        dispatchDesc.Width = w;
        dispatchDesc.Height = h;
        dispatchDesc.Depth = 1;
        commandList->SetPipelineState1(m_dxrStateObject.Get());
        commandList->DispatchRays(&dispatchDesc);
    }

    Ref<GPUBuffer> DXRSample::BuildBottomLevelAccelerationStructure(Render::Mesh *mesh)
    {
        if (mesh == nullptr)
            return nullptr;

        auto *vb = dynamic_cast<D3DVertexBuffer *>(mesh->GetVertexBuffer().get());
        auto *ib = dynamic_cast<D3DIndexBuffer *>(mesh->GetIndexBuffer().get());
        if (vb == nullptr || ib == nullptr)
            return nullptr;

        D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
        geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometryDesc.Triangles.IndexBuffer = ib->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();
        geometryDesc.Triangles.IndexCount = ib->GetCount();
        geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        geometryDesc.Triangles.Transform3x4 = 0;
        geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geometryDesc.Triangles.VertexCount = vb->GetVertexCount();
        geometryDesc.Triangles.VertexBuffer.StartAddress = vb->NativeResource(0u).As<ID3D12Resource>()->GetGPUVirtualAddress();
        geometryDesc.Triangles.VertexBuffer.StrideInBytes = vb->GetLayout().GetStride(0);
        geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputs = {};
        bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        bottomLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        bottomLevelInputs.NumDescs = 1;
        bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        bottomLevelInputs.pGeometryDescs = &geometryDesc;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo = {};
        m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
        AL_ASSERT(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

        BufferDesc buffer_desc = {};
        buffer_desc._target = EGPUBufferTarget::kRaytraceAS;
        buffer_desc._size = static_cast<u32>(bottomLevelPrebuildInfo.ScratchDataSizeInBytes);
        buffer_desc._is_random_write = true;
        buffer_desc._init_state = EResourceState::kUnorderedAccess;
        buffer_desc._is_create_srv = false;
        buffer_desc._is_create_uav = false;
        Ref<GPUBuffer> scratch_buffer = GPUBuffer::Create(buffer_desc, "DXR_Sample_BLAS_Scratch_Buffer");
        scratch_buffer->ApplySync();

        buffer_desc._size = static_cast<u32>(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes);
        buffer_desc._init_state = EResourceState::kRaytracingAccelerationStructure;
        Ref<GPUBuffer> bottom_level_as = GPUBuffer::CreateSync(buffer_desc, "DXR_Sample_BLAS_Buffer");

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
        bottomLevelBuildDesc.Inputs = bottomLevelInputs;
        bottomLevelBuildDesc.ScratchAccelerationStructureData = dynamic_cast<D3DGPUBuffer *>(scratch_buffer.get())->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();
        bottomLevelBuildDesc.DestAccelerationStructureData = dynamic_cast<D3DGPUBuffer *>(bottom_level_as.get())->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();

        auto cmd = RHICommandBufferPool::Get();
        auto d3d_cmd = dynamic_cast<D3DCommandBuffer *>(cmd.get());
        d3d_cmd->NativeCmdList()->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
        cmd->InsertUAVBarrier();
        GraphicsContext::Get().ExecuteRHICommandBuffer(cmd.get());
        RHICommandBufferPool::Release(cmd);
        GraphicsContext::Get().WaitForGpu();
        return bottom_level_as;
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

        m_cube = RayTracingGeometry::Create(RayTracingGeometryDesc(mesh.get(), 0, true));
        m_cube->Apply();
        m_cube->Build();
        m_plane = RayTracingGeometry::Create(RayTracingGeometryDesc(plane.get(), 0, true));
        m_plane->Apply();
        m_plane->Build();

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
        m_scene->AddInstance(plane_instance);
        m_scene->Apply();
        m_scene->Build();

        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS topLevelBuildFlags =
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs = {};
        topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        topLevelInputs.Flags = topLevelBuildFlags;
        topLevelInputs.NumDescs = m_instanceCount;
        topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};
        m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
        AL_ASSERT(topLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

        BufferDesc buffer_desc = {};
        buffer_desc._target = EGPUBufferTarget::kRaytraceAS;
        buffer_desc._size = static_cast<u32>(topLevelPrebuildInfo.ScratchDataSizeInBytes);
        buffer_desc._is_random_write = true;
        buffer_desc._init_state = EResourceState::kUnorderedAccess;
        buffer_desc._is_create_srv = false;
        buffer_desc._is_create_uav = false;
        m_tlasScratchBuffer = GPUBuffer::Create(buffer_desc, "DXR_Sample_Scratch_Buffer");
        m_tlasScratchBuffer->ApplySync();
        // Allocate resources for acceleration structures.
        // Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent).
        // Default heap is OK since the application doesn't need CPU read/write access to them.
        // The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        // and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both:
        //  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
        //  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
        {
            m_bottomLevelAS = BuildBottomLevelAccelerationStructure(mesh.get());
            _plane_blas = plane ? BuildBottomLevelAccelerationStructure(plane.get()) : nullptr;
            if (m_bottomLevelAS == nullptr || _plane_blas == nullptr)
            {
                LOG_ERROR("DXRSample: failed to build BLAS for {}{}.",
                          m_bottomLevelAS == nullptr ? "monkey" : "",
                          _plane_blas == nullptr ? (m_bottomLevelAS == nullptr ? " and plane" : "plane") : "");
                return;
            }

            buffer_desc._size = (u32) topLevelPrebuildInfo.ResultDataMaxSizeInBytes;
            buffer_desc._init_state = EResourceState::kRaytracingAccelerationStructure;
            buffer_desc._is_create_srv = true;
            m_topLevelAS = GPUBuffer::CreateSync(buffer_desc, "DXR_Sample_TLAS_Buffer");
            buffer_desc._is_create_srv = false;
        }

        m_instanceDescs.resize(m_instanceCount);
        for (u32 i = 0; i < 2; i++)
        {
            // Create an instance desc for the bottom-level acceleration structure.
            D3D12_RAYTRACING_INSTANCE_DESC &instanceDesc = m_instanceDescs[i];
            const Matrix4x4f instance_transform = MatrixTranslation((i == 0) ? -0.5f : 0.5f, 0.0f, 0.0f);
            FillInstanceTransform(instance_transform, instanceDesc);
            instanceDesc.InstanceID = i;// This can be used in shader to identify the instance.
            instanceDesc.InstanceContributionToHitGroupIndex = i;
            instanceDesc.InstanceMask = 1;
            instanceDesc.AccelerationStructure = dynamic_cast<D3DGPUBuffer *>(m_bottomLevelAS.get())->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();
        }
        // Create an instance desc for the plane bottom-level acceleration structure.
        D3D12_RAYTRACING_INSTANCE_DESC &planeInstanceDesc = m_instanceDescs[2];
        const Matrix4x4f planeTransform = MatrixScale(10.0f,1.0f,10.0f) * MatrixTranslation(0.0f, -1.0f, 0.0f);
        FillInstanceTransform(planeTransform, planeInstanceDesc);
        planeInstanceDesc.InstanceID = 2;
        planeInstanceDesc.InstanceContributionToHitGroupIndex = 1;
        planeInstanceDesc.InstanceMask = 1;
        planeInstanceDesc.AccelerationStructure = dynamic_cast<D3DGPUBuffer *>(_plane_blas.get())->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();

        // Create an instance desc for the bottom-level acceleration structure.
        // D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
        // instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1;
        // instanceDesc.InstanceMask = 1;
        // instanceDesc.AccelerationStructure = dynamic_cast<D3DGPUBuffer *>(m_bottomLevelAS.get())->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();
        buffer_desc._target = EGPUBufferTarget::kConstant;//上传堆
        buffer_desc._size = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * m_instanceCount;
        buffer_desc._element_size = sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
        m_instanceDescsBuffer = GPUBuffer::Create(buffer_desc, "DXR_Sample_Instance_Descs_Buffer");
        m_instanceDescsBuffer->SetData(reinterpret_cast<const u8 *>(m_instanceDescs.data()), buffer_desc._size);
        m_instanceDescsBuffer->ApplySync();

        // Top Level Acceleration Structure desc
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc = {};
        {
            topLevelInputs.InstanceDescs = dynamic_cast<D3DGPUBuffer *>(m_instanceDescsBuffer.get())->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();
            topLevelBuildDesc.Inputs = topLevelInputs;
            topLevelBuildDesc.DestAccelerationStructureData = dynamic_cast<D3DGPUBuffer *>(m_topLevelAS.get())->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();
            topLevelBuildDesc.ScratchAccelerationStructureData = dynamic_cast<D3DGPUBuffer *>(m_tlasScratchBuffer.get())->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();
        }

        auto cmd = RHICommandBufferPool::Get();
        auto d3d_cmd = dynamic_cast<D3DCommandBuffer *>(cmd.get());
        d3d_cmd->NativeCmdList()->BuildRaytracingAccelerationStructure(&topLevelBuildDesc, 0, nullptr);
        GraphicsContext::Get().ExecuteRHICommandBuffer(cmd.get());
        RHICommandBufferPool::Release(cmd);
        GraphicsContext::Get().WaitForGpu();
        m_tlasDirty = false;
    }

    void DXRSample::RebuildTopLevelAccelerationStructure(RHICommandBuffer *cmd)
    {
        if (m_instanceDescsBuffer == nullptr || m_tlasScratchBuffer == nullptr || m_topLevelAS == nullptr)
            return;

        const u32 instance_descs_size = static_cast<u32>(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * m_instanceDescs.size());
        m_instanceDescsBuffer->SetData(reinterpret_cast<const u8 *>(m_instanceDescs.data()), instance_descs_size);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs = {};
        topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        topLevelInputs.Flags =
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        topLevelInputs.NumDescs = m_instanceCount;
        topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        topLevelInputs.InstanceDescs = dynamic_cast<D3DGPUBuffer *>(m_instanceDescsBuffer.get())->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc = {};
        topLevelBuildDesc.Inputs = topLevelInputs;
        topLevelBuildDesc.DestAccelerationStructureData = dynamic_cast<D3DGPUBuffer *>(m_topLevelAS.get())->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();
        topLevelBuildDesc.ScratchAccelerationStructureData =
                dynamic_cast<D3DGPUBuffer *>(m_tlasScratchBuffer.get())->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();

        auto d3d_cmd = dynamic_cast<D3DCommandBuffer *>(cmd);
        d3d_cmd->NativeCmdList()->BuildRaytracingAccelerationStructure(&topLevelBuildDesc, 0, nullptr);
        cmd->InsertUAVBarrier();
        m_tlasDirty = false;
    }

    void DXRSample::UpdateTopLevelAccelerationStructure(RHICommandBuffer *cmd)
    {
        if (!m_tlasDirty || m_instanceDescsBuffer == nullptr || m_tlasScratchBuffer == nullptr || m_topLevelAS == nullptr)
            return;

        const u32 instance_descs_size = static_cast<u32>(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * m_instanceDescs.size());
        m_instanceDescsBuffer->SetData(reinterpret_cast<const u8 *>(m_instanceDescs.data()), instance_descs_size);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs = {};
        topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        topLevelInputs.Flags =
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        topLevelInputs.NumDescs = m_instanceCount;
        topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        topLevelInputs.InstanceDescs = dynamic_cast<D3DGPUBuffer *>(m_instanceDescsBuffer.get())->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();

        const D3D12_GPU_VIRTUAL_ADDRESS top_level_as_address =
                dynamic_cast<D3DGPUBuffer *>(m_topLevelAS.get())->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc = {};
        topLevelBuildDesc.Inputs = topLevelInputs;
        topLevelBuildDesc.SourceAccelerationStructureData = top_level_as_address;
        topLevelBuildDesc.DestAccelerationStructureData = top_level_as_address;
        topLevelBuildDesc.ScratchAccelerationStructureData =
                dynamic_cast<D3DGPUBuffer *>(m_tlasScratchBuffer.get())->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();

        auto d3d_cmd = dynamic_cast<D3DCommandBuffer *>(cmd);
        d3d_cmd->NativeCmdList()->BuildRaytracingAccelerationStructure(&topLevelBuildDesc, 0, nullptr);
        cmd->InsertUAVBarrier();
        m_tlasDirty = false;
    }

    // Local root signature and shader association
    // This is a root signature that enables a shader to have unique arguments that come from shader tables.
    void DXRSample::CreateLocalRootSignatureSubobjects(CD3DX12_STATE_OBJECT_DESC *raytracingPipeline)
    {
        // Hit group and miss shaders in this sample are not using a local root signature and thus one is not associated with them.

        // Local root signature to be used in a ray gen shader.
        {
            auto localRootSignature = raytracingPipeline->CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
            localRootSignature->SetRootSignature(m_raytracingLocalRootSignature.Get());
            // Shader association
            auto rootSignatureAssociation = raytracingPipeline->CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
            rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
            rootSignatureAssociation->AddExport(c_raygenShaderName.c_str());
        }
    }

    void DXRSample::SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC &desc, ComPtr<ID3D12RootSignature> *rootSig)
    {
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> error;

        ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
        ThrowIfFailed(m_dxrDevice->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&(*rootSig))));
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

    void DXRSample::CreateRootSignatures()
    {
        // Global Root Signature
        // This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
        {
            CD3DX12_ROOT_PARAMETER rootParameters[8];
            CD3DX12_DESCRIPTOR_RANGE srvRanges[5];
            srvRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
            srvRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
            srvRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
            srvRanges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
            srvRanges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);

            rootParameters[0].InitAsDescriptorTable(1, &srvRanges[0]);
            rootParameters[2].InitAsDescriptorTable(1, &srvRanges[1]);
            rootParameters[3].InitAsDescriptorTable(1, &srvRanges[2]);
            rootParameters[4].InitAsDescriptorTable(1, &srvRanges[3]);
            rootParameters[5].InitAsDescriptorTable(1, &srvRanges[4]);

            CD3DX12_DESCRIPTOR_RANGE cbvRanges[2];
            cbvRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
            cbvRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 2);

            rootParameters[6].InitAsConstantBufferView(1);
            rootParameters[7].InitAsConstantBufferView(2);

            CD3DX12_DESCRIPTOR_RANGE uavRange;
            uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
            rootParameters[1].InitAsDescriptorTable(1, &uavRange);

            CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
            SerializeAndCreateRaytracingRootSignature(globalRootSignatureDesc, &m_raytracingGlobalRootSignature);
        }

        // Local Root Signature
        // This is a root signature that enables a shader to have unique arguments that come from shader tables.
        {
            CD3DX12_ROOT_PARAMETER rootParameters[1];
            rootParameters[0].InitAsConstants(SizeOfInUint32(m_rayGenCB), 0, 0);
            CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
            localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
            SerializeAndCreateRaytracingRootSignature(localRootSignatureDesc, &m_raytracingLocalRootSignature);
        }
    }

    // Build shader tables.
    // This encapsulates all shader records - shaders and the arguments for their local root signatures.
    void DXRSample::BuildShaderTables()
    {
        auto device = m_dxrDevice;

        void *rayGenShaderIdentifier;
        void *missShaderIdentifier;
        void *shadowMissShaderIdentifier;
        void *hitGroupShaderIdentifier0;
        void *hitGroupShaderIdentifier1;

        auto GetShaderIdentifiers = [&](auto *stateObjectProperties)
        {
            rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_raygenShaderName.c_str());
            missShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_missShaderName.c_str());
            shadowMissShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_shadowMissShaderName.c_str());
            hitGroupShaderIdentifier0 = stateObjectProperties->GetShaderIdentifier(c_hitGroupName0.c_str());
            hitGroupShaderIdentifier1 = stateObjectProperties->GetShaderIdentifier(c_hitGroupName1.c_str());
        };

        // Get shader identifiers.
        UINT shaderIdentifierSize;
        {
            ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
            ThrowIfFailed(m_dxrStateObject.As(&stateObjectProperties));
            GetShaderIdentifiers(stateObjectProperties.Get());
            shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        }

        // Ray gen shader table
        {
            struct RootArguments
            {
                RayGenConstantBuffer cb;
            } rootArguments;
            rootArguments.cb = m_rayGenCB;

            UINT numShaderRecords = 1;
            UINT shaderRecordSize = shaderIdentifierSize + sizeof(rootArguments);
            ShaderTable rayGenShaderTable(device, numShaderRecords, shaderRecordSize, L"RayGenShaderTable");
            rayGenShaderTable.push_back(ShaderRecord(rayGenShaderIdentifier, shaderIdentifierSize, &rootArguments, sizeof(rootArguments)));
            m_rayGenShaderTable = rayGenShaderTable.GetResource();
        }

        // Miss shader table
        {
            UINT numShaderRecords = 2;
            UINT shaderRecordSize = shaderIdentifierSize;
            ShaderTable missShaderTable(device, numShaderRecords, shaderRecordSize, L"MissShaderTable");
            missShaderTable.push_back(ShaderRecord(missShaderIdentifier, shaderIdentifierSize));
            missShaderTable.push_back(ShaderRecord(shadowMissShaderIdentifier, shaderIdentifierSize));
            m_missShaderTable = missShaderTable.GetResource();
        }

        // Hit group shader table
        {
            UINT numShaderRecords = 2;
            UINT shaderRecordSize = shaderIdentifierSize;
            m_hitGroupShaderRecordSize = shaderRecordSize;
            ShaderTable hitGroupShaderTable(device, numShaderRecords, shaderRecordSize, L"HitGroupShaderTable");
            hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier0, shaderIdentifierSize));
            hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier1, shaderIdentifierSize));
            m_hitGroupShaderTable = hitGroupShaderTable.GetResource();
        }
    }

    void DXRSample::CreateRaytracingPipelineStateObject()
    {
        // Create 7 subobjects that combine into a RTPSO:
        // Subobjects need to be associated with DXIL exports (i.e. shaders) either by way of default or explicit associations.
        // Default association applies to every exported shader entrypoint that doesn't have any of the same type of subobject associated with it.
        // This simple sample utilizes default shader association except for local root signature subobject
        // which has an explicit association specified purely for demonstration purposes.
        // 1 - DXIL library
        // 1 - Triangle hit group
        // 1 - Shader config
        // 2 - Local root signature and association
        // 1 - Global root signature
        // 1 - Pipeline config
        //创建一个RTPSO需要创建7个子对象，raytracingPipeline是一个容器
        CD3DX12_STATE_OBJECT_DESC raytracingPipeline{D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};
        // DXIL library
        // This contains the shaders and their entrypoints for the state object.
        // Since shaders are not considered a subobject, they need to be passed in via DXIL library subobjects.
        auto lib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
        D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE(m_byte_code->GetBufferPointer(), m_byte_code->GetBufferSize());
        lib->SetDXILLibrary(&libdxil);
        //指定哪些入口外部可见，默认是全部可见
        {
            lib->DefineExport(c_raygenShaderName.c_str());
            lib->DefineExport(c_closestHitShaderName0.c_str());
            lib->DefineExport(c_closestHitShaderName1.c_str());
            lib->DefineExport(c_missShaderName.c_str());
            lib->DefineExport(c_shadowMissShaderName.c_str());
        }

        // Triangle hit group
        // A hit group specifies closest hit, any hit and intersection shaders to be executed when a ray intersects the geometry's triangle/AABB.
        // In this sample, we only use triangle geometry with a closest hit shader, so others are not set.
        auto hitGroup0 = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
        hitGroup0->SetClosestHitShaderImport(c_closestHitShaderName0.c_str());
        hitGroup0->SetHitGroupExport(c_hitGroupName0.c_str());//SBT 用这个名字查 Shader Identifier
        hitGroup0->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

        auto hitGroup1 = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
        hitGroup1->SetClosestHitShaderImport(c_closestHitShaderName1.c_str());
        hitGroup1->SetHitGroupExport(c_hitGroupName1.c_str());//SBT 用这个名字查 Shader Identifier
        hitGroup1->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

        // Shader config
        // Defines the maximum sizes in bytes for the ray payload and attribute structure.
        auto shaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
        UINT payloadSize = 4 * sizeof(float);  // float4 color
        UINT attributeSize = 2 * sizeof(float);// float2 barycentrics
        shaderConfig->Config(payloadSize, attributeSize);

        // Local root signature and shader association
        CreateLocalRootSignatureSubobjects(&raytracingPipeline);
        // This is a root signature that enables a shader to have unique arguments that come from shader tables.

        // Global root signature
        // This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
        auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
        globalRootSignature->SetRootSignature(m_raytracingGlobalRootSignature.Get());

        // Pipeline config
        // Defines the maximum TraceRay() recursion depth.
        auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
        // PERFOMANCE TIP: Set max recursion depth as low as needed
        // as drivers may apply optimization strategies for low recursion depths.
        UINT maxRecursionDepth = 2;// primary ray + one shadow ray.
        pipelineConfig->Config(maxRecursionDepth);
#if _DEBUG

#endif
        PrintStateObjectDesc(raytracingPipeline);
        // Create the state object.
        //, L"Couldn't create DirectX Raytracing state object.\n"
        auto hr = m_dxrDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_dxrStateObject));
        ThrowIfFailed(hr);
    }
}// namespace Ailu::RHI::DX12
