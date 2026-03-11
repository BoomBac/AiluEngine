#include "RHI/DX12/RayTracing/D3DRayTracingGeometry.h"
#include "RHI/DX12/D3DBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/D3DCommandBuffer.h"


namespace Ailu::RHI::DX12
{
    D3DRayTracingGeometry::D3DRayTracingGeometry(const Render::RayTracingGeometryDesc &desc) : Render::RayTracingGeometry(desc)
    {
    }

    D3DRayTracingGeometry::~D3DRayTracingGeometry()
    {
    }

    void D3DRayTracingGeometry::UploadImpl(GraphicsContext *ctx, RHICommandBuffer *rhi_cmd, UploadParams *params)
    {
        if (_desc._vertex_buffer == nullptr || _desc._index_buffer == nullptr)
            return;

        AL_ASSERT(rhi_cmd != nullptr);

        auto *vb = dynamic_cast<D3DVertexBuffer *>(_desc._vertex_buffer);
        auto *ib = dynamic_cast<D3DIndexBuffer *>(_desc._index_buffer);
        if (vb == nullptr || ib == nullptr)
            return;


        _geometry_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        _geometry_desc.Triangles.IndexBuffer = ib->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();
        _geometry_desc.Triangles.IndexCount = _desc._index_count;
        _geometry_desc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        _geometry_desc.Triangles.Transform3x4 = 0;
        _geometry_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        _geometry_desc.Triangles.VertexCount = _desc._vertex_count;
        _geometry_desc.Triangles.VertexBuffer.StartAddress = vb->NativeResource(0u).As<ID3D12Resource>()->GetGPUVirtualAddress();
        _geometry_desc.Triangles.VertexBuffer.StrideInBytes = vb->GetLayout().GetStride(0);
        _geometry_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

        _inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        _inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        _inputs.NumDescs = 1;
        _inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        _inputs.pGeometryDescs = &_geometry_desc;

        auto* dev = dynamic_cast<D3DContext *>(ctx)->GetDevice();

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo = {};
        dev->GetRaytracingAccelerationStructurePrebuildInfo(&_inputs, &bottomLevelPrebuildInfo);
        AL_ASSERT(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);
        _scratch_buffer_size = static_cast<u32>(bottomLevelPrebuildInfo.ScratchDataSizeInBytes);
        BufferDesc buffer_desc = {};
        buffer_desc._target = Render::EGPUBufferTarget::kRaytraceAS;
        buffer_desc._size = _scratch_buffer_size;
        buffer_desc._is_random_write = true;
        buffer_desc._init_state = EResourceState::kUnorderedAccess;
        buffer_desc._is_create_srv = false;
        buffer_desc._is_create_uav = false;
        _scratch_buffer = MakeRef<D3DGPUBuffer>(buffer_desc);
        _scratch_buffer->Name(std::format("blas_{}_scratch", _desc._vertex_buffer->Name()));
        _scratch_buffer->Upload(ctx, rhi_cmd, nullptr);
        _mem_size += buffer_desc._size;

        buffer_desc._size = static_cast<u32>(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes);
        buffer_desc._init_state = EResourceState::kRaytracingAccelerationStructure;
        auto blas_buffer = MakeRef<D3DGPUBuffer>(buffer_desc);
        blas_buffer->Name(std::format("blas_{}", _desc._vertex_buffer->Name()));
        blas_buffer->Upload(ctx, rhi_cmd, nullptr);
        _blas_gpu_address = blas_buffer->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();
        _blas_buffer = blas_buffer;
        _mem_size += buffer_desc._size;
        //调用基类的UploadImpl以便正确统计内存使用量等
        GpuResource::UploadImpl(ctx, rhi_cmd, params);
    }
}// namespace Ailu::RHI::DX12
