#pragma once
#ifndef __ANIMATION_CLIP_EDITOR_H__
#define __ANIMATION_CLIP_EDITOR_H__

#include "Animation/AnimationEvent.h"
#include "Animation/Clip.h"
#include "Editors/AssetEditor.h"
#include "UI/Container.h"

#include <functional>

namespace Ailu
{
    namespace Render
    {
        class Sprite;
    }

    namespace UI
    {
        class Button;
        class CheckBox;
        class Dropdown;
        class HorizontalBox;
        class InputBlock;
        class Image;
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
            using AssetEditor::Open;
            void Open(AnimationClip *clip);
            void Close();

        private:
            void MarkDirty();
            void RefreshAllUI();
            void RefreshTimeline();
            void RefreshEvents();
            void RefreshPreview();
            void AddFrame();
            void AddEvent();
            void RemoveFrame(u32 index);
            void RemoveEvent(u32 index);
            void ShowSpritePicker(u32 frame_index, UI::UIElement *anchor);

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

            void OnBeforeSave() override;
            void OnAssetSaved() override;
            void OnAssetReloaded() override;

        private:
            AnimationClip *_clip = nullptr;
            bool _is_previewing = false;
            f32 _preview_time = 0.0f;

            UI::VerticalBox *_timeline_root = nullptr;
            UI::VerticalBox *_events_root = nullptr;
            UI::Image *_preview_image = nullptr;
            UI::Text *_txt_preview_time = nullptr;
            Ref<Render::Sprite> _preview_sprite;
            Guid _preview_sprite_guid = Guid::EmptyGuid();
            UI::Text *_txt_status = nullptr;
            UI::InputBlock *_input_name = nullptr;
            UI::InputBlock *_input_duration = nullptr;
            UI::InputBlock *_input_rate = nullptr;
            UI::InputBlock *_input_frame_duration = nullptr;
            UI::CheckBox *_check_looping = nullptr;
            UI::Button *_btn_play = nullptr;
            UI::Button *_btn_stop = nullptr;
            String _last_edit_snapshot;
            bool _is_refreshing_ui = false;
        };
    }// namespace Editor
}// namespace Ailu

#endif // __ANIMATION_CLIP_EDITOR_H__
