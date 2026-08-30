#include "Animation/SkinningSystem.h"
#include "Framework/Common/Profiler.h"
#include "Render/Mesh.h"

#include <algorithm>

namespace Ailu::ECS
{
    namespace
    {
        void SkinTask(Render::SkeletonMesh *mesh, const Vector<Matrix4x4f> &pose_palette, Vector3f *vertices,
                      Vector3f *normals, Vector4f *tangents, u32 begin, u32 end)
        {
            PROFILE_BLOCK_CPU("SkinTask")
            const auto bone_weights = mesh->GetBoneWeights();
            const auto bone_indices = mesh->GetBoneIndices();
            const auto source_vertices = mesh->GetPreviousVertices();
            const auto source_normals = mesh->GetNormals();
            const auto source_tangents = mesh->GetTangents();
            for (u32 index = begin; index < end; ++index)
            {
                if (index >= bone_weights.size() || index >= bone_indices.size() || index >= source_vertices.size())
                    continue;

                Vector3f skinned_position = Vector3f::kZero;
                Vector3f skinned_normal = Vector3f::kZero;
                Vector3f skinned_tangent = Vector3f::kZero;
                f32 valid_weight = 0.0f;
                const auto &weights = bone_weights[index];
                const auto &indices = bone_indices[index];
                for (u16 influence = 0u; influence < 4u; ++influence)
                {
                    if (weights[influence] <= 0.0f)
                        continue;
                    const u16 joint_index = indices[influence];
                    if (joint_index >= pose_palette.size())
                        continue;
                    skinned_position +=
                        weights[influence] * TransformCoord(pose_palette[joint_index], source_vertices[index]);
                    if (normals != nullptr && index < source_normals.size())
                        skinned_normal += weights[influence] *
                                          TransformNormal(pose_palette[joint_index], source_normals[index]);
                    if (tangents != nullptr && index < source_tangents.size())
                        skinned_tangent += weights[influence] *
                                           TransformNormal(pose_palette[joint_index], source_tangents[index].xyz);
                    valid_weight += weights[influence];
                }
                vertices[index] = valid_weight > Math::kFloatEpsilon ? skinned_position : source_vertices[index];
                if (normals != nullptr && index < source_normals.size())
                    normals[index] = valid_weight > Math::kFloatEpsilon ? Normalize(skinned_normal) :
                                                                            source_normals[index];
                if (tangents != nullptr && index < source_tangents.size())
                {
                    Vector4f tangent = source_tangents[index];
                    if (valid_weight > Math::kFloatEpsilon)
                    {
                        const Vector3f normalized_tangent = Normalize(skinned_tangent);
                        tangent.x = normalized_tangent.x;
                        tangent.y = normalized_tangent.y;
                        tangent.z = normalized_tangent.z;
                    }
                    tangents[index] = tangent;
                }
            }
        }
    }

    void SkinningSystem::Submit(Render::SkeletonMesh *mesh, const Vector<Matrix4x4f> &pose_palette,
                                u32 vertices_per_task)
    {
        if (mesh == nullptr || pose_palette.empty() || vertices_per_task == 0u)
            return;

        if (mesh->GetVertexBuffer() == nullptr)
            return;

        Vector3f *vertices = reinterpret_cast<Vector3f *>(mesh->GetVertexBuffer()->GetStream(0));
        Vector3f *normals = nullptr;
        Vector4f *tangents = nullptr;
        const i32 normal_stream = mesh->GetNormalStream();
        const i32 tangent_stream = mesh->GetTangentStream();
        if (normal_stream >= 0)
            normals = reinterpret_cast<Vector3f *>(
                mesh->GetVertexBuffer()->GetStream(static_cast<u8>(normal_stream)));
        if (tangent_stream >= 0)
            tangents = reinterpret_cast<Vector4f *>(
                mesh->GetVertexBuffer()->GetStream(static_cast<u8>(tangent_stream)));
        const u32 vertex_count = mesh->GetVertexCount();
        const u32 task_count = (vertex_count + vertices_per_task - 1u) / vertices_per_task;
        _tasks.reserve(_tasks.size() + task_count);
        for (u32 task_index = 0u; task_index < task_count; ++task_index)
        {
            const u32 begin = task_index * vertices_per_task;
            const u32 end = std::min(begin + vertices_per_task, vertex_count);
            auto wait_handle = JobSystem::Get().Dispatch(
                [mesh, pose_palette, vertices, normals, tangents, begin, end]()
                { SkinTask(mesh, pose_palette, vertices, normals, tangents, begin, end); });
            _tasks.emplace_back(MakeRef<WaitHandle>(std::move(wait_handle)));
        }
    }

    void SkinningSystem::WaitFor() const
    {
        for (const auto &task : _tasks)
            JobSystem::Get().Wait(*task);
    }

    void SkinningSystem::Clear()
    {
        WaitFor();
        _tasks.clear();
    }
}
