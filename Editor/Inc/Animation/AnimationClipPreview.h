#pragma once

#include "Animation/Skeleton/SkeletonAnimationBinding.h"
#include "Animation/SkinningSystem.h"
#include "Animation/AssetPreviewViewport3D.h"
#include "Framework/Core/SmartPtr.h"

namespace Ailu::Render
{
    class CommandBuffer;
    class Material;
    class Mesh;
    class SkeletonMesh;
}

namespace Ailu::Editor
{
    class AnimationClipPreview
    {
    public:
        AnimationClipPreview();
        ~AnimationClipPreview();

        void SetClip(AnimationClip *clip);
        void SetMesh(Render::SkeletonMesh *mesh);
        void SetTime(f32 time);
        void SetShowSkeleton(bool show_skeleton);
        void SetShowGrid(bool show_grid);
        void SetWireframe(bool wireframe);
        void SetSelectedJoint(u16 joint_index);
        bool SetViewportSize(Vector2f size);
        void BeginCameraDrag(Vector2f local_position);
        void EndCameraDrag();
        void DragCamera(Vector2f local_position);
        void BeginCameraPan(Vector2f local_position);
        void EndCameraPan();
        void PanCamera(Vector2f local_position);
        void ZoomCamera(f32 scroll_delta);
        void ResetCamera();
        bool PickJoint(Vector2f local_position, u16 &joint_index) const;

        Render::RenderTexture *GetRenderTexture() const { return _viewport.GetRenderTexture(); }
        Render::SkeletonMesh *GetPreviewMesh() const { return _preview_mesh.get(); }
        f32 CurrentTime() const { return _current_time; }
        bool IsRenderPending() const { return _viewport.IsRenderPending(); }
        void RenderIfPending() { if (_viewport.IsRenderPending()) RenderPreview(); }

    private:
        void RebuildPreviewMesh();
        void EvaluatePose();
        void UpdateSkinning();
        void RenderPreview();
        bool HasResolvedSkeleton() const;
        bool BuildJointPositions(const Matrix4x4f &world_matrix, Vector<Vector3f> &joint_positions) const;
        void DrawSkeletonOverlay(Render::CommandBuffer *command_buffer, const Matrix4x4f &world_matrix,
                                 const Vector3f &center, const Vector3f &extents);

        AnimationClip *_clip = nullptr;
        Render::SkeletonMesh *_source_mesh = nullptr;
        Ref<Render::SkeletonMesh> _preview_mesh;
        AssetPreviewViewport3D _viewport;
        Ref<Render::Mesh> _skeleton_sphere;
        Ref<Render::Mesh> _skeleton_cone;
        Ref<Render::Material> _skeleton_joint_material;
        Ref<Render::Material> _skeleton_bone_material;
        Ref<Render::Material> _skeleton_selected_material;
        SkeletonAnimationBinding _binding;
        SkeletonPose _pose;
        Vector<Matrix4x4f> _palette;
        Scope<ECS::SkinningSystem> _skinning_system;
        f32 _current_time = 0.0f;
        u16 _selected_joint = Joint::kInvalidJointIndex;
        bool _show_skeleton = false;
        bool _show_grid = true;
    };
}
