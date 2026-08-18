#include "Editors/AnimationClipEditor.h"

#include "Assets/Asset.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "Render/2D/Sprite.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"

#include <algorithm>
#include <cmath>
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
            const Color kPanelColor = Color(0.16f, 0.17f, 0.19f, 1.0f);
            const Color kCenterColor = Color(0.12f, 0.13f, 0.14f, 1.0f);
            const Color kTextColor = Color(0.75f, 0.75f, 0.75f, 1.0f);
            const Color kMutedTextColor = Color(0.55f, 0.55f, 0.55f, 1.0f);

            UI::InputBlock *AddInputToRow(UI::HorizontalBox *row, const String &value)
            {
                auto *input = row->AddChild<UI::InputBlock>(value);
                input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                        .Margin(Vector4f(2.0f, 1.0f, 2.0f, 1.0f));
                return input;
            }

            void StyleText(UI::Text *text, Color color = kTextColor, f32 size = 11.0f)
            {
                text->_color = color;
                text->FontSize(size);
            }
        }

        AnimationClipEditor::AnimationClipEditor() : DockWindow("Animation Clip Editor", Vector2f(1120.0f, 700.0f))
        {
            SetPosition(Vector2f(100.0f, 50.0f));
            auto *root = _content_root->AddChild<UI::VerticalBox>();
            root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto *toolbar = root->AddChild<UI::HorizontalBox>();
            toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, kToolbarHeight));
            toolbar->SlotPadding() = UI::Padding(4.0f, 2.0f, 4.0f, 2.0f);
            auto add_button = [toolbar](const String &text, f32 width)
            {
                auto *button = toolbar->AddChild<UI::Button>(text);
                button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                        .Size(Vector2f(width, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 2.0f, 0.0f));
                return button;
            };
            _btn_apply = add_button("Apply", 56.0f);
            _btn_apply->OnMouseClick() += [this](UI::UIEvent &e) { Apply(); e._is_handled = true; };
            _btn_revert = add_button("Revert", 58.0f);
            _btn_revert->OnMouseClick() += [this](UI::UIEvent &e) { Revert(); e._is_handled = true; };
            auto *add_frame = add_button("+ Frame", 68.0f);
            add_frame->OnMouseClick() += [this](UI::UIEvent &e) { AddFrame(); e._is_handled = true; };
            auto *add_event = add_button("+ Event", 66.0f);
            add_event->OnMouseClick() += [this](UI::UIEvent &e) { AddEvent(); e._is_handled = true; };
            _btn_play = add_button("Play", 52.0f);
            _btn_play->OnMouseClick() += [this](UI::UIEvent &e)
            {
                _is_previewing = !_is_previewing;
                _btn_play->SetText(_is_previewing ? "Pause" : "Play");
                e._is_handled = true;
            };
            _btn_stop = add_button("Stop", 52.0f);
            _btn_stop->OnMouseClick() += [this](UI::UIEvent &e)
            {
                _is_previewing = false;
                _preview_time = 0.0f;
                _btn_play->SetText("Play");
                RefreshPreview();
                e._is_handled = true;
            };
            _txt_status = toolbar->AddChild<UI::Text>("Saved");
            StyleText(_txt_status, kMutedTextColor);
            _txt_status->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                    .Margin(Vector4f(8.0f, 0.0f, 0.0f, 0.0f));

            auto *main = root->AddChild<UI::SplitView>();
            main->_is_horizontal = true;
            main->SetRatio(0.28f);
            main->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto *info_border = main->AddChild<UI::Border>();
            info_border->_bg_color = kPanelColor;
            auto *info_scroll = info_border->AddChild<UI::ScrollView>();
            info_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            auto *info = info_scroll->AddChild<UI::VerticalBox>();
            info->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            info->SlotPadding() = UI::Padding(5.0f);
            AddSectionTitle(info, "Clip");
            _input_name = AddTextInput(info, "Name", "", [this](String value)
            {
                _editing_name = std::move(value);
                MarkDirty();
            });
            _input_duration = AddFloatInput(info, "Duration", 0.0f, [this](f32 value)
            {
                _editing_duration = std::max(value, 0.0f);
                MarkDirty();
            });
            _input_rate = AddFloatInput(info, "Frame Rate", 30.0f, [this](f32 value)
            {
                _editing_frame_rate = std::max(value, 0.0f);
                MarkDirty();
            });
            _input_frame_duration = AddFloatInput(info, "Frame Duration", 0.0f, [this](f32 value)
            {
                _editing_frame_duration = std::max(value, 0.0f);
                MarkDirty();
            });
            auto *loop_row = AddPropertyRow(info, "Looping");
            _check_looping = loop_row->AddChild<UI::CheckBox>();
            _check_looping->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(28.0f, 0.0f));
            _check_looping->_on_click += [this](bool value)
            {
                _editing_looping = value;
                MarkDirty();
            };
            AddSectionTitle(info, "Usage");
            auto *usage = info->AddChild<UI::Text>("Edit sprite frames and events in the timeline.\nApply saves JSON.");
            StyleText(usage, kMutedTextColor);
            usage->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, 42.0f)).Margin(Vector4f(4.0f, 2.0f, 4.0f, 2.0f));

            auto *right = main->AddChild<UI::SplitView>();
            right->_is_horizontal = true;
            right->SetRatio(0.70f);
            auto *timeline_border = right->AddChild<UI::Border>();
            timeline_border->_bg_color = kCenterColor;
            auto *timeline_scroll = timeline_border->AddChild<UI::ScrollView>();
            timeline_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _timeline_root = timeline_scroll->AddChild<UI::VerticalBox>();
            _timeline_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            _timeline_root->SlotPadding() = UI::Padding(5.0f);

            auto *events_border = right->AddChild<UI::Border>();
            events_border->_bg_color = kPanelColor;
            auto *events_scroll = events_border->AddChild<UI::ScrollView>();
            events_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _events_root = events_scroll->AddChild<UI::VerticalBox>();
            _events_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            _events_root->SlotPadding() = UI::Padding(5.0f);
        }

        void AnimationClipEditor::Update(f32 dt)
        {
            DockWindow::Update(dt);
            if (!_clip)
                return;
            if (_is_previewing)
            {
                if (_editing_duration <= 0.0f)
                {
                    _is_previewing = false;
                    if (_btn_play)
                        _btn_play->SetText("Play");
                }
                else
                {
                    _preview_time += std::max(dt, 0.0f);
                    if (_editing_looping)
                        _preview_time = std::fmod(_preview_time, _editing_duration);
                    else if (_preview_time >= _editing_duration)
                    {
                        _preview_time = _editing_duration;
                        _is_previewing = false;
                        if (_btn_play)
                            _btn_play->SetText("Play");
                    }
                    RefreshPreview();
                }
            }
            const bool ctrl = Input::IsKeyDown(EKey::kLCONTROL) || Input::IsKeyDown(EKey::kRCONTROL);
            if (ctrl && Input::IsKeyPressed(EKey::kS))
                Apply();
            if (_btn_apply)
                _btn_apply->SetInteractiveEnabled(_is_dirty);
            if (_btn_revert)
                _btn_revert->SetInteractiveEnabled(_is_dirty);
        }

        void AnimationClipEditor::Open(AnimationClip *clip)
        {
            if (clip == nullptr)
                return;
            _clip = clip;
            ReadFromAsset();
            _original_name = _editing_name;
            _original_frame_count = _editing_frame_count;
            _original_duration = _editing_duration;
            _original_frame_rate = _editing_frame_rate;
            _original_frame_duration = _editing_frame_duration;
            _original_looping = _editing_looping;
            _original_frames = _editing_frames;
            _original_events = _editing_events;
            _is_dirty = false;
            _is_previewing = false;
            _preview_time = 0.0f;
            _preview_sprite.reset();
            _preview_sprite_guid = Guid::EmptyGuid();
            SetTitle("Animation Clip Editor - " + clip->Name());
            RefreshAllUI();
        }

        void AnimationClipEditor::Close()
        {
            _clip = nullptr;
            _is_previewing = false;
            _preview_time = 0.0f;
            _preview_sprite.reset();
            _preview_sprite_guid = Guid::EmptyGuid();
            _editing_frames.clear();
            _editing_events.clear();
        }

        void AnimationClipEditor::ReadFromAsset()
        {
            _editing_name = _clip->Name();
            _editing_frame_count = _clip->FrameCount();
            _editing_duration = _clip->Duration();
            _editing_frame_rate = _clip->FrameRate();
            _editing_frame_duration = _clip->FrameDuration();
            _editing_looping = _clip->IsLooping();
            _editing_frames = _clip->SpriteTrack().Frames();
            _editing_events = _clip->Events();
        }

        void AnimationClipEditor::WriteToAsset()
        {
            _clip->Name(_editing_name);
            _clip->FrameCount(_editing_frame_count);
            _clip->Duration(_editing_duration);
            _clip->FrameRate(_editing_frame_rate);
            _clip->FrameDuration(_editing_frame_duration);
            _clip->IsLooping(_editing_looping);
            _clip->SpriteTrack().Frames().clear();
            for (const auto &frame : _editing_frames)
                _clip->SpriteTrack().AddFrame(frame);
            _clip->Events() = _editing_events;
            std::sort(_clip->Events().begin(), _clip->Events().end(),
                      [](const AnimationEvent &lhs, const AnimationEvent &rhs) { return lhs._time < rhs._time; });
        }

        void AnimationClipEditor::Apply()
        {
            if (_clip == nullptr || !_is_dirty)
                return;
            WriteToAsset();
            if (auto *linked = ResourceMgr::Get().GetLinkedAsset(_clip))
                ResourceMgr::Get().SaveAsset(linked);
            _original_name = _editing_name;
            _original_frame_count = _editing_frame_count;
            _original_duration = _editing_duration;
            _original_frame_rate = _editing_frame_rate;
            _original_frame_duration = _editing_frame_duration;
            _original_looping = _editing_looping;
            _original_frames = _editing_frames;
            _original_events = _editing_events;
            _is_dirty = false;
            SetTitle("Animation Clip Editor - " + _clip->Name());
            RefreshAllUI();
            LOG_INFO("AnimationClipEditor: Applied");
        }

        void AnimationClipEditor::Revert()
        {
            if (!_is_dirty)
                return;
            _editing_name = _original_name;
            _editing_frame_count = _original_frame_count;
            _editing_duration = _original_duration;
            _editing_frame_rate = _original_frame_rate;
            _editing_frame_duration = _original_frame_duration;
            _editing_looping = _original_looping;
            _editing_frames = _original_frames;
            _editing_events = _original_events;
            _is_dirty = false;
            _preview_time = 0.0f;
            _is_previewing = false;
            RefreshAllUI();
        }

        void AnimationClipEditor::MarkDirty()
        {
            _is_dirty = true;
            if (_txt_status)
                _txt_status->SetText("Modified");
        }

        void AnimationClipEditor::RefreshAllUI()
        {
            if (_input_name)
                _input_name->SetContent(_editing_name, false);
            if (_input_duration)
                _input_duration->SetContent(std::format("{:.3f}", _editing_duration), false);
            if (_input_rate)
                _input_rate->SetContent(std::format("{:.3f}", _editing_frame_rate), false);
            if (_input_frame_duration)
                _input_frame_duration->SetContent(std::format("{:.3f}", _editing_frame_duration), false);
            if (_check_looping)
                _check_looping->SetChecked(_editing_looping);
            if (_btn_play)
                _btn_play->SetText(_is_previewing ? "Pause" : "Play");
            if (_txt_status)
                _txt_status->SetText(_is_dirty ? "Modified" : "Saved");
            RefreshTimeline();
            RefreshEvents();
        }

        void AnimationClipEditor::RefreshTimeline()
        {
            if (_timeline_root == nullptr)
                return;
            _timeline_root->ClearChildren();
            _preview_image = nullptr;
            _txt_preview_time = nullptr;
            AddSectionTitle(_timeline_root, std::format("Sprite Timeline ({} frames)", _editing_frames.size()));
            auto *help = _timeline_root->AddChild<UI::Text>("Time is in seconds. Choose a loaded Sprite for each key frame.");
            StyleText(help, kMutedTextColor);
            help->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, 20.0f));
            auto *preview = _timeline_root->AddChild<UI::HorizontalBox>();
            preview->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, 150.0f)).Margin(Vector4f(0.0f, 3.0f, 0.0f, 5.0f));
            _preview_image = preview->AddChild<UI::Image>();
            _preview_image->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(150.0f, 0.0f)).Margin(Vector4f(2.0f, 2.0f, 2.0f, 2.0f));
            auto *preview_info = preview->AddChild<UI::VerticalBox>();
            preview_info->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                    .Margin(Vector4f(8.0f, 8.0f, 4.0f, 4.0f));
            _txt_preview_time = preview_info->AddChild<UI::Text>("Preview 0.000s");
            StyleText(_txt_preview_time, Color(0.88f, 0.88f, 0.88f, 1.0f), 13.0f);
            preview_info->AddChild<UI::Text>("Play/Pause previews the edited sprite track.");
            StyleText(preview_info->AddChild<UI::Text>("Atlas UV is used by runtime rendering; the editor preview shows the source texture."),
                      kMutedTextColor);
            RefreshPreview();
            for (u32 index = 0u; index < _editing_frames.size(); ++index)
            {
                auto &frame = _editing_frames[index];
                auto *row = _timeline_root->AddChild<UI::HorizontalBox>();
                row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                        .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(0.0f, 1.0f, 0.0f, 1.0f));
                auto *label = row->AddChild<UI::Text>(std::format("{}", index + 1u));
                StyleText(label, kMutedTextColor);
                label->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                        .Size(Vector2f(28.0f, 0.0f));
                auto *time_input = AddInputToRow(row, std::format("{:.3f}", frame._time));
                time_input->GetSlotAs<UI::LinearSlot>().FillRate(1.0f);
                time_input->_on_content_changed += [this, index](String value)
                {
                    if (index >= _editing_frames.size())
                        return;
                    if (auto parsed = StringUtils::ParseFloat(value); parsed.has_value())
                    {
                        _editing_frames[index]._time = std::max(parsed.value(), 0.0f);
                        MarkDirty();
                    }
                };
                auto *sprite_button = row->AddChild<UI::Button>("None");
                sprite_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                        .FillRate(3.0f).Margin(Vector4f(2.0f, 1.0f, 2.0f, 1.0f));
                if (!frame._sprite.IsEmpty())
                {
                    auto sprite = ResourceMgr::Get().Load<Render::Sprite>(frame._sprite);
                    if (sprite != nullptr)
                        sprite_button->SetText(sprite->Name());
                    else
                        sprite_button->SetText(frame._sprite.ToString());
                }
                sprite_button->OnMouseClick() += [this, index](UI::UIEvent &event)
                {
                    ShowSpritePicker(index, event._current_target);
                    event._is_handled = true;
                };
                auto *remove = row->AddChild<UI::Button>("X");
                remove->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                        .Size(Vector2f(24.0f, 0.0f));
                remove->OnMouseClick() += [this, index](UI::UIEvent &event)
                {
                    RemoveFrame(index);
                    event._is_handled = true;
                };
            }
        }

        void AnimationClipEditor::RefreshPreview()
        {
            if (_preview_image == nullptr)
                return;
            Guid sprite_guid = Guid::EmptyGuid();
            if (!_editing_frames.empty())
            {
                f32 sample_time = _preview_time;
                if (_editing_looping && _editing_duration > 0.0f)
                    sample_time = std::fmod(sample_time, _editing_duration);
                for (const auto &frame : _editing_frames)
                {
                    if (frame._time > sample_time)
                        break;
                    sprite_guid = frame._sprite;
                }
                if (sprite_guid.IsEmpty())
                    sprite_guid = _editing_frames.front()._sprite;
            }
            if (sprite_guid != _preview_sprite_guid)
            {
                _preview_sprite_guid = sprite_guid;
                _preview_sprite = sprite_guid.IsEmpty() ? Ref<Render::Sprite>() :
                                                           ResourceMgr::Get().GetRef<Render::Sprite>(sprite_guid);
                if (_preview_sprite == nullptr && !sprite_guid.IsEmpty())
                    _preview_sprite = ResourceMgr::Get().Load<Render::Sprite>(sprite_guid);
            }
            const auto &sprite = _preview_sprite;
            _preview_image->SetTexture(sprite != nullptr && sprite->_texture != nullptr ? sprite->_texture.get() : nullptr);
            _preview_image->_uv_rect = sprite != nullptr ? sprite->_uv_rect : Vector4f(0.0f, 0.0f, 1.0f, 1.0f);
            _preview_image->InvalidatePaint();
            if (_txt_preview_time != nullptr)
                _txt_preview_time->SetText(std::format("Preview {:.3f}s{}", _preview_time,
                                                        sprite != nullptr ? " - " + sprite->Name() : " - None"));
        }

        void AnimationClipEditor::RefreshEvents()
        {
            if (_events_root == nullptr)
                return;
            _events_root->ClearChildren();
            AddSectionTitle(_events_root, std::format("Events ({})", _editing_events.size()));
            auto *help = _events_root->AddChild<UI::Text>("Event ID is delivered to the animation event queue.");
            StyleText(help, kMutedTextColor);
            help->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, 34.0f));
            for (u32 index = 0u; index < _editing_events.size(); ++index)
            {
                auto &event = _editing_events[index];
                AddFloatInput(_events_root, std::format("{} Time", index + 1u), event._time, [this, index](f32 value)
                {
                    if (index < _editing_events.size())
                    {
                        _editing_events[index]._time = std::max(value, 0.0f);
                        MarkDirty();
                    }
                });
                auto *id_input = AddTextInput(_events_root, "Event ID", std::to_string(event._event_id), [this, index](String value)
                {
                    if (index < _editing_events.size())
                    {
                        if (auto parsed = StringUtils::ParseFloat(value); parsed.has_value())
                            _editing_events[index]._event_id = static_cast<u32>(std::max(parsed.value(), 0.0f));
                        MarkDirty();
                    }
                });
                (void)id_input;
                auto *kind_row = AddPropertyRow(_events_root, "Kind");
                auto *kind = kind_row->AddChild<UI::Dropdown>(Vector<String>{"Gameplay", "Cosmetic"});
                kind->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
                kind->SetSelectedIndex(event._kind == EAnimationEventKind::kGameplay ? 0 : 1);
                kind->_on_selected_changed += [this, index](i32 selected)
                {
                    if (index < _editing_events.size())
                    {
                        _editing_events[index]._kind = selected == 0 ? EAnimationEventKind::kGameplay : EAnimationEventKind::kCosmetic;
                        MarkDirty();
                    }
                };
                auto *remove = _events_root->AddChild<UI::Button>("Remove Event");
                remove->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                        .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(0.0f, 0.0f, 0.0f, 5.0f));
                remove->OnMouseClick() += [this, index](UI::UIEvent &event_value)
                {
                    RemoveEvent(index);
                    event_value._is_handled = true;
                };
            }
        }

        void AnimationClipEditor::AddFrame()
        {
            SpriteKeyFrame frame;
            frame._time = _editing_frames.empty() ? 0.0f : _editing_frames.back()._time +
                                                       (_editing_frame_duration > 0.0f ? _editing_frame_duration : 0.1f);
            if (!_editing_frames.empty())
                frame._sprite = _editing_frames.back()._sprite;
            _editing_frames.push_back(frame);
            _editing_frame_count = static_cast<u32>(_editing_frames.size());
            if (_editing_duration < frame._time)
                _editing_duration = frame._time;
            MarkDirty();
            RefreshAllUI();
        }

        void AnimationClipEditor::AddEvent()
        {
            _editing_events.push_back(AnimationEvent{0.0f, 0u, EAnimationEventKind::kCosmetic});
            MarkDirty();
            RefreshEvents();
        }

        void AnimationClipEditor::RemoveFrame(u32 index)
        {
            if (index >= _editing_frames.size())
                return;
            _editing_frames.erase(_editing_frames.begin() + index);
            _editing_frame_count = static_cast<u32>(_editing_frames.size());
            MarkDirty();
            RefreshTimeline();
        }

        void AnimationClipEditor::RemoveEvent(u32 index)
        {
            if (index >= _editing_events.size())
                return;
            _editing_events.erase(_editing_events.begin() + index);
            MarkDirty();
            RefreshEvents();
        }

        void AnimationClipEditor::ShowSpritePicker(u32 frame_index, UI::UIElement *anchor)
        {
            auto list = MakeRef<UI::ListView>();
            list->SetViewportHeight(220.0f);
            auto none = MakeRef<UI::Text>("None");
            none->OnMouseClick() += [this, frame_index](UI::UIEvent &)
            {
                if (frame_index < _editing_frames.size())
                    _editing_frames[frame_index]._sprite = Guid::EmptyGuid();
                MarkDirty();
                UIManager::Get()->HidePopup();
                RefreshTimeline();
            };
            list->AddItem(none);
            for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
            {
                Asset *asset = it->second.get();
                if (asset == nullptr || asset->_asset_type != Render::Sprite::StaticType())
                    continue;
                auto sprite = ResourceMgr::Get().Load<Render::Sprite>(asset->_asset_path);
                if (!sprite)
                    continue;
                auto item = MakeRef<UI::Text>(sprite->Name());
                const Guid guid = asset->GetGuid();
                item->OnMouseClick() += [this, frame_index, guid](UI::UIEvent &)
                {
                    if (frame_index < _editing_frames.size())
                        _editing_frames[frame_index]._sprite = guid;
                    MarkDirty();
                    UIManager::Get()->HidePopup();
                    RefreshTimeline();
                };
                list->AddItem(item);
            }
            const auto rect = anchor->GetArrangeRect();
            UIManager::Get()->ShowPopupAt(rect.x, rect.y + rect.w, list);
        }

        UI::Text *AnimationClipEditor::AddSectionTitle(UI::UIElement *parent, const String &title)
        {
            auto *text = parent->AddChild<UI::Text>(title);
            StyleText(text, Color(0.88f, 0.88f, 0.88f, 1.0f), 13.0f);
            text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, 25.0f)).Margin(Vector4f(3.0f, 4.0f, 0.0f, 2.0f));
            return text;
        }

        UI::HorizontalBox *AnimationClipEditor::AddPropertyRow(UI::UIElement *parent, const String &label)
        {
            auto *row = parent->AddChild<UI::HorizontalBox>();
            row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(0.0f, 1.0f, 0.0f, 1.0f));
            auto *text = row->AddChild<UI::Text>(label);
            StyleText(text, kMutedTextColor);
            text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(100.0f, 0.0f));
            return row;
        }

        UI::InputBlock *AnimationClipEditor::AddTextInput(UI::UIElement *parent, const String &label,
                                                           const String &value,
                                                           const std::function<void(String)> &on_changed)
        {
            auto *input = AddInputToRow(AddPropertyRow(parent, label), value);
            input->_on_content_changed += std::function<void(String)>(on_changed);
            return input;
        }

        UI::InputBlock *AnimationClipEditor::AddFloatInput(UI::UIElement *parent, const String &label, f32 value,
                                                            const std::function<void(f32)> &on_changed)
        {
            return AddTextInput(parent, label, std::format("{:.3f}", value), [on_changed](String content)
            {
                if (auto parsed = StringUtils::ParseFloat(content); parsed.has_value())
                    on_changed(parsed.value());
            });
        }
    }// namespace Editor
}// namespace Ailu
