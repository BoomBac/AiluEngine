#pragma once
#ifndef __RAY_TRACING_GEOMETRY_H__
#define __RAY_TRACING_GEOMETRY_H__
#include "../GpuResource.h"

namespace Ailu::Render
{
    class CommandBuffer;
    class VertexBuffer;
    class IndexBuffer;
    class Mesh;

    enum class ERayTracingGeometryBuildMode : u8
    {
        kStatic,
        kDynamic
    };

    struct RayTracingGeometryDesc
    {
        VertexBuffer *_vertex_buffer = nullptr;
        Vector<IndexBuffer*> _index_buffer;
        u32 _vertex_stride = 0u;
        u32 _vertex_count = 0u;
        bool _opaque = false;
        ERayTracingGeometryBuildMode _build_mode = ERayTracingGeometryBuildMode::kStatic;
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
        bool IsDynamic() const { return _desc._build_mode == ERayTracingGeometryBuildMode::kDynamic; }
        bool NeedsBuild() const { return !_has_been_built; }
        void Build();
        void Build(CommandBuffer *cmd);
        void Update(CommandBuffer *cmd);
        IndexBuffer* GetIndexBuffer(u32 gem_index = 0) const { return gem_index < _index_buffers.size() ? _index_buffers[gem_index] : nullptr; }
    protected:
        RayTracingGeometryDesc _desc;
        NativeHandle _native_resource;
        u32 _scratch_buffer_size = 0u;
        Vector<IndexBuffer*> _index_buffers;
        bool _has_been_built = false;
    };
}
#endif// !__RAY_TRACING_GEOMETRY_H__
