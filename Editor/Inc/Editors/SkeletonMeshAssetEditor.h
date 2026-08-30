#pragma once
#ifndef __SKELETON_MESH_ASSET_EDITOR_H__
#define __SKELETON_MESH_ASSET_EDITOR_H__

#include "Animation/AnimationClipPreview.h"
#include "Editors/AssetEditor.h"
#include "Framework/Core/SmartPtr.h"

namespace Ailu
{
    namespace Render
    {
        class SkeletonMesh;
    }

    namespace UI
    {
        class Button;
        class Dropdown;
        class HorizontalBox;
        class Image;
        class ObjectAssetDropdown;
        class TreeView;
        class Text;
        class VerticalBox;
    }

    namespace Editor
    {
        class SkeletonMeshAssetEditor final : public AssetEditor
        {
        public:
            SkeletonMeshAssetEditor();
            ~SkeletonMeshAssetEditor() override;

            void Update(f32 dt) override;

        protected:
            bool OnOpen() override;
            void OnClose() override;
            void OnAssetReloaded() override;

        private:
            void BuildToolbar(UI::HorizontalBox *toolbar);
            void BuildSkeletonTree(UI::VerticalBox *panel);
            void BuildInfoPanel(UI::VerticalBox *panel);
            void RefreshAllUI();
            void RefreshSkeletonTree();
            void RefreshBoneInfo();
            void RefreshPreview();
            void SelectJoint(u16 joint_index);
            void SelectSkeletonAsset(const Guid &guid);
            void SelectClip(i32 index);
            void TogglePlayback();
            void StopPlayback();

            Render::SkeletonMesh *_mesh = nullptr;
            AnimationClip *_clip = nullptr;
            Vector<Ref<AnimationClip>> _clips;
            Vector<WString> _clip_asset_paths;
            class SkeletonTreeDataSource;
            AnimationClipPreview _preview;
            Scope<SkeletonTreeDataSource> _data_source;
            UI::Image *_preview_image = nullptr;
            UI::TreeView *_skeleton_tree = nullptr;
            UI::ObjectAssetDropdown *_skeleton_dropdown = nullptr;
            UI::Dropdown *_clip_dropdown = nullptr;
            UI::Button *_btn_play = nullptr;
            UI::Button *_btn_skeleton = nullptr;
            UI::Button *_btn_grid = nullptr;
            UI::Button *_btn_wireframe = nullptr;
            UI::Text *_txt_time = nullptr;
            UI::Text *_txt_asset_name = nullptr;
            UI::Text *_txt_vertex_count = nullptr;
            UI::Text *_txt_triangle_count = nullptr;
            UI::Text *_txt_bone_count = nullptr;
            UI::Text *_txt_skeleton_guid = nullptr;
            UI::Text *_txt_skeleton_layout = nullptr;
            UI::Text *_txt_selected_bone = nullptr;
            UI::Text *_txt_parent = nullptr;
            UI::Text *_txt_children = nullptr;
            UI::Text *_txt_bind_position = nullptr;
            u16 _selected_joint = Joint::kInvalidJointIndex;
            Vector2f _preview_mouse_down_position = Vector2f::kZero;
            bool _preview_camera_dragged = false;
            f32 _preview_time = 0.0f;
            bool _playing = false;
            bool _show_skeleton = true;
            bool _show_grid = true;
            bool _wireframe = false;
        };
    }
}

#endif // __SKELETON_MESH_ASSET_EDITOR_H__
