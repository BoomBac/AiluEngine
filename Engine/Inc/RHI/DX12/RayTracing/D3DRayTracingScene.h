#pragma once
#ifndef __D3D_RAY_TRACING_SCENE_H__
#define __D3D_RAY_TRACING_SCENE_H__
#include "d3dx12.h"
#include "Render/RayTracing/RayTracingScene.h"

namespace Ailu::RHI::DX12
{
    using Ailu::Render::BindParams;
    using Ailu::Render::UploadParams;
    using Ailu::Render::GraphicsContext;
    using Ailu::Render::RHICommandBuffer;
    class D3DGPUBuffer;

    class D3DRayTracingScene : public Render::RayTracingScene
    {
    public:
        D3DRayTracingScene();
        ~D3DRayTracingScene();
        D3D12_GPU_VIRTUAL_ADDRESS GetTLASGpuAddress() const { return _tlas_gpu_address; }
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& GetBuildDesc() const { return _build_desc; }
    private:
        void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) final;
        void BindImpl(RHICommandBuffer* rhi_cmd, const BindParams& params) final;
    private:
        Ref<D3DGPUBuffer> _scratch_buffer;
        Ref<D3DGPUBuffer> _instance_descs_buffer;
        D3D12_GPU_VIRTUAL_ADDRESS _tlas_gpu_address = 0u;
        Vector<D3D12_RAYTRACING_INSTANCE_DESC> _instance_descs;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC _build_desc;
    };
}

#endif // !__D3D_RAY_TRACING_SCENE_H__