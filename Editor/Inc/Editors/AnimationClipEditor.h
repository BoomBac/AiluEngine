#pragma once
#ifndef __ANIMATION_CLIP_EDITOR_H__
#define __ANIMATION_CLIP_EDITOR_H__

#include "Animation/AnimationEvent.h"
#include "Animation/Clip.h"
#include "Dock/DockWindow.h"
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
        class AnimationClipEditor final : public DockWindow
        {
        public:
            AnimationClipEditor();
            ~AnimationClipEditor() override = default;

            void Update(f32 dt) override;
            void Open(AnimationClip *clip);
            void Close();

        private:
            void ReadFromAsset();
            void WriteToAsset();
            void Apply();
            void Revert();
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

            static UI::Text *AddSectionTitle(UI::UIElement *parent, const String &title);
            static UI::HorizontalBox *AddPropertyRow(UI::UIElement *parent, const String &label);
            static UI::InputBlock *AddTextInput(UI::UIElement *parent, const String &label, const String &value,
                                                const std::function<void(String)> &on_changed);
            static UI::InputBlock *AddFloatInput(UI::UIElement *parent, const String &label, f32 value,
                                                 const std::function<void(f32)> &on_changed);

        private:
            AnimationClip *_clip = nullptr;
            String _editing_name;
            u32 _editing_frame_count = 0u;
            f32 _editing_duration = 0.0f;
            f32 _editing_frame_rate = 30.0f;
            f32 _editing_frame_duration = 0.0f;
            bool _editing_looping = true;
            Vector<SpriteKeyFrame> _editing_frames;
            Vector<AnimationEvent> _editing_events;
            String _original_name;
            u32 _original_frame_count = 0u;
            f32 _original_duration = 0.0f;
            f32 _original_frame_rate = 30.0f;
            f32 _original_frame_duration = 0.0f;
            bool _original_looping = true;
            Vector<SpriteKeyFrame> _original_frames;
            Vector<AnimationEvent> _original_events;
            bool _is_dirty = false;
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
            UI::Button *_btn_apply = nullptr;
            UI::Button *_btn_revert = nullptr;
            UI::Button *_btn_play = nullptr;
            UI::Button *_btn_stop = nullptr;
        };
    }// namespace Editor
}// namespace Ailu

#endif // __ANIMATION_CLIP_EDITOR_H__
