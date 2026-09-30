#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Render/GpuResource.h"
#include <unordered_map>

namespace Ailu::Render
{
    class Material;
    class VertexBuffer;
    class IndexBuffer;

    struct DrawSortKey
    {
        u64 _high = 0u;
        u64 _low = 0u;

        bool operator==(const DrawSortKey &rhs) const { return _high == rhs._high && _low == rhs._low; }
        bool operator<(const DrawSortKey &rhs) const
        {
            return _high != rhs._high ? _high < rhs._high : _low < rhs._low;
        }
    };

    enum class EDrawSortPolicy : u8
    {
        kOpaque,
        kShadow,
        kTransparent,
        kDepthOnly
    };

    enum EPreparedDrawFlags : u16
    {
        kPreparedDrawNone = 0u,
        kPreparedDrawForceAlphaTest = 1u << 0u,
        kPreparedDrawPreserveOrder = 1u << 1u,
        kPreparedDrawAllowInstancing = 1u << 2u
    };

    using GraphicsPipelineStateId = u32;
    using MaterialStateId = u32;
    using DrawGeometryId = u32;
    using DrawStateBucketId = u32;

    struct PreparedDrawCommand
    {
        VertexBuffer *_vertex_buffer = nullptr;
        IndexBuffer *_index_buffer = nullptr;
        Material *_material = nullptr;
        GraphicsPipelineStateId _pipeline_state_id = 0u;
        MaterialStateId _material_state_id = 0u;
        DrawGeometryId _geometry_id = 0u;
        DrawStateBucketId _state_bucket_id = 0u;
        u64 _variant_hash = 0u;
        u16 _submesh_index = 0u;
        u16 _pass_index = 0u;
        u16 _stencil_ref = 0u;
        u16 _flags = kPreparedDrawNone;
    };

    struct VisibleDrawItem
    {
        const PreparedDrawCommand *_command = nullptr;
        DrawSortKey _sort_key{};
        u32 _state_bucket_id = 0u;
        u32 _primitive_index = 0u;
        u32 _submission_index = 0u;
    };

    static_assert(sizeof(VisibleDrawItem) <= 40u);

    struct DrawSortInput
    {
        u32 _render_queue = 0u;
        u16 _depth_bucket = 0u;
        u32 _pipeline_state_id = 0u;
        u32 _material_state_id = 0u;
        u32 _state_bucket_id = 0u;
        u32 _submission_index = 0u;
    };

    u16 QuantizeOpaqueDepth(f32 view_depth, f32 near_plane, f32 far_plane);
    u16 QuantizeTransparentDepth(f32 view_depth, f32 near_plane, f32 far_plane);
    DrawSortKey BuildDrawSortKey(EDrawSortPolicy policy, const DrawSortInput &input);
    bool CanMergeInstances(const VisibleDrawItem &lhs, const VisibleDrawItem &rhs);

    struct DrawPipelineStateKey
    {
        u64 _shader_id = 0u;
        u64 _variant_hash = 0u;
        u16 _pass_index = 0u;
        u8 _blend_hash = 0u;
        u8 _raster_hash = 0u;
        u8 _depth_hash = 0u;
        u8 _topology = 0u;
        bool operator==(const DrawPipelineStateKey &) const = default;
    };

    struct MaterialStateKey
    {
        u64 _material_id = 0u;
        u32 _draw_state_version = 0u;
        u64 _variant_hash = 0u;
        u16 _pass_index = 0u;
        bool operator==(const MaterialStateKey &) const = default;
    };

    struct DrawGeometryKey
    {
        BufferHandle _vertex_buffer{};
        BufferHandle _index_buffer{};
        u32 _vertex_buffer_id = 0u;
        u32 _index_buffer_id = 0u;
        u32 _index_start = 0u;
        u32 _index_count = 0u;
        u16 _submesh_index = 0u;
        bool operator==(const DrawGeometryKey &) const = default;
    };

    struct DrawStateBucketKey
    {
        u32 _pipeline_state_id = 0u;
        u32 _material_state_id = 0u;
        u32 _geometry_id = 0u;
        u32 _render_queue = 0u;
        u16 _stencil_ref = 0u;
        u16 _flags = 0u;
        bool operator==(const DrawStateBucketKey &) const = default;
    };

}

namespace std
{
    template<> struct hash<Ailu::Render::DrawPipelineStateKey>
    {
        size_t operator()(const Ailu::Render::DrawPipelineStateKey &key) const noexcept;
    };
    template<> struct hash<Ailu::Render::MaterialStateKey>
    {
        size_t operator()(const Ailu::Render::MaterialStateKey &key) const noexcept;
    };
    template<> struct hash<Ailu::Render::DrawGeometryKey>
    {
        size_t operator()(const Ailu::Render::DrawGeometryKey &key) const noexcept;
    };
    template<> struct hash<Ailu::Render::DrawStateBucketKey>
    {
        size_t operator()(const Ailu::Render::DrawStateBucketKey &key) const noexcept;
    };
}

namespace Ailu::Render
{
    class DrawSubmissionRegistry
    {
    public:
        static DrawSubmissionRegistry &Get();
        u32 InternPipeline(const DrawPipelineStateKey &key);
        u32 InternMaterial(const MaterialStateKey &key);
        u32 InternGeometry(const DrawGeometryKey &key);
        u32 InternBucket(const DrawStateBucketKey &key);

    private:
        std::unordered_map<DrawPipelineStateKey, u32> _pipelines;
        std::unordered_map<MaterialStateKey, u32> _materials;
        std::unordered_map<DrawGeometryKey, u32> _geometries;
        std::unordered_map<DrawStateBucketKey, u32> _buckets;
    };
}
