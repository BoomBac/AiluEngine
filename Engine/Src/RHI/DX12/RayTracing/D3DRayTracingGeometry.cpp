#include "RHI/DX12/RayTracing/D3DRayTracingGeometry.h"
#include "RHI/DX12/D3DBuffer.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "RHI/DX12/dxhelper.h"


namespace Ailu::RHI::DX12
{
    namespace
    {
        void CreateBufferResource(D3DContext* ctx,
                                  u64 size,
                                  bool allow_uav,
                                  const String& name,
                                  D3DResource &resource)
        {
            auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
            if (allow_uav)
                desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            ThrowIfFailed(ctx->GetDevice()->CreateCommittedResource(
                &heap_prop,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(resource.GetAddressOf())));
            resource->SetName(ToWChar(name).c_str());
            resource._subresource_count = 1u;
            resource.ResetStateId();
        }
    }

    D3DRayTracingGeometry::D3DRayTracingGeometry(const Render::RayTracingGeometryDesc &desc) : Render::RayTracingGeometry(desc)
    {
    }

    D3DRayTracingGeometry::~D3DRayTracingGeometry()
    {
        if (::Ailu::Render::g_pGfxContext)
            ::Ailu::Render::g_pGfxContext->WaitForFence(_fence_value);
    }

    void D3DRayTracingGeometry::UploadImpl(GraphicsContext *ctx, RHICommandBuffer *rhi_cmd, UploadParams *params)
    {
        AL_ASSERT(rhi_cmd != nullptr);
        auto *vb = dynamic_cast<D3DVertexBuffer *>(_desc._vertex_buffer);
        _geometry_descs.clear();
        _geometry_descs.reserve(_desc._index_buffer.size());
        for (u32 i = 0; i < _desc._index_buffer.size(); ++i)
        {
            auto *ib = dynamic_cast<D3DIndexBuffer *>(_desc._index_buffer[i]);
            if (vb == nullptr || ib == nullptr)
                continue;
            D3D12_RAYTRACING_GEOMETRY_DESC geometry_desc = {};
            geometry_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            geometry_desc.Triangles.IndexBuffer = ib->NativeResource().As<ID3D12Resource>()->GetGPUVirtualAddress();
            geometry_desc.Triangles.IndexCount = ib->GetCount();
            geometry_desc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
            geometry_desc.Triangles.Transform3x4 = 0;
            geometry_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            geometry_desc.Triangles.VertexCount = _desc._vertex_count;
            geometry_desc.Triangles.VertexBuffer.StartAddress = vb->NativeResource(0u).As<ID3D12Resource>()->GetGPUVirtualAddress();
            geometry_desc.Triangles.VertexBuffer.StrideInBytes = vb->GetLayout().GetStride(0);
            geometry_desc.Flags = _desc._opaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
            _geometry_descs.push_back(geometry_desc);
        }

        AL_ASSERT_MSG(!_geometry_descs.empty(), "RayTracingGeometry({}) has no valid submesh index buffer for BLAS build", Name());

        _inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        _inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        _inputs.NumDescs = static_cast<UINT>(_geometry_descs.size());
        _inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        _inputs.pGeometryDescs = _geometry_descs.data();


        auto* dev = dynamic_cast<D3DContext *>(ctx)->GetDevice();
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo = {};
        dev->GetRaytracingAccelerationStructurePrebuildInfo(&_inputs, &bottomLevelPrebuildInfo);
        AL_ASSERT(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);
        _scratch_buffer_size = static_cast<u32>(bottomLevelPrebuildInfo.ScratchDataSizeInBytes);
        CreateBufferResource(dynamic_cast<D3DContext *>(ctx),
                             bottomLevelPrebuildInfo.ScratchDataSizeInBytes,
                             true,
                             std::format("blas_{}_scratch", _desc._vertex_buffer->Name()),
                             _scratch_resource);
        _mem_size += static_cast<u32>(bottomLevelPrebuildInfo.ScratchDataSizeInBytes);

        CreateBufferResource(dynamic_cast<D3DContext *>(ctx),
                             bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes,
                             true,
                             std::format("blas_{}", _desc._vertex_buffer->Name()),
                             _blas_resource);
        _blas_gpu_address = _blas_resource->GetGPUVirtualAddress();
        _native_resource = {Render::RendererAPI::ERenderAPI::kDirectX12, _blas_resource.Get()};
        _mem_size += static_cast<u32>(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes);
        //调用基类的UploadImpl以便正确统计内存使用量等
        GpuResource::UploadImpl(ctx, rhi_cmd, params);
    }
}// namespace Ailu::RHI::DX12
