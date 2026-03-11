#pragma once
#ifndef __D3D_RAY_TRACING_GEOMETRY_H__
#define __D3D_RAY_TRACING_GEOMETRY_H__
#include "d3dx12.h"
#include "Render/RayTracing/RayTracingGeometry.h"

namespace Ailu::RHI::DX12
{
    using Ailu::Render::BindParams;
    using Ailu::Render::UploadParams;
    using Ailu::Render::GraphicsContext;
    using Ailu::Render::RHICommandBuffer;
    class D3DGPUBuffer;
    class D3DRayTracingGeometry : public Render::RayTracingGeometry
    {
        friend class D3DContext;
    public:
        D3DRayTracingGeometry(const Render::RayTracingGeometryDesc &desc);
        ~D3DRayTracingGeometry();
        D3D12_GPU_VIRTUAL_ADDRESS GetBLASGpuAddress() const { return _blas_gpu_address; }
    private:
        void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) final;
    private:
        Ref<D3DGPUBuffer> _scratch_buffer;
        D3D12_GPU_VIRTUAL_ADDRESS _blas_gpu_address = 0u;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS _inputs;
        D3D12_RAYTRACING_GEOMETRY_DESC _geometry_desc;
    };
}

#endif // !__D3D_RAY_TRACING_GEOMETRY_H__