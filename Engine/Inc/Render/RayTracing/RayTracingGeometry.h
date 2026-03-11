#pragma once
#ifndef __RAY_TRACING_GEOMETRY_H__
#define __RAY_TRACING_GEOMETRY_H__
#include "../GpuResource.h"

namespace Ailu::Render
{
    class VertexBuffer;
    class IndexBuffer;
    class GPUBuffer;
    class Mesh;

    struct RayTracingGeometryDesc
    {
        VertexBuffer* _vertex_buffer;
        IndexBuffer* _index_buffer;
        u32 _vertex_stride;
        u32 _vertex_count;
        u32 _index_count;
        bool _opaque;
        RayTracingGeometryDesc() = default;
        RayTracingGeometryDesc(Mesh* mesh,u16 sub_mesh, bool opaque);
    };

    class RayTracingGeometry : public GpuResource
    {
    public:
        static Ref<RayTracingGeometry> Create(const RayTracingGeometryDesc& desc);
        RayTracingGeometry() = default;
        RayTracingGeometry(const RayTracingGeometryDesc& desc);
        ~RayTracingGeometry();
        NativeHandle NativeResource() final;
        const RayTracingGeometryDesc& GetDesc() const { return _desc; }
        GPUBuffer* GetBLAS() const { return _blas_buffer.get(); }
        u32 GetScratchBufferSize() const { return _scratch_buffer_size; }
        void Build();
        void Update();
    protected:
        RayTracingGeometryDesc _desc;
        Ref<GPUBuffer> _blas_buffer;
        u32 _scratch_buffer_size = 0u;
    };
}
#endif// !__RAY_TRACING_GEOMETRY_H__