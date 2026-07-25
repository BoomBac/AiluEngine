#pragma once
#ifndef __D3D_RAY_TRACING_GEOMETRY_H__
#define __D3D_RAY_TRACING_GEOMETRY_H__
#include <wrl/client.h>
#include "d3dx12.h"
#include "RHI/DX12/D3DResourceBase.h"
#include "Render/RayTracing/RayTracingGeometry.h"

using Microsoft::WRL::ComPtr;

namespace Ailu::RHI::DX12
{
    using ::Ailu::Render::BindParams;
    using ::Ailu::Render::UploadParams;
    using ::Ailu::Render::GraphicsContext;
    using ::Ailu::Render::RHICommandBuffer;
    class D3DRayTracingGeometry : public ::Ailu::Render::RayTracingGeometry
    {
        friend class D3DContext;
    public:
        D3DRayTracingGeometry(const ::Ailu::Render::RayTracingGeometryDesc &desc);
        ~D3DRayTracingGeometry();
        D3D12_GPU_VIRTUAL_ADDRESS GetBLASGpuAddress() const { return _blas_gpu_address; }
    private:
        void UploadImpl(GraphicsContext* ctx,RHICommandBuffer* rhi_cmd,UploadParams* params) final;
    private:
        ComPtr<ID3D12Resource> _scratch_resource;
        ComPtr<ID3D12Resource> _blas_resource;
        D3DResourceStateGuard _scratch_state_guard;
        D3DResourceStateGuard _blas_state_guard;
        D3D12_GPU_VIRTUAL_ADDRESS _blas_gpu_address = 0u;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS _inputs;
        Vector<D3D12_RAYTRACING_GEOMETRY_DESC> _geometry_descs;
    };
}

#endif // !__D3D_RAY_TRACING_GEOMETRY_H__
