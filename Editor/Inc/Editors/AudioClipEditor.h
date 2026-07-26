#pragma once
#ifndef __AUDIO_CLIP_EDITOR_H__
#define __AUDIO_CLIP_EDITOR_H__

#include "Audio/AudioHandle.h"
#include "Audio/AudioTypes.h"
#include "Dock/DockWindow.h"
#include "Framework/Math/Guid.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIElement.h"

namespace Ailu
{
    class AudioClip;

    namespace UI
    {
        class Button;
        class CheckBox;
        class HorizontalBox;
        class InputBlock;
        class Text;
        class VerticalBox;
    }// namespace UI

    namespace Editor
    {
        struct AudioClipEditData
        {
            EAudioLoadMode _load_mode = EAudioLoadMode::kMemory;
            EAudioChannelMode _channel_mode = EAudioChannelMode::kAuto;
            bool _force_mono = false;

            bool operator==(const AudioClipEditData &other) const
            {
                return _load_mode == other._load_mode && _channel_mode == other._channel_mode &&
                       _force_mono == other._force_mono;
            }
            bool operator!=(const AudioClipEditData &other) const { return !(*this == other); }
        };

        class AudioClipEditor : public DockWindow
        {
        public:
            AudioClipEditor();
            ~AudioClipEditor() override;

            void Update(f32 dt) override;
            void Open(AudioClip *asset);
            void Close();

            bool IsDirty() const { return _editing != _original; }

        private:
            void BuildToolbar(UI::HorizontalBox *toolbar);
            void BuildInfoPanel(UI::VerticalBox *panel);
            void BuildImportPanel(UI::VerticalBox *panel);
            void ReadFromAsset();
            void WriteToAsset();
            void Apply();
            void Revert();
            void PlayPreview();
            void StopPreview();
            void RefreshAllUI();
            void RefreshAssetInfo();
            void RefreshImportSettings();
            void RefreshStatus();
            void SetLoadMode(EAudioLoadMode mode);
            void SetChannelMode(EAudioChannelMode mode);

            static UI::Text *AddSectionTitle(UI::UIElement *parent, const String &title);
            static UI::HorizontalBox *AddPropertyRow(UI::UIElement *parent, const String &label,
                                                     f32 label_width = 96.0f);
            static String ToString(EAudioLoadMode mode);
            static String ToString(EAudioChannelMode mode);

        private:
            AudioClip *_clip = nullptr;
            AudioClipEditData _original;
            AudioClipEditData _editing;
            AudioHandle _preview_handle;

            UI::Button *_btn_apply = nullptr;
            UI::Button *_btn_revert = nullptr;
            UI::Button *_btn_play = nullptr;
            UI::Button *_btn_stop = nullptr;
            UI::Button *_btn_memory = nullptr;
            UI::Button *_btn_streaming = nullptr;
            UI::Button *_btn_auto = nullptr;
            UI::Button *_btn_mono = nullptr;
            UI::Button *_btn_stereo = nullptr;
            UI::CheckBox *_chk_force_mono = nullptr;
            UI::Text *_txt_asset_name = nullptr;
            UI::Text *_txt_asset_path = nullptr;
            UI::Text *_txt_asset_guid = nullptr;
            UI::Text *_txt_source_path = nullptr;
            UI::Text *_txt_runtime_path = nullptr;
            UI::Text *_txt_duration = nullptr;
            UI::Text *_txt_sample_rate = nullptr;
            UI::Text *_txt_channels = nullptr;
            UI::Text *_txt_status = nullptr;
        };
    }// namespace Editor
}// namespace Ailu

#endif// __AUDIO_CLIP_EDITOR_H__
