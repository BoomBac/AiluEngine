#pragma once
#ifndef __D3D_RAY_TRACING_SCENE_H__
#define __D3D_RAY_TRACING_SCENE_H__
#include <wrl/client.h>
#include "d3dx12.h"
#include "RHI/DX12/D3DResourceBase.h"
#include "RHI/DX12/DescriptorManager.h"
#include "Render/RayTracing/RayTracingScene.h"

namespace Ailu::RHI::DX12
{
    using Ailu::Render::BindParams;
    using Ailu::Render::UploadParams;
    using Ailu::Render::GraphicsContext;
    using Ailu::Render::RHICommandBuffer;
    using Microsoft::WRL::ComPtr;

    class D3DRayTracingScene : public Render::RayTracingScene
    {
        friend class D3DContext;
    public:
        D3DRayTracingScene();
        ~D3DRayTracingScene();
        D3D12_GPU_VIRTUAL_ADDRESS GetTLASGpuAddress() const { return _tlas_gpu_address; }
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& GetBuildDesc(bool is_update);
    private:
        void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) final;
        void BindImpl(RHICommandBuffer* rhi_cmd, const BindParams& params) final;
        void OnInstanceTransformChanged(u32 index) final;
        void OnInstanceChanged(u32 index) final;
        bool NeedRecreateBuildResource() const final;
        void PrepareBuild(bool is_rebuild) final;
    private:
        ComPtr<ID3D12Resource> _scratch_resource;
        ComPtr<ID3D12Resource> _instance_descs_resource;
        ComPtr<ID3D12Resource> _tlas_resource;
        D3DResourceStateGuard _scratch_state_guard;
        D3DResourceStateGuard _instance_descs_state_guard;
        D3DResourceStateGuard _tlas_state_guard;
        GPUVisibleDescriptorAllocation _tlas_srv_alloc;
        D3D12_GPU_VIRTUAL_ADDRESS _tlas_gpu_address = 0u;
        void* _mapped_instance_descs = nullptr;
        u32 _instance_capacity = 0u;
        Vector<D3D12_RAYTRACING_INSTANCE_DESC> _instance_descs;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC _build_desc;
    };
}

#endif // !__D3D_RAY_TRACING_SCENE_H__