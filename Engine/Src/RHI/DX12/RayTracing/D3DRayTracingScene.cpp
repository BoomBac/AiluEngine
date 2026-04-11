#include "RHI/DX12/RayTracing/D3DRayTracingScene.h"
#include "RHI/DX12/RayTracing/D3DRayTracingGeometry.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/dxhelper.h"

namespace Ailu::RHI::DX12
{
    static void FillTransform(const Matrix4x4f &matrix, D3D12_RAYTRACING_INSTANCE_DESC &instance_desc)
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

    namespace
    {
        u32 GrowInstanceCapacity(u32 required_capacity, u32 current_capacity)
        {
            u32 new_capacity = std::max(4u, current_capacity);
            while (new_capacity < required_capacity)
                new_capacity *= 2u;
            return new_capacity;
        }

        void CreateDefaultBufferResource(D3DContext* ctx,
                                         u64 size,
                                         D3D12_RESOURCE_STATES init_state,
                                         bool allow_uav,
                                         const String& name,
                                         ComPtr<ID3D12Resource>& resource,
                                         D3DResourceStateGuard& state_guard)
        {
            auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
            if (allow_uav)
                desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            ThrowIfFailed(ctx->GetDevice()->CreateCommittedResource(
                &heap_prop,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                init_state,
                nullptr,
                IID_PPV_ARGS(resource.ReleaseAndGetAddressOf())));
            resource->SetName(ToWChar(name).c_str());
            state_guard = D3DResourceStateGuard(resource.Get(), init_state, 1u);
        }

        void CreateUploadBufferResource(D3DContext* ctx,
                                        const void* data,
                                        u64 size,
                                        const String& name,
                                        ComPtr<ID3D12Resource>& resource,
                                        D3DResourceStateGuard& state_guard,
                                        void** mapped_data = nullptr)
        {
            auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
            auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            ThrowIfFailed(ctx->GetDevice()->CreateCommittedResource(
                &heap_prop,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(resource.ReleaseAndGetAddressOf())));
            resource->SetName(ToWChar(name).c_str());
            state_guard = D3DResourceStateGuard(resource.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, 1u);

            void* mapped = nullptr;
            ThrowIfFailed(resource->Map(0, nullptr, &mapped));
            memcpy(mapped, data, size);
            if (mapped_data)
                *mapped_data = mapped;
            else
                resource->Unmap(0, nullptr);
        }

        void FillInstanceDesc(const Render::RayTracingInstance& instance, D3D12_RAYTRACING_INSTANCE_DESC& instance_desc)
        {
            FillTransform(instance._transform, instance_desc);
            instance_desc.InstanceID = instance._instance_id;
            instance_desc.InstanceContributionToHitGroupIndex = instance._hit_group_offset;
            instance_desc.InstanceMask = instance._mask;
            instance_desc.AccelerationStructure = static_cast<D3DRayTracingGeometry *>(instance._geometry)->GetBLASGpuAddress();
        }
    }

    D3DRayTracingScene::D3DRayTracingScene()
    {

    }

    D3DRayTracingScene::~D3DRayTracingScene()
    {
        if (_instance_descs_resource && _mapped_instance_descs)
        {
            _instance_descs_resource->Unmap(0, nullptr);
            _mapped_instance_descs = nullptr;
        }
        D3DDescriptorMgr::Get().Free(std::move(_tlas_srv_alloc));
        if (::Ailu::Render::g_pGfxContext)
            ::Ailu::Render::g_pGfxContext->WaitForFence(_fence_value);
    }

    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& D3DRayTracingScene::GetBuildDesc(bool is_update)
    {
        if (is_update)
        {
            _build_desc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
            _build_desc.SourceAccelerationStructureData = _tlas_gpu_address;
        }
        else
        {
            _build_desc.Inputs.Flags &= ~D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
            _build_desc.SourceAccelerationStructureData = 0u;
        }
        _build_desc.DestAccelerationStructureData = _tlas_gpu_address;
        return _build_desc;
    }

    bool D3DRayTracingScene::NeedRecreateBuildResource() const
    {
        if (_instances.empty())
            return false;

        if (_instance_capacity == 0u)
            return false;

        return _instances.size() > _instance_capacity;
    }

    void D3DRayTracingScene::PrepareBuild(bool is_rebuild)
    {
        if (_instances.empty() || _tlas_resource == nullptr || _instance_descs_resource == nullptr)
            return;

        _native_resource = {Render::RendererAPI::ERenderAPI::kDirectX12, _tlas_resource.Get()};
        _tlas_gpu_address = _tlas_resource->GetGPUVirtualAddress();
        _build_desc.Inputs.NumDescs = static_cast<UINT>(_instances.size());
        _build_desc.Inputs.InstanceDescs = _instance_descs_resource->GetGPUVirtualAddress();
        _build_desc.DestAccelerationStructureData = _tlas_gpu_address;
        _build_desc.ScratchAccelerationStructureData = _scratch_resource ? _scratch_resource->GetGPUVirtualAddress() : 0u;

        if (is_rebuild)
        {
            for (u32 index = 0u; index < _instances.size(); ++index)
                OnInstanceChanged(index);
        }
    }

    void D3DRayTracingScene::UploadImpl(GraphicsContext *ctx, RHICommandBuffer *rhi_cmd, UploadParams *params)
    {
        if (_instances.empty())
        {
            _instance_descs.clear();
            _tlas_gpu_address = 0u;
            _native_resource = {};
            LOG_ERROR("RayTracingScene({}) has no instance, skip building TLAS",Name());
            return;
        }

        if (_instance_descs_resource && _mapped_instance_descs)
        {
            _instance_descs_resource->Unmap(0, nullptr);
            _mapped_instance_descs = nullptr;
        }
        D3DDescriptorMgr::Get().Free(std::move(_tlas_srv_alloc));

        AL_ASSERT(rhi_cmd != nullptr);

        auto* dev = dynamic_cast<D3DContext *>(ctx)->GetDevice();
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS topLevelBuildFlags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

        _instance_capacity = GrowInstanceCapacity(static_cast<u32>(_instances.size()), _instance_capacity);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs = {};
        topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        topLevelInputs.Flags = topLevelBuildFlags;
        topLevelInputs.NumDescs = static_cast<UINT>(_instances.size());
        topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS allocationInputs = topLevelInputs;
        allocationInputs.NumDescs = _instance_capacity;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};
        dev->GetRaytracingAccelerationStructurePrebuildInfo(&allocationInputs, &topLevelPrebuildInfo);
        AL_ASSERT(topLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

        CreateDefaultBufferResource(dynamic_cast<D3DContext *>(ctx),
                        topLevelPrebuildInfo.ScratchDataSizeInBytes,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        true,
                        "scene_scratch_buffer",
                        _scratch_resource,
                        _scratch_state_guard);
        _mem_size += static_cast<u32>(topLevelPrebuildInfo.ScratchDataSizeInBytes);

        CreateDefaultBufferResource(dynamic_cast<D3DContext *>(ctx),
                        topLevelPrebuildInfo.ResultDataMaxSizeInBytes,
                        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                        true,
                        "scene_tlas_buffer",
                        _tlas_resource,
                        _tlas_state_guard);
        _tlas_gpu_address = _tlas_resource->GetGPUVirtualAddress();
        _native_resource = {Render::RendererAPI::ERenderAPI::kDirectX12, _tlas_resource.Get()};
        _mem_size += static_cast<u32>(topLevelPrebuildInfo.ResultDataMaxSizeInBytes);

        _tlas_srv_alloc = D3DDescriptorMgr::Get().AllocGPU(1u);
        auto [srv_cpu, srv_gpu] = _tlas_srv_alloc.At(0);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        srv_desc.RaytracingAccelerationStructure.Location = _tlas_gpu_address;
        dev->CreateShaderResourceView(nullptr, &srv_desc, srv_cpu);

        _instance_descs.clear();
        _instance_descs.resize(_instance_capacity);
        for (u64 i = 0; i < _instances.size(); i++)
        {
            FillInstanceDesc(_instances[i], _instance_descs[i]);
        }
        const u64 instance_descs_size = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * _instance_capacity;
        CreateUploadBufferResource(dynamic_cast<D3DContext *>(ctx),
                                   _instance_descs.data(),
                                   instance_descs_size,
                                   "scene_instance_descs_buffer",
                                   _instance_descs_resource,
                                   _instance_descs_state_guard,
                                   &_mapped_instance_descs);
        _mem_size += instance_descs_size;
        
        topLevelInputs.InstanceDescs = _instance_descs_resource->GetGPUVirtualAddress();
        _build_desc.Inputs = topLevelInputs;
        _build_desc.SourceAccelerationStructureData = 0u;
        _build_desc.DestAccelerationStructureData = _tlas_gpu_address;
        _build_desc.ScratchAccelerationStructureData = _scratch_resource->GetGPUVirtualAddress();
        GpuResource::UploadImpl(ctx, rhi_cmd, params);
    }
    void D3DRayTracingScene::BindImpl(RHICommandBuffer *rhi_cmd, const BindParams &params)
    {
        GpuResource::BindImpl(rhi_cmd, params);
        auto d3dcmd = dynamic_cast<D3DCommandBuffer *>(rhi_cmd);
        _tlas_srv_alloc.CommitDescriptorsForDispatch(d3dcmd, params._slot);
    }

    void D3DRayTracingScene::OnInstanceTransformChanged(u32 index)
    {
        FillTransform(_instances[index]._transform, _instance_descs[index]);
        if (_mapped_instance_descs)
        {
            auto* mapped_descs = reinterpret_cast<D3D12_RAYTRACING_INSTANCE_DESC *>(_mapped_instance_descs);
            mapped_descs[index] = _instance_descs[index];
        }
    }

    void D3DRayTracingScene::OnInstanceChanged(u32 index)
    {
        FillInstanceDesc(_instances[index], _instance_descs[index]);
        if (_mapped_instance_descs)
        {
            auto* mapped_descs = reinterpret_cast<D3D12_RAYTRACING_INSTANCE_DESC *>(_mapped_instance_descs);
            mapped_descs[index] = _instance_descs[index];
        }
    }

}