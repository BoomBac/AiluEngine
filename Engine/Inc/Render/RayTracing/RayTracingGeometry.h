#pragma once
#ifndef __RAY_TRACING_GEOMETRY_H__
#define __RAY_TRACING_GEOMETRY_H__
#include "../GpuResource.h"

namespace Ailu::Render
{
    class VertexBuffer;
    class IndexBuffer;
    class Mesh;

    struct RayTracingGeometryDesc
    {
        VertexBuffer* _vertex_buffer;
        Vector<IndexBuffer*> _index_buffer;
        u32 _vertex_stride;
        u32 _vertex_count;
        bool _opaque;
        RayTracingGeometryDesc() = default;
        RayTracingGeometryDesc(Mesh* mesh, bool opaque);
    };

    class RayTracingGeometry : public GpuResource
    {
    public:
        static Ref<RayTracingGeometry> Create(const RayTracingGeometryDesc& desc,const String& name);
        RayTracingGeometry() = default;
        RayTracingGeometry(const RayTracingGeometryDesc& desc);
        ~RayTracingGeometry();
        NativeHandle NativeResource() final;
        const RayTracingGeometryDesc& GetDesc() const { return _desc; }
        u32 GetScratchBufferSize() const { return _scratch_buffer_size; }
        void Build();
        void Update();
    protected:
        RayTracingGeometryDesc _desc;
        NativeHandle _native_resource;
        u32 _scratch_buffer_size = 0u;
    };
}
#endif// !__RAY_TRACING_GEOMETRY_H__