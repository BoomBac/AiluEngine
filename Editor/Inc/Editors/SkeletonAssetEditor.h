#pragma once
#ifndef __SKELETON_ASSET_EDITOR_H__
#define __SKELETON_ASSET_EDITOR_H__

#include "Animation/Skeleton.h"
#include "Animation/AssetPreviewViewport3D.h"
#include "Editors/AssetEditor.h"
#include "Framework/Core/SmartPtr.h"

namespace Ailu
{
    class SkeletonAsset;

    namespace Render
    {
        class CommandBuffer;
        class Material;
        class Mesh;
    }

    namespace UI
    {
        class Image;
        class Text;
        class TreeView;
        class VerticalBox;
    }

    namespace Editor
    {
        class SkeletonAssetEditor final : public AssetEditor
        {
        public:
            SkeletonAssetEditor();
            ~SkeletonAssetEditor() override;
            void Update(f32 dt) override;

        protected:
            bool OnOpen() override;
            void OnClose() override;
            void OnAssetReloaded() override;

        private:
            void BuildSkeletonTree(UI::VerticalBox *panel);
            void BuildInfoPanel(UI::VerticalBox *panel);
            void RefreshAllUI();
            void RefreshSkeletonTree();
            void RefreshJointInfo();
            void RefreshPreview();
            void SelectJoint(u16 joint_index);
            bool BuildJointPositions(const Matrix4x4f &world_matrix, Vector<Vector3f> &joint_positions) const;
            AABB BuildPreviewBounds() const;
            bool PickJoint(Vector2f local_position, u16 &joint_index) const;
            void DrawSkeletonOverlay(Render::CommandBuffer *command_buffer, const Matrix4x4f &world_matrix,
                                     const Vector3f &center, const Vector3f &extents);

            SkeletonAsset *_skeleton_asset = nullptr;
            class SkeletonTreeDataSource;
            Scope<SkeletonTreeDataSource> _data_source;
            AssetPreviewViewport3D _preview;
            Ref<Render::Mesh> _skeleton_sphere;
            Ref<Render::Mesh> _skeleton_cone;
            Ref<Render::Material> _skeleton_joint_material;
            Ref<Render::Material> _skeleton_bone_material;
            Ref<Render::Material> _skeleton_selected_material;
            UI::Image *_preview_image = nullptr;
            UI::TreeView *_skeleton_tree = nullptr;
            UI::Text *_txt_asset_name = nullptr;
            UI::Text *_txt_asset_guid = nullptr;
            UI::Text *_txt_joint_count = nullptr;
            UI::Text *_txt_layout_hash = nullptr;
            UI::Text *_txt_selected_joint = nullptr;
            UI::Text *_txt_parent = nullptr;
            UI::Text *_txt_children = nullptr;
            UI::Text *_txt_bind_position = nullptr;
            UI::Text *_txt_bind_rotation = nullptr;
            UI::Text *_txt_bind_scale = nullptr;
            u16 _selected_joint = Joint::kInvalidJointIndex;
            Vector2f _preview_mouse_down_position = Vector2f::kZero;
            bool _preview_camera_dragged = false;
        };
    }
}

#endif // __SKELETON_ASSET_EDITOR_H__
