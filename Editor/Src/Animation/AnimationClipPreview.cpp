#include "Animation/AnimationClipPreview.h"

#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/ResourcePool.h"
#include "Render/Texture.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace Ailu::Editor
{
    namespace
    {
        constexpr f32 kSkeletonJointRadius = 0.025f;

        Matrix4x4f MakeAxisOrientation(const Vector3f &direction)
        {
            const Vector3f dir = Normalize(direction);
            const Vector3f base = Vector3f::kUp;
            const f32 dot = DotProduct(base, dir);
            if (dot > 1.0f - 1e-6f)
                return BuildIdentityMatrix();
            if (dot < -1.0f + 1e-6f)
                return MatrixRotationX(Math::kPi);

            const Vector3f axis = Normalize(CrossProduct(base, dir));
            Matrix4x4f orientation;
            MatrixRotationAxis(orientation, axis, std::acos(std::clamp(dot, -1.0f, 1.0f)));
            return orientation;
        }

        bool GetMeshBounds(Render::Mesh *mesh, Vector3f &center, Vector3f &size)
        {
            if (mesh == nullptr || mesh->BoundBox().empty())
                return false;
            const AABB &bounds = mesh->BoundBox()[0];
            center = (bounds._min + bounds._max) * 0.5f;
            size = bounds._max - bounds._min;
            size.x = std::max(size.x, 0.001f);
            size.y = std::max(size.y, 0.001f);
            size.z = std::max(size.z, 0.001f);
            return true;
        }

        bool IsDrawableMesh(Render::Mesh *mesh)
        {
            return mesh != nullptr && mesh->GetVertexBuffer() != nullptr && mesh->SubmeshCount() > 0u &&
                   !mesh->GetIndices(0u).empty();
        }

        Matrix4x4f MakeJointMatrix(Render::Mesh *mesh, const Vector3f &position, f32 radius,
                                   const Matrix4x4f &world_matrix)
        {
            Vector3f center;
            Vector3f size;
            if (!GetMeshBounds(mesh, center, size))
                return world_matrix;
            const Vector3f scale{radius * 2.0f / size.x, radius * 2.0f / size.y, radius * 2.0f / size.z};
            const Vector3f offset{-center.x * scale.x, -center.y * scale.y, -center.z * scale.z};
            return MatrixScale(scale) * MatrixTranslation(offset) * MatrixTranslation(position) * world_matrix;
        }

        Matrix4x4f MakeBoneMatrix(Render::Mesh *mesh, const Vector3f &origin, const Vector3f &direction,
                                  f32 radius, const Matrix4x4f &world_matrix)
        {
            Vector3f center;
            Vector3f size;
            if (!GetMeshBounds(mesh, center, size))
                return world_matrix;
            const AABB &bounds = mesh->BoundBox()[0];
            const f32 length = Magnitude(direction);
            const Vector3f scale{radius * 2.0f / size.x, length / size.y, radius * 2.0f / size.z};
            const Vector3f offset{-center.x * scale.x, -bounds._min.y * scale.y, -center.z * scale.z};
            return MatrixScale(scale) * MatrixTranslation(offset) * MakeAxisOrientation(direction) *
                   MatrixTranslation(origin) * world_matrix;
        }

        f32 DistanceSquaredToScreenSegment(const Vector2f &point, const Vector2f &segment_start,
                                           const Vector2f &segment_end, f32 &segment_factor)
        {
            const Vector2f segment = segment_end - segment_start;
            const f32 segment_length_squared = DotProduct(segment, segment);
            segment_factor = 0.0f;
            if (segment_length_squared > Math::kFloatEpsilon)
                segment_factor = std::clamp(DotProduct(point - segment_start, segment) /
                                                segment_length_squared,
                                            0.0f, 1.0f);
            const Vector2f difference = point - (segment_start + segment * segment_factor);
            return DotProduct(difference, difference);
        }

        bool HasTransformTrack(const AnimationClip *clip, u16 joint_index)
        {
            if (clip == nullptr)
                return false;
            for (u32 track_index = 0u; track_index < clip->Size(); ++track_index)
            {
                if (clip->GetIdAtIndex(track_index) != joint_index)
                    continue;
                const TransformTrack &track = clip->GetTrackAtIndex(track_index);
                return track.GetPositionTrack().Size() > 0u || track.GetRotationTrack().Size() > 0u ||
                       track.GetScaleTrack().Size() > 0u;
            }
            return false;
        }
    }

    AnimationClipPreview::AnimationClipPreview() : _skinning_system(MakeScope<ECS::SkinningSystem>())
    {
        _viewport.SetOverlayCallback([this](Render::CommandBuffer *command_buffer, const Matrix4x4f &world_matrix,
                                             const Vector3f &center, const Vector3f &extents)
        {
            DrawSkeletonOverlay(command_buffer, world_matrix, center, extents);
        });
    }

    AnimationClipPreview::~AnimationClipPreview()
    {
        if (_skinning_system != nullptr)
            _skinning_system->Clear();
    }

    void AnimationClipPreview::SetClip(AnimationClip *clip)
    {
        if (_clip == clip && _blend_space == nullptr)
            return;
        _clip = clip;
        _blend_space = nullptr;
        _blend_space_clips.clear();
        _blend_space_duration = 0.0f;
        _binding.Clear();
        if (HasResolvedSkeleton())
            _pose = _preview_mesh->GetSkeletonAsset()->GetSkeleton().GetBindPose();
        else
            _pose = SkeletonPose();
        _current_time = 0.0f;
        if (_clip != nullptr && HasResolvedSkeleton())
            _binding.Resolve(*_clip, _preview_mesh->GetSkeletonAsset()->GetSkeleton());
    }

    void AnimationClipPreview::SetBlendSpace(BlendSpaceAsset *blend_space, Vector2f input)
    {
        _clip = nullptr;
        _blend_space = blend_space;
        _blend_space_input = input;
        _blend_space_clips.clear();
        _blend_space_duration = 0.0f;
        _binding.Clear();
        if (HasResolvedSkeleton())
        {
            _pose = _preview_mesh->GetSkeletonAsset()->GetSkeleton().GetBindPose();
            ResolveBlendSpaceBindings();
        }
        else
        {
            _pose = SkeletonPose();
        }
        _current_time = 0.0f;
        EvaluatePose();
        UpdateSkinning();
        RenderPreview();
    }

    void AnimationClipPreview::SetBlendSpaceInput(Vector2f input, bool render)
    {
        if (_blend_space == nullptr || _blend_space_input == input)
            return;
        _blend_space_input = input;
        if (!render)
            return;
        EvaluatePose();
        UpdateSkinning();
        RenderPreview();
    }

    void AnimationClipPreview::SetMesh(Render::SkeletonMesh *mesh)
    {
        if (_source_mesh == mesh)
            return;
        _source_mesh = mesh;
        RebuildPreviewMesh();
    }

    bool AnimationClipPreview::HasResolvedSkeleton() const
    {
        return _preview_mesh != nullptr && _preview_mesh->GetSkeletonAsset().IsResolved();
    }

    bool AnimationClipPreview::SetViewportSize(Vector2f size)
    {
        return _viewport.SetViewportSize(size);
    }

    void AnimationClipPreview::BeginCameraDrag(Vector2f local_position)
    {
        _viewport.BeginCameraDrag(local_position);
    }

    void AnimationClipPreview::EndCameraDrag()
    {
        _viewport.EndCameraDrag();
    }

    void AnimationClipPreview::DragCamera(Vector2f local_position)
    {
        _viewport.DragCamera(local_position);
    }

    void AnimationClipPreview::BeginCameraPan(Vector2f local_position)
    {
        _viewport.BeginCameraPan(local_position);
    }

    void AnimationClipPreview::EndCameraPan()
    {
        _viewport.EndCameraPan();
    }

    void AnimationClipPreview::PanCamera(Vector2f local_position)
    {
        _viewport.PanCamera(local_position);
    }

    void AnimationClipPreview::ZoomCamera(f32 scroll_delta)
    {
        _viewport.ZoomCamera(scroll_delta);
    }

    void AnimationClipPreview::ResetCamera()
    {
        _viewport.ResetCamera();
    }

    bool AnimationClipPreview::BuildJointPositions(const Matrix4x4f &world_matrix,
                                                   Vector<Vector3f> &joint_positions) const
    {
        if (!HasResolvedSkeleton())
            return false;
        const Skeleton &skeleton = _preview_mesh->GetSkeletonAsset()->GetSkeleton();
        const SkeletonPose *pose = &_pose;
        if (_pose.Size() != skeleton.JointNum())
            pose = &skeleton.GetBindPose();
        Vector<Matrix4x4f> global_pose_palette;
        pose->GetMatrixPalette(global_pose_palette);
        Vector<Vector3f> mesh_space_positions;
        _preview_mesh->BuildMeshSpaceJointPositions(
            std::span<const Matrix4x4f>(global_pose_palette.data(), global_pose_palette.size()), mesh_space_positions);
        if (mesh_space_positions.size() != skeleton.JointNum())
            return false;
        joint_positions.resize(mesh_space_positions.size());
        for (u32 joint_index = 0u; joint_index < mesh_space_positions.size(); ++joint_index)
            joint_positions[joint_index] = TransformCoord(world_matrix, mesh_space_positions[joint_index]);
        return true;
    }

    bool AnimationClipPreview::PickJoint(Vector2f local_position, u16 &joint_index) const
    {
        joint_index = Joint::kInvalidJointIndex;
        if (!_show_skeleton || !HasResolvedSkeleton() || _preview_mesh->BoundBox().empty())
            return false;

        Vector3f center;
        Vector3f size;
        if (!GetMeshBounds(_preview_mesh.get(), center, size))
            return false;
        Vector<Vector3f> joint_positions;
        if (!BuildJointPositions(MatrixTranslation(-center), joint_positions))
            return false;

        const Skeleton &skeleton = _preview_mesh->GetSkeletonAsset()->GetSkeleton();
        const f32 hit_radius = std::max(Magnitude(size) * kSkeletonJointRadius * 2.5f, 0.001f);
        f32 best_screen_distance_squared = std::numeric_limits<f32>::max();
        f32 best_depth = std::numeric_limits<f32>::max();
        const auto consider_hit = [&](u16 candidate, f32 distance_squared, f32 depth, f32 screen_radius)
        {
            const f32 hit_radius_pixels = std::max(screen_radius, 6.0f);
            if (distance_squared > hit_radius_pixels * hit_radius_pixels ||
                distance_squared > best_screen_distance_squared - Math::kFloatEpsilon ||
                (std::abs(distance_squared - best_screen_distance_squared) <= Math::kFloatEpsilon &&
                 depth >= best_depth))
                return;
            joint_index = candidate;
            best_screen_distance_squared = distance_squared;
            best_depth = depth;
        };

        Vector<Vector2f> screen_positions(skeleton.JointNum());
        Vector<f32> depths(skeleton.JointNum(), 0.0f);
        for (const Joint &joint : skeleton)
        {
            if (joint._self >= joint_positions.size() ||
                !_viewport.ProjectWorldPoint(joint_positions[joint._self], screen_positions[joint._self],
                                              depths[joint._self]))
                continue;
        }

        for (const Joint &joint : skeleton)
        {
            if (joint._self >= joint_positions.size() || depths[joint._self] <= Math::kFloatEpsilon)
                continue;
            const Vector2f difference = screen_positions[joint._self] - local_position;
            const f32 screen_radius = _viewport.GetScreenRadius(hit_radius, depths[joint._self]);
            consider_hit(joint._self, DotProduct(difference, difference), depths[joint._self], screen_radius);
        }
        for (const Joint &joint : skeleton)
        {
            if (joint._self >= joint_positions.size() || joint._parent == Joint::kInvalidJointIndex ||
                joint._parent >= joint_positions.size() || depths[joint._self] <= Math::kFloatEpsilon ||
                depths[joint._parent] <= Math::kFloatEpsilon)
                continue;
            f32 segment_factor = 0.0f;
            const f32 distance_squared = DistanceSquaredToScreenSegment(local_position,
                                                                          screen_positions[joint._parent],
                                                                          screen_positions[joint._self],
                                                                          segment_factor);
            const f32 depth = depths[joint._parent] +
                              (depths[joint._self] - depths[joint._parent]) * segment_factor;
            const f32 screen_radius = _viewport.GetScreenRadius(hit_radius, depth);
            consider_hit(joint._self, distance_squared, depth, screen_radius);
        }
        return joint_index != Joint::kInvalidJointIndex;
    }

    void AnimationClipPreview::SetTime(f32 time)
    {
        _current_time = time;
        if (_clip != nullptr && _clip->Duration() > 0.0f)
        {
            if (_clip->IsLooping())
                _current_time = std::fmod(std::max(time, 0.0f), _clip->Duration());
            else
                _current_time = std::clamp(time, 0.0f, _clip->Duration());
        }
        EvaluatePose();
        UpdateSkinning();
        RenderPreview();
    }

    void AnimationClipPreview::SetShowSkeleton(bool show_skeleton)
    {
        if (_show_skeleton == show_skeleton)
            return;
        _show_skeleton = show_skeleton;
        RenderPreview();
    }

    void AnimationClipPreview::SetShowGrid(bool show_grid)
    {
        if (_show_grid == show_grid)
            return;
        _show_grid = show_grid;
        RenderPreview();
    }

    void AnimationClipPreview::SetWireframe(bool wireframe)
    {
        _viewport.SetWireframe(wireframe);
    }

    void AnimationClipPreview::SetSelectedJoint(u16 joint_index)
    {
        if (_selected_joint == joint_index)
            return;
        _selected_joint = joint_index;
        RenderPreview();
    }

    void AnimationClipPreview::RebuildPreviewMesh()
    {
        if (_skinning_system != nullptr)
            _skinning_system->Clear();
        _preview_mesh.reset();
        _viewport.SetMesh(nullptr);
        _binding.Clear();
        _blend_space_clips.clear();
        _pose = SkeletonPose();
        _palette.clear();
        if (_source_mesh == nullptr)
            return;

        auto preview_mesh = MakeRef<Render::SkeletonMesh>(std::format("{}_animation_preview", _source_mesh->Name()));
        preview_mesh->SetVertices(_source_mesh->GetVertices());
        preview_mesh->SetNormals(_source_mesh->GetNormals());
        preview_mesh->SetTangents(_source_mesh->GetTangents());
        preview_mesh->SetColors(_source_mesh->GetColors());
        for (u8 channel = 0u; channel < Render::Mesh::kMaxUVChannels; ++channel)
        {
            const auto uvs = _source_mesh->GetUVs(channel);
            if (!uvs.empty())
                preview_mesh->SetUVs(uvs, channel);
        }
        for (u16 submesh_index = 0u; submesh_index < _source_mesh->SubmeshCount(); ++submesh_index)
            preview_mesh->AddSubmesh(_source_mesh->GetIndices(submesh_index));
        preview_mesh->SetBoneWeights(_source_mesh->GetBoneWeights());
        preview_mesh->SetBoneIndices(_source_mesh->GetBoneIndices());
        preview_mesh->SetBounds(_source_mesh->BoundBox());
        preview_mesh->SetMeshBindGlobalTransform(_source_mesh->GetMeshBindGlobalTransform());
        preview_mesh->SetSkeletonAsset(_source_mesh->GetSkeletonAsset().GetGuid(),
                                       _source_mesh->GetSkeletonAsset().Get());
        preview_mesh->Apply();
        _preview_mesh = std::move(preview_mesh);
        _viewport.SetMesh(_preview_mesh.get());
        if (!HasResolvedSkeleton())
        {
            RenderPreview();
            return;
        }
        _pose = _preview_mesh->GetSkeletonAsset()->GetSkeleton().GetBindPose();

        if (_clip != nullptr)
        {
            _binding.Resolve(*_clip, _preview_mesh->GetSkeletonAsset()->GetSkeleton());
        }
        else if (_blend_space != nullptr)
        {
            ResolveBlendSpaceBindings();
        }
    }

    void AnimationClipPreview::ResolveBlendSpaceBindings()
    {
        if (_blend_space == nullptr || !HasResolvedSkeleton())
            return;
        const Skeleton &skeleton = _preview_mesh->GetSkeletonAsset()->GetSkeleton();
        _blend_space_duration = 0.0f;
        for (const auto &sample : _blend_space->Samples())
        {
            if (sample._clip.IsEmpty())
                continue;
            Ref<AnimationClip> clip = ResourceMgr::Get().GetRef<AnimationClip>(sample._clip);
            if (clip == nullptr)
                clip = ResourceMgr::Get().Load<AnimationClip>(sample._clip);
            if (clip == nullptr)
                continue;
            _blend_space_clips.push_back(clip);
            _blend_space_duration = std::max(_blend_space_duration, clip->Duration());
            _binding.Resolve(sample._clip, *clip, skeleton);
        }
    }

    void AnimationClipPreview::EvaluatePose()
    {
        if (!HasResolvedSkeleton())
        {
            _palette.clear();
            return;
        }
        const Skeleton &skeleton = _preview_mesh->GetSkeletonAsset()->GetSkeleton();
        if (_blend_space != nullptr)
        {
            if (_blend_space_clips.empty())
                ResolveBlendSpaceBindings();
            AnimationEvaluation evaluation;
            _blend_space->AddSamples(_blend_space_input, 0.0f, 1.0f, true, evaluation);
            f32 duration_sum = 0.0f;
            f32 weight_sum = 0.0f;
            for (u8 sample_index = 0u; sample_index < evaluation._sample_count; ++sample_index)
            {
                const auto &sample = evaluation._samples[sample_index];
                const AnimationClip *clip = _binding.FindClip(sample._clip);
                if (clip == nullptr || sample._weight <= 0.0f)
                    continue;
                duration_sum += clip->Duration() * sample._weight;
                weight_sum += sample._weight;
            }
            if (weight_sum > 0.0f)
                _blend_space_duration = duration_sum / weight_sum;
            const f32 phase = _blend_space_duration > 0.0f ? _current_time / _blend_space_duration : 0.0f;
            evaluation.Clear();
            _blend_space->AddSamples(_blend_space_input, phase, 1.0f, true, evaluation, true);
            _binding.Evaluate(evaluation, skeleton, _pose);
        }
        else if (_clip != nullptr)
        {
            if (_binding.FindClip(Guid::EmptyGuid()) != _clip)
                _binding.Resolve(*_clip, skeleton);

            AnimationEvaluation evaluation;
            evaluation.AddSample(AnimationSample{Guid::EmptyGuid(), _current_time, 1.0f, _clip->IsLooping()});
            _binding.Evaluate(evaluation, skeleton, _pose);
        }
        else
        {
            _pose = skeleton.GetBindPose();
        }
        Vector<Matrix4x4f> global_pose_palette;
        _pose.GetMatrixPalette(global_pose_palette);
        _preview_mesh->BuildSkinMatrixPalette(
            std::span<const Matrix4x4f>(global_pose_palette.data(), global_pose_palette.size()), _palette);
    }

    void AnimationClipPreview::UpdateSkinning()
    {
        if (_preview_mesh == nullptr || _palette.empty() || _skinning_system == nullptr)
            return;
        _skinning_system->Clear();
        _skinning_system->Submit(_preview_mesh.get(), _palette);
        _skinning_system->WaitFor();
    }

    void AnimationClipPreview::RenderPreview()
    {
        if (_preview_mesh == nullptr || !IsDrawableMesh(_preview_mesh.get()))
            return;
        _viewport.SetMesh(_preview_mesh.get());
        _viewport.SetShowGrid(_show_grid);
        _viewport.Render();
    }

    void AnimationClipPreview::DrawSkeletonOverlay(Render::CommandBuffer *cmd, const Matrix4x4f &world_matrix,
                                                   const Vector3f &center, const Vector3f &extents)
    {
        if (!_show_skeleton || cmd == nullptr || !HasResolvedSkeleton())
            return;
        auto material = Render::Material::s_standard_forward_lit.lock();
        if (material == nullptr)
            return;

            if (_skeleton_sphere == nullptr)
                _skeleton_sphere = Render::Mesh::s_sphere.lock();
            if (_skeleton_cone == nullptr)
                _skeleton_cone = Render::Mesh::s_cone.lock();
            if (!IsDrawableMesh(_skeleton_sphere.get()))
                _skeleton_sphere = ResourceMgr::Get().GetRef<Render::Mesh>(L"Meshs/src_res/sphere.alasset");
            if (!IsDrawableMesh(_skeleton_cone.get()))
                _skeleton_cone = ResourceMgr::Get().GetRef<Render::Mesh>(L"Meshs/src_res/cone.alasset");
            if (!IsDrawableMesh(_skeleton_sphere.get()))
                _skeleton_sphere = ResourceMgr::Get().Load<Render::Mesh>(L"Meshs/src_res/sphere.alasset");
            if (!IsDrawableMesh(_skeleton_cone.get()))
                _skeleton_cone = ResourceMgr::Get().Load<Render::Mesh>(L"Meshs/src_res/cone.alasset");

            Render::Mesh *sphere = _skeleton_sphere.get();
            Render::Mesh *cone = _skeleton_cone.get();
            if (IsDrawableMesh(sphere) && IsDrawableMesh(cone))
            {
                if (_skeleton_joint_material == nullptr)
                {
                    _skeleton_joint_material = material->CreateInstance();
                    _skeleton_joint_material->SetVector("_AlbedoValue", Vector4f(0.18f, 0.72f, 1.0f, 1.0f));
                    _skeleton_joint_material->SetFloat("_MetallicValue", 0.0f);
                    _skeleton_joint_material->SetFloat("_RoughnessValue", 0.55f);
                }
                if (_skeleton_bone_material == nullptr)
                {
                    _skeleton_bone_material = material->CreateInstance();
                    _skeleton_bone_material->SetVector("_AlbedoValue", Vector4f(0.08f, 0.34f, 0.62f, 1.0f));
                    _skeleton_bone_material->SetFloat("_MetallicValue", 0.0f);
                    _skeleton_bone_material->SetFloat("_RoughnessValue", 0.65f);
                }
                if (_skeleton_selected_material == nullptr)
                {
                    _skeleton_selected_material = material->CreateInstance();
                    _skeleton_selected_material->SetVector("_AlbedoValue", Vector4f(1.0f, 0.72f, 0.08f, 1.0f));
                    _skeleton_selected_material->SetFloat("_MetallicValue", 0.0f);
                    _skeleton_selected_material->SetFloat("_RoughnessValue", 0.4f);
                }

                Vector<Vector3f> joint_positions;
                const Skeleton &skeleton = _preview_mesh->GetSkeletonAsset()->GetSkeleton();
                if (!BuildJointPositions(world_matrix, joint_positions))
                    return;
                const f32 skeleton_radius = std::max(Magnitude(extents), 0.001f) * kSkeletonJointRadius;
                u16 unanimated_root_index = Joint::kInvalidJointIndex;
                for (const Joint &joint : skeleton)
                {
                    if (joint._parent == Joint::kInvalidJointIndex && !HasTransformTrack(_clip, joint._self))
                    {
                        unanimated_root_index = joint._self;
                        break;
                    }
                }
                for (const Joint &joint : skeleton)
                {
                    if (joint._self >= joint_positions.size() || joint._parent == Joint::kInvalidJointIndex ||
                        joint._parent >= joint_positions.size() || joint._parent == unanimated_root_index)
                        continue;
                    const Vector3f direction = joint_positions[joint._self] - joint_positions[joint._parent];
                    if (Magnitude(direction) <= Math::kFloatEpsilon)
                        continue;
                    const bool selected = joint._self == _selected_joint;
                    Render::Material *bone_material = selected ? _skeleton_selected_material.get() :
                                                                 _skeleton_bone_material.get();
                    cmd->DrawMesh(cone, bone_material,
                                  MakeBoneMatrix(cone, joint_positions[joint._parent], direction, skeleton_radius,
                                                 BuildIdentityMatrix()));
                }
                for (const Joint &joint : skeleton)
                {
                    if (joint._self >= joint_positions.size())
                        continue;
                    const bool selected = joint._self == _selected_joint;
                    Render::Material *joint_material = selected ? _skeleton_selected_material.get() :
                                                                  _skeleton_joint_material.get();
                    cmd->DrawMesh(sphere, joint_material,
                                  MakeJointMatrix(sphere, joint_positions[joint._self], skeleton_radius,
                                                 BuildIdentityMatrix()));
                }
            }
    }
}
