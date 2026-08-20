#pragma once
#ifndef __AUDIO_CLIP_EDITOR_H__
#define __AUDIO_CLIP_EDITOR_H__

#include "Audio/AudioHandle.h"
#include "Audio/AudioTypes.h"
#include "Editors/AssetEditor.h"
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
        class AudioClipEditor : public AssetEditor
        {
        public:
            AudioClipEditor();
            ~AudioClipEditor() override;

            void Update(f32 dt) override;
            using AssetEditor::Open;
            void Open(AudioClip *clip);

        private:
            void BuildToolbar(UI::HorizontalBox *toolbar);
            void BuildInfoPanel(UI::VerticalBox *panel);
            void BuildImportPanel(UI::VerticalBox *panel);
            void PlayPreview();
            void StopPreview();
            void RefreshAllUI();
            void RefreshAssetInfo();
            void RefreshImportSettings();
            void RefreshStatus();
            void SetLoadMode(EAudioLoadMode mode);
            void SetChannelMode(EAudioChannelMode mode);
            void MarkEdited();

            static UI::Text *AddSectionTitle(UI::UIElement *parent, const String &title);
            static UI::HorizontalBox *AddPropertyRow(UI::UIElement *parent, const String &label,
                                                     f32 label_width = 96.0f);
            static String ToString(EAudioLoadMode mode);
            static String ToString(EAudioChannelMode mode);

            bool OnOpen() override;
            void OnClose() override;
            void OnAssetSaved() override;
            void OnAssetReloaded() override;

        private:
            AudioClip *_clip = nullptr;
            AudioHandle _preview_handle;

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
            String _last_edit_snapshot;
        };
    }// namespace Editor
}// namespace Ailu

#endif// __AUDIO_CLIP_EDITOR_H__
