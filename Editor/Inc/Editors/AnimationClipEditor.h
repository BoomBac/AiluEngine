#pragma once
#ifndef __ANIMATION_CLIP_EDITOR_H__
#define __ANIMATION_CLIP_EDITOR_H__

#include "Animation/AnimationEvent.h"
#include "Animation/AnimationClipPreview.h"
#include "Animation/AnimationTimeline.h"
#include "Animation/Clip.h"
#include "Animation/SkeletonAsset.h"
#include "Editors/AssetEditor.h"
#include "UI/Container.h"

#include <functional>

namespace Ailu
{
    namespace Render
    {
        class Sprite;
        class SkeletonMesh;
    }

    namespace UI
    {
        class Button;
        class CheckBox;
        class Dropdown;
        class HorizontalBox;
        class InputBlock;
        class Image;
        class ObjectAssetDropdown;
        class Slider;
        class Text;
        class VerticalBox;
    }

    namespace Editor
    {
        class AnimationClipEditor final : public AssetEditor
        {
        public:
            AnimationClipEditor();
            ~AnimationClipEditor() override = default;

            void Update(f32 dt) override;

        private:
            void MarkDirty();
            void RefreshAllUI();
            void RefreshTimeline();
            void RefreshEvents();
            void RefreshPreview();
            void SetPreviewTime(f32 time, bool refresh_slider = true);
            void RefreshPreviewControls();
            void TogglePreviewPlayback();
            void StepPreview(f32 direction);
            void RefreshSkeletonAsset();
            bool MapToSkeleton(const Guid &target_guid);
            void ShowSkeletonMappingError(const String &message);
            void AddFrame();
            void AddEvent();
            void RemoveFrame(u32 index);
            void RemoveEvent(u32 index);
            void ShowSpritePicker(u32 frame_index, UI::UIElement *anchor);
            void SelectJoint(u16 joint_index);

            Vector<SpriteKeyFrame> &Frames() { return _clip->SpriteTrack().Frames(); }
            const Vector<SpriteKeyFrame> &Frames() const { return _clip->SpriteTrack().Frames(); }
            Vector<AnimationEvent> &Events() { return _clip->Events(); }
            const Vector<AnimationEvent> &Events() const { return _clip->Events(); }

            static UI::Text *AddSectionTitle(UI::UIElement *parent, const String &title);
            static UI::HorizontalBox *AddPropertyRow(UI::UIElement *parent, const String &label);
            static UI::InputBlock *AddTextInput(UI::UIElement *parent, const String &label, const String &value,
                                                const std::function<void(String)> &on_changed);
            static UI::InputBlock *AddFloatInput(UI::UIElement *parent, const String &label, f32 value,
                                                 const std::function<void(f32)> &on_changed);

            bool OnOpen() override;
            void OnClose() override;
            void OnBeforeSave() override;
            void OnAssetSaved() override;
            void OnAssetReloaded() override;

        private:
            AnimationClip *_clip = nullptr;
            bool _is_previewing = false;
            f32 _preview_time = 0.0f;

            UI::VerticalBox *_timeline_root = nullptr;
            UI::VerticalBox *_events_root = nullptr;
            AnimationTimeline *_animation_timeline = nullptr;
            Scope<AnimationClipPreview> _animation_preview;
            UI::Image *_preview_image = nullptr;
            UI::Text *_txt_preview_time = nullptr;
            UI::Slider *_preview_time_slider = nullptr;
            UI::Button *_preview_play_button = nullptr;
            UI::Button *_preview_loop_button = nullptr;
            Ref<Render::Sprite> _preview_sprite;
            Guid _preview_sprite_guid = Guid::EmptyGuid();
            UI::Text *_txt_status = nullptr;
            UI::InputBlock *_input_name = nullptr;
            UI::InputBlock *_input_duration = nullptr;
            UI::InputBlock *_input_rate = nullptr;
            UI::InputBlock *_input_frame_duration = nullptr;
            UI::CheckBox *_check_looping = nullptr;
            UI::CheckBox *_check_skeleton = nullptr;
            UI::ObjectAssetDropdown *_skeleton_dropdown = nullptr;
            UI::ObjectAssetDropdown *_preview_mesh_dropdown = nullptr;
            String _last_edit_snapshot;
            bool _is_refreshing_ui = false;
            Guid _preview_mesh_guid = Guid::EmptyGuid();
            Ref<Render::SkeletonMesh> _preview_mesh;
            Ref<SkeletonAsset> _skeleton_asset;
            bool _show_skeleton = false;
            u16 _selected_joint = Joint::kInvalidJointIndex;
            bool _preview_loop = true;
            Vector2f _preview_mouse_down_position = Vector2f::kZero;
            bool _preview_camera_dragged = false;
        };
    }// namespace Editor
}// namespace Ailu

#endif // __ANIMATION_CLIP_EDITOR_H__
