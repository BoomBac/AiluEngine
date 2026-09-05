#pragma once
#ifndef __SKINNING_SYSTEM_H__
#define __SKINNING_SYSTEM_H__

#include "Framework/Common/JobSystem.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/SmartPtr.h"
#include "Framework/Math/ALMath.hpp"
#include "Render/RenderGraph/RenderGraphFwd.h"
#include "Render/Shader.h"
#include "Render/RenderingData.h"

#include <span>
#include <unordered_map>

namespace Ailu::Render
{
    class GPUBuffer;
    class Mesh;
    class SkeletonMesh;
    class VertexBuffer;
}

namespace Ailu::ECS
{
    struct PoseHandle
    {
        u32 _id = 0u;

        [[nodiscard]] bool IsValid() const noexcept { return _id != 0u; }
        friend bool operator==(PoseHandle lhs, PoseHandle rhs) noexcept { return lhs._id == rhs._id; }
    };

    struct SkinHandle
    {
        u32 _id = 0u;

        [[nodiscard]] bool IsValid() const noexcept { return _id != 0u; }
        friend bool operator==(SkinHandle lhs, SkinHandle rhs) noexcept { return lhs._id == rhs._id; }
    };

    class AILU_API SkinningSystem
    {
    public:
        static constexpr u32 kDefaultVerticesPerTask = 2000u;
        static constexpr u32 kThreadsPerGroup = 64u;
        static constexpr u32 kMaxSkinningJobs = 4096u;
        static constexpr u32 kMaxPaletteBones = 65536u;
        static constexpr u64 kCacheKeepFrames = 60u;

        bool BeginFrame();
        PoseHandle CreatePoseHandle();
        void UploadPose(PoseHandle handle, std::span<const Matrix4x4f> pose_palette);
        void SetEntityPose(u32 entity, PoseHandle pose_handle);
        SkinHandle Acquire(Render::SkeletonMesh *mesh, PoseHandle pose_handle, u32 entity);
        void PrepareVisibleSkinning(const Render::CullResult &cull_results);
        void RecordRenderGraph(Render::RDG::RenderGraph &graph, Render::RenderingData &data);

        [[nodiscard]] const Vector<Render::RDG::RGHandle> &GetRenderGraphOutputHandles() const noexcept
        {
            return _output_handles;
        }

        [[nodiscard]] bool IsGpuSkinningEnabled() const noexcept { return _gpu_enabled; }
        [[nodiscard]] Render::VertexBuffer *GetVertexBuffer(SkinHandle handle) const;
        static Render::VertexBuffer *ResolveVertexBuffer(Render::Mesh *mesh, u32 entity);

        // CPU fallback and animation preview path.
        void Submit(Render::SkeletonMesh *mesh, const Vector<Matrix4x4f> &pose_palette,
                    u32 vertices_per_task = kDefaultVerticesPerTask);
        void WaitFor() const;
        void Clear();

    private:
        struct SkinCacheKey
        {
            u32 _mesh_id = 0u;
            u32 _pose_id = 0u;

            friend bool operator==(const SkinCacheKey &lhs, const SkinCacheKey &rhs) noexcept
            {
                return lhs._mesh_id == rhs._mesh_id && lhs._pose_id == rhs._pose_id;
            }
        };

        struct SkinCacheKeyHash
        {
            size_t operator()(const SkinCacheKey &key) const noexcept
            {
                return (static_cast<size_t>(key._mesh_id) << 32u) ^ key._pose_id;
            }
        };

        struct SkinningJobData
        {
            u32 _position_srv = 0u;
            u32 _normal_srv = 0u;
            u32 _tangent_srv = 0u;
            u32 _bone_index_srv = 0u;
            u32 _bone_weight_srv = 0u;
            u32 _position_uav = 0u;
            u32 _normal_uav = 0u;
            u32 _tangent_uav = 0u;
            u32 _palette_offset = 0u;
            u32 _vertex_count = 0u;
            u32 _vertex_offset = 0u;
        };

        struct SkinCacheEntry
        {
            Render::Mesh *_mesh = nullptr;
            Ref<Render::GPUBuffer> _position_buffer;
            Ref<Render::GPUBuffer> _normal_buffer;
            Ref<Render::GPUBuffer> _tangent_buffer;
            Ref<Render::VertexBuffer> _vertex_buffer;
            SkinHandle _handle;
            u64 _last_used_frame = 0u;
            u32 _vertex_count = 0u;
        };

        bool EnsureComputeShader();
        void EnsureStructuredBuffers();
        SkinCacheEntry *FindEntry(SkinHandle handle);
        const SkinCacheEntry *FindEntry(SkinHandle handle) const;
        SkinCacheEntry *CreateEntry(Render::SkeletonMesh *mesh, PoseHandle pose_handle, SkinCacheKey key);
        static Ref<Render::GPUBuffer> CreateOutputBuffer(u32 vertex_count, u32 element_size, const String &name);

        Vector<Ref<WaitHandle>> _tasks;
        std::unordered_map<SkinCacheKey, SkinCacheEntry, SkinCacheKeyHash> _cache;
        std::unordered_map<u32, SkinCacheKey> _handles;
        std::unordered_map<u32, u32> _pose_offsets;
        std::unordered_map<u32, PoseHandle> _entity_poses;
        std::unordered_map<u32, SkinHandle> _entity_handles;
        Vector<SkinningJobData> _jobs;
        Vector<SkinHandle> _job_handles;
        Vector<Matrix4x4f> _palette_data;
        Vector<Render::RDG::RGHandle> _output_handles;
        Ref<Render::GPUBuffer> _palette_buffer;
        Ref<Render::GPUBuffer> _job_buffer;
        Ref<Render::ComputeShader> _compute_shader;
        u16 _kernel = Render::kInvalidComputeShaderKernelId;
        u64 _frame_index = 0u;
        u32 _next_pose_id = 1u;
        u32 _next_skin_id = 1u;
        bool _gpu_enabled = false;
        bool _recorded = false;

        inline static SkinningSystem *s_active_system = nullptr;
    };
}

#endif // __SKINNING_SYSTEM_H__
