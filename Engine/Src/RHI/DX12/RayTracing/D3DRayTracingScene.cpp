#include "RHI/DX12/RayTracing/D3DRayTracingScene.h"
#include "RHI/DX12/RayTracing/D3DRayTracingGeometry.h"
#include "RHI/DX12/D3DBuffer.h"
#include "RHI/DX12/D3DContext.h"

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
    D3DRayTracingScene::D3DRayTracingScene()
    {
    }

    D3DRayTracingScene::~D3DRayTracingScene()
    {
    }

    void D3DRayTracingScene::UploadImpl(GraphicsContext *ctx, RHICommandBuffer *rhi_cmd, UploadParams *params)
    {
        if (_instances.empty())
        {
            _instance_descs.clear();
            _tlas_gpu_address = 0u;
            return;
        }

        AL_ASSERT(rhi_cmd != nullptr);

        auto* dev = dynamic_cast<D3DContext *>(ctx)->GetDevice();
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS topLevelBuildFlags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs = {};
        topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        topLevelInputs.Flags = topLevelBuildFlags;
        topLevelInputs.NumDescs = static_cast<UINT>(_instances.size());
        topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};
        dev->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
        AL_ASSERT(topLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

        BufferDesc buffer_desc = {};
        buffer_desc._target = Render::EGPUBufferTarget::kRaytraceAS;
        buffer_desc._size = static_cast<u32>(topLevelPrebuildInfo.ScratchDataSizeInBytes);
        buffer_desc._is_random_write = true;
        buffer_desc._init_state = EResourceState::kUnorderedAccess;
        buffer_desc._is_create_srv = false;
        buffer_desc._is_create_uav = false;
        _scratch_buffer = MakeRef<D3DGPUBuffer>(buffer_desc);
        _scratch_buffer->Name("scene_scratch_buffer");
        _scratch_buffer->Upload(ctx, rhi_cmd, nullptr);
        _mem_size += buffer_desc._size;
        buffer_desc._size = (u32) topLevelPrebuildInfo.ResultDataMaxSizeInBytes;
        buffer_desc._init_state = EResourceState::kRaytracingAccelerationStructure;
        buffer_desc._is_create_srv = true;
        auto tlas_buffer = MakeRef<D3DGPUBuffer>(buffer_desc);
        tlas_buffer->Name("scene_tlas_buffer");
        tlas_buffer->Upload(ctx, rhi_cmd, nullptr);
        _tlas_buffer = tlas_buffer;
        _mem_size += buffer_desc._size;
        buffer_desc._is_create_srv = false;
        _instance_descs.resize(_instances.size());
        for (u64 i = 0; i < _instances.size(); i++)
        {
            // Create an instance desc for the bottom-level acceleration structure.
            D3D12_RAYTRACING_INSTANCE_DESC &instanceDesc = _instance_descs[i];
            FillTransform(_instances[i]._transform, instanceDesc);
            instanceDesc.InstanceID = _instances[i]._instance_id;
            instanceDesc.InstanceContributionToHitGroupIndex = _instances[i]._hit_group_offset;
            instanceDesc.InstanceMask = _instances[i]._mask;
            instanceDesc.AccelerationStructure = static_cast<D3DRayTracingGeometry *>(_instances[i]._geometry)->GetBLASGpuAddress();
        }
        buffer_desc._target = Render::EGPUBufferTarget::kConstant;//上传堆
        buffer_desc._size = static_cast<u32>(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * _instances.size());
        buffer_desc._element_size = sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
        _instance_descs_buffer = MakeRef<D3DGPUBuffer>(buffer_desc);
        _instance_descs_buffer->Name("scene_instance_descs_buffer");
        _instance_descs_buffer->SetData(reinterpret_cast<const u8 *>(_instance_descs.data()), buffer_desc._size);
        _instance_descs_buffer->Upload(ctx, rhi_cmd, nullptr);
        _mem_size += buffer_desc._size;
        
        topLevelInputs.InstanceDescs = _instance_descs_buffer->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();
        _build_desc.Inputs = topLevelInputs;
        _tlas_gpu_address = tlas_buffer->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();
        _build_desc.SourceAccelerationStructureData = NULL;
        _build_desc.DestAccelerationStructureData = _tlas_gpu_address;
        _build_desc.ScratchAccelerationStructureData = _scratch_buffer->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();
        GpuResource::UploadImpl(ctx, rhi_cmd, params);
    }
    void D3DRayTracingScene::BindImpl(RHICommandBuffer *rhi_cmd, const BindParams &params)
    {
        BindParams new_params = params;
        new_params._is_compute_pipeline = true;
        new_params._is_random_access = false;
        _tlas_buffer->Bind(rhi_cmd, new_params);
    }

}