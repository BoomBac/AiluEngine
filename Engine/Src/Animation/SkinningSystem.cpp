#include "Animation/SkinningSystem.h"

#include "Framework/Common/ThreadPool.h"
#include "Render/Mesh.h"

#include <algorithm>

namespace Ailu::ECS
{
    namespace
    {
        void SkinTask(Render::SkeletonMesh *mesh, const Vector<Matrix4x4f> &pose_palette, Vector3f *vertices,
                      u32 begin, u32 end)
        {
            const auto bone_weights = mesh->GetBoneWeights();
            const auto bone_indices = mesh->GetBoneIndices();
            const auto source_vertices = mesh->GetVertices();
            for (u32 index = begin; index < end; ++index)
            {
                Vector3f skinned_position;
                const auto &weights = bone_weights[index];
                const auto &indices = bone_indices[index];
                for (u16 influence = 0u; influence < 4u; ++influence)
                {
                    const u16 joint_index = indices[influence];
                    skinned_position += weights[influence] * TransformCoord(pose_palette[joint_index], source_vertices[index]);
                }
                vertices[index] = skinned_position;
            }
        }
    }

    void SkinningSystem::Submit(Render::SkeletonMesh *mesh, const Vector<Matrix4x4f> &pose_palette,
                                u32 vertices_per_task)
    {
        if (mesh == nullptr || pose_palette.empty() || vertices_per_task == 0u)
            return;

        Vector3f *vertices = reinterpret_cast<Vector3f *>(mesh->GetVertexBuffer()->GetStream(0));
        const u32 vertex_count = mesh->GetVertexCount();
        const u32 task_count = (vertex_count + vertices_per_task - 1u) / vertices_per_task;
        _tasks.reserve(_tasks.size() + task_count);
        for (u32 task_index = 0u; task_index < task_count; ++task_index)
        {
            const u32 begin = task_index * vertices_per_task;
            const u32 end = std::min(begin + vertices_per_task, vertex_count);
            auto task = Core::ThreadPool::Get().Enqueue("SkinningSystem::Skin", SkinTask, mesh, pose_palette, vertices, begin, end);
            _tasks.emplace_back(MakeRef<std::future<void>>(std::move(task)));
        }
    }

    void SkinningSystem::WaitFor() const
    {
        for (const auto &task : _tasks)
            task->wait();
    }

    void SkinningSystem::Clear()
    {
        _tasks.clear();
    }
}
