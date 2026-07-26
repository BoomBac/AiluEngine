#include "Editors/AudioClipEditor.h"
#include "Audio/Audio.h"
#include "Audio/AudioClip.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include <format>

using namespace Ailu::UI;

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            constexpr f32 kToolbarHeight = 28.0f;
            constexpr f32 kInputHeight = 22.0f;
            constexpr f32 kLeftPanelWidth = 300.0f;

            void StyleModeButton(UI::Button *button, bool active)
            {
                if (button)
                    button->SetInteractiveEnabled(!active);
            }
        }// namespace

        AudioClipEditor::AudioClipEditor() : DockWindow("Audio Clip", Vector2f(780.0f, 420.0f))
        {
            SetPosition(Vector2f(160.0f, 80.0f));

            auto *root_vb = _content_root->AddChild<UI::VerticalBox>();
            root_vb->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto *toolbar = root_vb->AddChild<UI::HorizontalBox>();
            toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, kToolbarHeight));
            BuildToolbar(toolbar);

            auto *main_area = root_vb->AddChild<UI::SplitView>();
            main_area->_is_horizontal = true;
            main_area->SetRatio(kLeftPanelWidth / 780.0f);
            main_area->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto *left_border = main_area->AddChild<UI::Border>();
            left_border->_bg_color = Color(0.16f, 0.17f, 0.19f, 1.0f);
            auto *left = left_border->AddChild<UI::VerticalBox>();
            left->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            BuildInfoPanel(left);

            auto *right_border = main_area->AddChild<UI::Border>();
            right_border->_bg_color = Color(0.12f, 0.13f, 0.14f, 1.0f);
            auto *right = right_border->AddChild<UI::VerticalBox>();
            right->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            BuildImportPanel(right);
        }

        AudioClipEditor::~AudioClipEditor()
        {
            StopPreview();
        }

        void AudioClipEditor::Open(AudioClip *asset)
        {
            if (!asset)
                return;
            _clip = asset;
            ReadFromAsset();
            _original = _editing;
            SetTitle("Audio Clip - " + asset->Name());
            RefreshAllUI();
        }

        void AudioClipEditor::Close()
        {
            StopPreview();
            _clip = nullptr;
        }

        void AudioClipEditor::Update(f32 dt)
        {
            DockWindow::Update(dt);
            if (!_clip)
                return;

            const bool ctrl = Input::IsKeyDown(EKey::kLCONTROL) || Input::IsKeyDown(EKey::kRCONTROL);
            if (ctrl && Input::IsKeyDownAccurate(EKey::kS))
                Apply();
            if (Input::IsKeyDownAccurate(EKey::kSPACE))
                PlayPreview();

            const bool dirty = IsDirty();
            if (_btn_apply)
                _btn_apply->SetInteractiveEnabled(dirty);
            if (_btn_revert)
                _btn_revert->SetInteractiveEnabled(dirty);
            RefreshStatus();
        }

        void AudioClipEditor::ReadFromAsset()
        {
            if (!_clip)
                return;
            _editing._load_mode = _clip->_load_mode;
            _editing._channel_mode = _clip->_channel_mode;
            _editing._force_mono = _clip->_force_mono;
        }

        void AudioClipEditor::WriteToAsset()
        {
            if (!_clip)
                return;
            _clip->_load_mode = _editing._load_mode;
            _clip->_channel_mode = _editing._channel_mode;
            _clip->_force_mono = _editing._force_mono;
        }

        void AudioClipEditor::Apply()
        {
            if (!_clip || !IsDirty())
                return;
            WriteToAsset();
            _original = _editing;
            if (auto *linked = ResourceMgr::Get().GetLinkedAsset(_clip))
            {
                ResourceMgr::Get().SaveAsset(linked);
                LOG_INFO("AudioClipEditor: Applied");
            }
            RefreshAllUI();
        }

        void AudioClipEditor::Revert()
        {
            if (!IsDirty())
                return;
            _editing = _original;
            RefreshAllUI();
        }

        void AudioClipEditor::PlayPreview()
        {
            if (!_clip)
                return;
            auto *linked = ResourceMgr::Get().GetLinkedAsset(_clip);
            if (!linked)
                return;
            StopPreview();
            AudioPlayOptions options;
            options._bus = EAudioBus::kUi;
            options._is_3d = false;
            options._streaming = _editing._load_mode == EAudioLoadMode::kStreaming;
            _preview_handle = Audio::Play(linked->GetGuid(), options);
        }

        void AudioClipEditor::StopPreview()
        {
            if (_preview_handle.IsValid())
            {
                Audio::Stop(_preview_handle);
                _preview_handle = {};
            }
        }

        void AudioClipEditor::RefreshAllUI()
        {
            RefreshAssetInfo();
            RefreshImportSettings();
            RefreshStatus();
        }

        void AudioClipEditor::RefreshAssetInfo()
        {
            if (!_clip)
                return;
            if (_txt_asset_name)
                _txt_asset_name->SetText(_clip->Name());
            if (_txt_duration)
                _txt_duration->SetText(_clip->_duration > 0.0f ? std::format("{:.2f}s", _clip->_duration) : "-");
            if (_txt_sample_rate)
                _txt_sample_rate->SetText(_clip->_sample_rate > 0u ? std::format("{} Hz", _clip->_sample_rate) : "-");
            if (_txt_channels)
                _txt_channels->SetText(_clip->_channel_count > 0u ? std::format("{}", _clip->_channel_count) : "-");
            if (_txt_runtime_path)
                _txt_runtime_path->SetText(_clip->_runtime_path.empty() ? "-" : _clip->_runtime_path);

            if (auto *linked = ResourceMgr::Get().GetLinkedAsset(_clip))
            {
                if (_txt_asset_path)
                    _txt_asset_path->SetText(ToChar(linked->_asset_path));
                if (_txt_asset_guid)
                    _txt_asset_guid->SetText(linked->GetGuid().ToString());
                if (_txt_source_path)
                    _txt_source_path->SetText(linked->_external_asset_path.empty() ? "-" :
                                              ToChar(linked->_external_asset_path));
            }
        }

        void AudioClipEditor::RefreshImportSettings()
        {
            StyleModeButton(_btn_memory, _editing._load_mode == EAudioLoadMode::kMemory);
            StyleModeButton(_btn_streaming, _editing._load_mode == EAudioLoadMode::kStreaming);
            StyleModeButton(_btn_auto, _editing._channel_mode == EAudioChannelMode::kAuto);
            StyleModeButton(_btn_mono, _editing._channel_mode == EAudioChannelMode::kMono);
            StyleModeButton(_btn_stereo, _editing._channel_mode == EAudioChannelMode::kStereo);
            if (_chk_force_mono)
                _chk_force_mono->SetChecked(_editing._force_mono);
        }

        void AudioClipEditor::RefreshStatus()
        {
            if (_txt_status)
                _txt_status->SetText(IsDirty() ? "Modified" : "Saved");
        }

        void AudioClipEditor::SetLoadMode(EAudioLoadMode mode)
        {
            _editing._load_mode = mode;
            RefreshImportSettings();
        }

        void AudioClipEditor::SetChannelMode(EAudioChannelMode mode)
        {
            _editing._channel_mode = mode;
            RefreshImportSettings();
        }

        UI::Text *AudioClipEditor::AddSectionTitle(UI::UIElement *parent, const String &title)
        {
            auto *txt = parent->AddChild<UI::Text>(title);
            txt->_color = Color(0.85f, 0.85f, 0.85f, 1.0f);
            txt->FontSize(13.0f);
            txt->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, 22.0f)).Margin(Vector4f(8.0f, 8.0f, 0.0f, 2.0f));
            return txt;
        }

        UI::HorizontalBox *AudioClipEditor::AddPropertyRow(UI::UIElement *parent, const String &label, f32 label_width)
        {
            auto *row = parent->AddChild<UI::HorizontalBox>();
            row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(8.0f, 1.0f, 8.0f, 1.0f));
            auto *lbl = row->AddChild<UI::Text>(label);
            lbl->_color = Color(0.65f, 0.65f, 0.65f, 1.0f);
            lbl->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(label_width, 0.0f));
            return row;
        }

        String AudioClipEditor::ToString(EAudioLoadMode mode)
        {
            return mode == EAudioLoadMode::kStreaming ? "Streaming" : "Memory";
        }

        String AudioClipEditor::ToString(EAudioChannelMode mode)
        {
            switch (mode)
            {
                case EAudioChannelMode::kMono: return "Mono";
                case EAudioChannelMode::kStereo: return "Stereo";
                default: return "Auto";
            }
        }

        void AudioClipEditor::BuildToolbar(UI::HorizontalBox *toolbar)
        {
            toolbar->SlotPadding() = UI::Padding(4.0f, 2.0f, 4.0f, 2.0f);

            _btn_apply = toolbar->AddChild<UI::Button>("Apply");
            _btn_apply->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(56.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 2.0f, 0.0f));
            _btn_apply->OnMouseClick() += [this](UI::UIEvent &e) { Apply(); e._is_handled = true; };

            _btn_revert = toolbar->AddChild<UI::Button>("Revert");
            _btn_revert->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(56.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 8.0f, 0.0f));
            _btn_revert->OnMouseClick() += [this](UI::UIEvent &e) { Revert(); e._is_handled = true; };

            _btn_play = toolbar->AddChild<UI::Button>("Play");
            _btn_play->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(48.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 2.0f, 0.0f));
            _btn_play->OnMouseClick() += [this](UI::UIEvent &e) { PlayPreview(); e._is_handled = true; };

            _btn_stop = toolbar->AddChild<UI::Button>("Stop");
            _btn_stop->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(48.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 8.0f, 0.0f));
            _btn_stop->OnMouseClick() += [this](UI::UIEvent &e) { StopPreview(); e._is_handled = true; };

            _txt_status = toolbar->AddChild<UI::Text>("Saved");
            _txt_status->_color = Color(0.7f, 0.7f, 0.7f, 1.0f);
            _txt_status->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(72.0f, 0.0f));

            auto *spacer = toolbar->AddChild<UI::Text>("");
            spacer->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        }

        void AudioClipEditor::BuildInfoPanel(UI::VerticalBox *panel)
        {
            panel->SlotPadding() = UI::Padding(4.0f);
            AddSectionTitle(panel, "Asset Info");

            auto add_text_row = [panel](const String &label, UI::Text *&out) {
                auto *row = AddPropertyRow(panel, label);
                out = row->AddChild<UI::Text>("-");
                out->_color = Color(0.75f, 0.75f, 0.75f, 1.0f);
                out->FontSize(11.0f);
                out->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            };
            add_text_row("Name", _txt_asset_name);
            add_text_row("Path", _txt_asset_path);
            add_text_row("GUID", _txt_asset_guid);
            add_text_row("Source", _txt_source_path);
            add_text_row("Runtime", _txt_runtime_path);

            AddSectionTitle(panel, "Audio Info");
            add_text_row("Duration", _txt_duration);
            add_text_row("Sample Rate", _txt_sample_rate);
            add_text_row("Channels", _txt_channels);
        }

        void AudioClipEditor::BuildImportPanel(UI::VerticalBox *panel)
        {
            panel->SlotPadding() = UI::Padding(4.0f);
            AddSectionTitle(panel, "Import Settings");

            auto *load_row = AddPropertyRow(panel, "Load Mode");
            _btn_memory = load_row->AddChild<UI::Button>("Memory");
            _btn_memory->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed,
                                                                UI::ESizePolicy::kFill)
                    .Size(Vector2f(76.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 4.0f, 0.0f));
            _btn_memory->OnMouseClick() += [this](UI::UIEvent &e) {
                SetLoadMode(EAudioLoadMode::kMemory);
                e._is_handled = true;
            };
            _btn_streaming = load_row->AddChild<UI::Button>("Streaming");
            _btn_streaming->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed,
                                                                   UI::ESizePolicy::kFill)
                    .Size(Vector2f(88.0f, 0.0f));
            _btn_streaming->OnMouseClick() += [this](UI::UIEvent &e) {
                SetLoadMode(EAudioLoadMode::kStreaming);
                e._is_handled = true;
            };

            auto *channel_row = AddPropertyRow(panel, "Channel");
            _btn_auto = channel_row->AddChild<UI::Button>("Auto");
            _btn_auto->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed,
                                                              UI::ESizePolicy::kFill)
                    .Size(Vector2f(56.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 4.0f, 0.0f));
            _btn_auto->OnMouseClick() += [this](UI::UIEvent &e) {
                SetChannelMode(EAudioChannelMode::kAuto);
                e._is_handled = true;
            };
            _btn_mono = channel_row->AddChild<UI::Button>("Mono");
            _btn_mono->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed,
                                                              UI::ESizePolicy::kFill)
                    .Size(Vector2f(56.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 4.0f, 0.0f));
            _btn_mono->OnMouseClick() += [this](UI::UIEvent &e) {
                SetChannelMode(EAudioChannelMode::kMono);
                e._is_handled = true;
            };
            _btn_stereo = channel_row->AddChild<UI::Button>("Stereo");
            _btn_stereo->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed,
                                                                UI::ESizePolicy::kFill)
                    .Size(Vector2f(64.0f, 0.0f));
            _btn_stereo->OnMouseClick() += [this](UI::UIEvent &e) {
                SetChannelMode(EAudioChannelMode::kStereo);
                e._is_handled = true;
            };

            auto *mono_row = AddPropertyRow(panel, "Force Mono");
            _chk_force_mono = mono_row->AddChild<UI::CheckBox>();
            _chk_force_mono->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed,
                                                                    UI::ESizePolicy::kFill)
                    .Size(Vector2f(24.0f, 0.0f));
            _chk_force_mono->OnMouseClick() += [this](UI::UIEvent &e) {
                _editing._force_mono = _chk_force_mono->IsChecked();
                RefreshImportSettings();
                e._is_handled = true;
            };
        }
    }// namespace Editor
}// namespace Ailu
