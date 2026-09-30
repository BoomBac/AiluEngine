#include "Render/DrawSubmission.h"
#include <algorithm>
#include <cmath>

namespace Ailu::Render
{
    namespace
    {
        size_t CombineHash(size_t seed, u64 value)
        {
            const size_t hashed = std::hash<u64>{}(value);
            return seed ^ (hashed + static_cast<size_t>(0x9e3779b9u) + (seed << 6u) + (seed >> 2u));
        }

        template<typename key_t>
        u32 InternKey(std::unordered_map<key_t, u32> &registry, const key_t &key)
        {
            const auto found = registry.find(key);
            if (found != registry.end())
                return found->second;
            const u32 id = static_cast<u32>(registry.size() + 1u);
            registry.emplace(key, id);
            return id;
        }
    }

    u16 QuantizeOpaqueDepth(f32 view_depth, f32 near_plane, f32 far_plane)
    {
        const f32 safe_near = std::max(near_plane, 0.0f);
        const f32 safe_far = std::max(far_plane, safe_near + 0.001f);
        const f32 normalized = std::clamp((view_depth - safe_near) / (safe_far - safe_near), 0.0f, 1.0f);
        return static_cast<u16>(normalized * 4095.0f + 0.5f);
    }

    u16 QuantizeTransparentDepth(f32 view_depth, f32 near_plane, f32 far_plane)
    {
        const f32 safe_near = std::max(near_plane, 0.0f);
        const f32 safe_far = std::max(far_plane, safe_near + 0.001f);
        const f32 normalized = std::clamp((view_depth - safe_near) / (safe_far - safe_near), 0.0f, 1.0f);
        return static_cast<u16>(normalized * 65535.0f + 0.5f);
    }

    DrawSortKey BuildDrawSortKey(EDrawSortPolicy policy, const DrawSortInput &input)
    {
        DrawSortKey key{};
        switch (policy)
        {
        case EDrawSortPolicy::kShadow:
            key._high = (static_cast<u64>(input._pipeline_state_id) << 32u) | input._material_state_id;
            key._low = (static_cast<u64>(input._state_bucket_id) << 32u) | input._depth_bucket;
            break;
        case EDrawSortPolicy::kTransparent:
            key._high = (static_cast<u64>(input._render_queue) << 32u) | (0xffffu - input._depth_bucket);
            key._low = input._submission_index;
            break;
        case EDrawSortPolicy::kDepthOnly:
            key._high = (static_cast<u64>(input._render_queue) << 48u)
                | (static_cast<u64>(input._depth_bucket) << 32u) | input._pipeline_state_id;
            key._low = (static_cast<u64>(input._state_bucket_id) << 32u) | input._submission_index;
            break;
        case EDrawSortPolicy::kOpaque:
        default:
            key._high = (static_cast<u64>(input._render_queue & 0xffffu) << 48u)
                | (static_cast<u64>(input._depth_bucket) << 32u) | input._pipeline_state_id;
            key._low = (static_cast<u64>(input._material_state_id) << 32u) | input._state_bucket_id;
            break;
        }
        return key;
    }

    bool CanMergeInstances(const VisibleDrawItem &lhs, const VisibleDrawItem &rhs)
    {
        if (lhs._state_bucket_id == 0u || lhs._state_bucket_id != rhs._state_bucket_id)
            return false;
        const PreparedDrawCommand *command = lhs._command;
        return command != nullptr && rhs._command != nullptr
            && (command->_flags & kPreparedDrawAllowInstancing) != 0u
            && (command->_flags & kPreparedDrawPreserveOrder) == 0u;
    }

    DrawSubmissionRegistry &DrawSubmissionRegistry::Get()
    {
        static DrawSubmissionRegistry s_registry;
        return s_registry;
    }

    u32 DrawSubmissionRegistry::InternPipeline(const DrawPipelineStateKey &key) { return InternKey(_pipelines, key); }
    u32 DrawSubmissionRegistry::InternMaterial(const MaterialStateKey &key) { return InternKey(_materials, key); }
    u32 DrawSubmissionRegistry::InternGeometry(const DrawGeometryKey &key) { return InternKey(_geometries, key); }
    u32 DrawSubmissionRegistry::InternBucket(const DrawStateBucketKey &key) { return InternKey(_buckets, key); }
}

size_t std::hash<Ailu::Render::DrawPipelineStateKey>::operator()(const Ailu::Render::DrawPipelineStateKey &key) const noexcept
{
    size_t seed = 0u;
    seed = Ailu::Render::CombineHash(seed, key._shader_id);
    seed = Ailu::Render::CombineHash(seed, key._variant_hash);
    seed = Ailu::Render::CombineHash(seed, key._pass_index);
    seed = Ailu::Render::CombineHash(seed, key._blend_hash);
    seed = Ailu::Render::CombineHash(seed, key._raster_hash);
    seed = Ailu::Render::CombineHash(seed, key._depth_hash);
    return Ailu::Render::CombineHash(seed, key._topology);
}

size_t std::hash<Ailu::Render::MaterialStateKey>::operator()(const Ailu::Render::MaterialStateKey &key) const noexcept
{
    size_t seed = 0u;
    seed = Ailu::Render::CombineHash(seed, key._material_id);
    seed = Ailu::Render::CombineHash(seed, key._draw_state_version);
    seed = Ailu::Render::CombineHash(seed, key._variant_hash);
    return Ailu::Render::CombineHash(seed, key._pass_index);
}

size_t std::hash<Ailu::Render::DrawGeometryKey>::operator()(const Ailu::Render::DrawGeometryKey &key) const noexcept
{
    size_t seed = 0u;
    seed = Ailu::Render::CombineHash(seed, key._vertex_buffer._index);
    seed = Ailu::Render::CombineHash(seed, key._vertex_buffer._generation);
    seed = Ailu::Render::CombineHash(seed, key._index_buffer._index);
    seed = Ailu::Render::CombineHash(seed, key._index_buffer._generation);
    seed = Ailu::Render::CombineHash(seed, key._vertex_buffer_id);
    seed = Ailu::Render::CombineHash(seed, key._index_buffer_id);
    seed = Ailu::Render::CombineHash(seed, key._index_start);
    seed = Ailu::Render::CombineHash(seed, key._index_count);
    return Ailu::Render::CombineHash(seed, key._submesh_index);
}

size_t std::hash<Ailu::Render::DrawStateBucketKey>::operator()(const Ailu::Render::DrawStateBucketKey &key) const noexcept
{
    size_t seed = 0u;
    seed = Ailu::Render::CombineHash(seed, key._pipeline_state_id);
    seed = Ailu::Render::CombineHash(seed, key._material_state_id);
    seed = Ailu::Render::CombineHash(seed, key._geometry_id);
    seed = Ailu::Render::CombineHash(seed, key._render_queue);
    seed = Ailu::Render::CombineHash(seed, key._stencil_ref);
    return Ailu::Render::CombineHash(seed, key._flags);
}
