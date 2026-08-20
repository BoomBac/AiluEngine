#include "Editors/AudioClipEditor.h"
#include "Audio/Audio.h"
#include "Audio/AudioClip.h"
#include "Common/Undo.h"
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

        AudioClipEditor::AudioClipEditor() : AssetEditor("Audio Clip", Vector2f(780.0f, 420.0f))
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

        void AudioClipEditor::Open(AudioClip *clip)
        {
            if (clip == nullptr)
                return;
            AssetEditor::Open(ResourceMgr::Get().GetLinkedAsset(clip));
        }

        bool AudioClipEditor::OnOpen()
        {
            _clip = GetAssetObject<AudioClip>();
            if (_clip == nullptr)
                return false;
            _last_edit_snapshot = CaptureAssetObject(GetAsset());
            RefreshAllUI();
            return true;
        }

        void AudioClipEditor::OnClose()
        {
            StopPreview();
            _clip = nullptr;
        }

        void AudioClipEditor::OnAssetSaved()
        {
            _last_edit_snapshot = CaptureAssetObject(GetAsset());
            RefreshStatus();
        }

        void AudioClipEditor::OnAssetReloaded()
        {
            _clip = GetAssetObject<AudioClip>();
            _last_edit_snapshot = CaptureAssetObject(GetAsset());
            RefreshAllUI();
        }

        void AudioClipEditor::Update(f32 dt)
        {
            AssetEditor::Update(dt);
            if (!_clip)
                return;

            if (Input::IsKeyDownAccurate(EKey::kSPACE))
                PlayPreview();
            const bool ctrl = Input::IsKeyDown(EKey::kLCONTROL) || Input::IsKeyDown(EKey::kRCONTROL);
            if (ctrl && Input::IsKeyDownAccurate(EKey::kZ) && g_pCommandMgr != nullptr)
            {
                g_pCommandMgr->Undo();
                _last_edit_snapshot = CaptureAssetObject(GetAsset());
                RefreshAllUI();
            }
            if (ctrl && Input::IsKeyDownAccurate(EKey::kY) && g_pCommandMgr != nullptr)
            {
                g_pCommandMgr->Redo();
                _last_edit_snapshot = CaptureAssetObject(GetAsset());
                RefreshAllUI();
            }

            RefreshStatus();
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
            options._streaming = _clip->_load_mode == EAudioLoadMode::kStreaming;
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
            if (_clip == nullptr)
                return;
            StyleModeButton(_btn_memory, _clip->_load_mode == EAudioLoadMode::kMemory);
            StyleModeButton(_btn_streaming, _clip->_load_mode == EAudioLoadMode::kStreaming);
            StyleModeButton(_btn_auto, _clip->_channel_mode == EAudioChannelMode::kAuto);
            StyleModeButton(_btn_mono, _clip->_channel_mode == EAudioChannelMode::kMono);
            StyleModeButton(_btn_stereo, _clip->_channel_mode == EAudioChannelMode::kStereo);
            if (_chk_force_mono)
                _chk_force_mono->SetChecked(_clip->_force_mono);
        }

        void AudioClipEditor::RefreshStatus()
        {
            if (_txt_status)
                _txt_status->SetText(IsDirty() ? "Modified" : "Saved");
        }

        void AudioClipEditor::SetLoadMode(EAudioLoadMode mode)
        {
            if (_clip == nullptr || _clip->_load_mode == mode)
                return;
            _clip->_load_mode = mode;
            MarkEdited();
            RefreshImportSettings();
        }

        void AudioClipEditor::SetChannelMode(EAudioChannelMode mode)
        {
            if (_clip == nullptr || _clip->_channel_mode == mode)
                return;
            _clip->_channel_mode = mode;
            MarkEdited();
            RefreshImportSettings();
        }

        void AudioClipEditor::MarkEdited()
        {
            if (GetAsset() == nullptr)
                return;
            const String snapshot = CaptureAssetObject(GetAsset());
            if (snapshot == _last_edit_snapshot)
                return;
            const String before = _last_edit_snapshot;
            _last_edit_snapshot = snapshot;
            if (g_pCommandMgr != nullptr)
                g_pCommandMgr->ExecuteCommand(std::make_unique<AssetSnapshotCommand>(
                    GetAsset(), before, snapshot, "Audio Clip Edit"));
            else if (!GetAsset()->IsDirty())
                GetAsset()->MarkModified();
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

            auto *save_button = toolbar->AddChild<UI::Button>("Save");
            save_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(50.0f, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 8.0f, 0.0f));
            save_button->OnMouseClick() += [this](UI::UIEvent &e) { Save(); e._is_handled = true; };

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
                if (_clip != nullptr && _clip->_force_mono != _chk_force_mono->IsChecked())
                {
                    _clip->_force_mono = _chk_force_mono->IsChecked();
                    MarkEdited();
                }
                RefreshImportSettings();
                e._is_handled = true;
            };
        }
    }// namespace Editor
}// namespace Ailu
