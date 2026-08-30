#include "Editors/AnimationClipEditor.h"

#include "Assets/Asset.h"
#include "Common/Undo.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "Render/2D/Sprite.h"
#include "Render/Mesh.h"
#include "UI/ObjectAssetDropdown.h"
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

        AnimationClipEditor::AnimationClipEditor() : AssetEditor("Animation Clip Editor", Vector2f(1120.0f, 700.0f))
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
            auto *save_button = add_button("Save", 50.0f);
            save_button->OnMouseClick() += [this](UI::UIEvent &e) { AssetEditor::Save(); e._is_handled = true; };
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
                if (_clip != nullptr)
                    _clip->Name(value);
                MarkDirty();
            });
            _input_duration = AddFloatInput(info, "Duration", 0.0f, [this](f32 value)
            {
                if (_clip != nullptr)
                    _clip->Duration(std::max(value, 0.0f));
                MarkDirty();
            });
            _input_rate = AddFloatInput(info, "Frame Rate", 30.0f, [this](f32 value)
            {
                if (_clip != nullptr)
                    _clip->FrameRate(std::max(value, 0.0f));
                MarkDirty();
            });
            _input_frame_duration = AddFloatInput(info, "Frame Duration", 0.0f, [this](f32 value)
            {
                if (_clip != nullptr)
                    _clip->FrameDuration(std::max(value, 0.0f));
                MarkDirty();
            });
            auto *loop_row = AddPropertyRow(info, "Looping");
            _check_looping = loop_row->AddChild<UI::CheckBox>();
            _check_looping->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(28.0f, 0.0f));
            _check_looping->_on_click += [this](bool value)
            {
                if (_clip != nullptr)
                    _clip->IsLooping(value);
                MarkDirty();
            };
            auto *preview_mesh_row = AddPropertyRow(info, "Preview Mesh");
            _preview_mesh_dropdown =
                preview_mesh_row->AddChild<UI::ObjectAssetDropdown>(Render::SkeletonMesh::StaticType());
            _preview_mesh_dropdown->SetObjectType(Render::SkeletonMesh::StaticType());
            _preview_mesh_dropdown->SetAllowNone(true);
            _preview_mesh_dropdown->GetSlotAs<UI::LinearSlot>()
                    .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                    .Margin(Vector4f(2.0f, 1.0f, 2.0f, 1.0f));
            _preview_mesh_dropdown->_on_object_asset_selected += [this](Asset *, Object *object, const Guid &guid)
            {
                _preview_mesh_guid = guid;
                if (_clip != nullptr)
                    _clip->PreviewMeshGuid(guid);
                _preview_mesh.reset();
                if (auto *mesh = dynamic_cast<Render::SkeletonMesh *>(object); mesh != nullptr)
                    _preview_mesh = ResourceMgr::Get().GetRef<Render::SkeletonMesh>(guid);
                MarkDirty();
                RefreshPreview();
                RefreshTimeline();
            };
            auto *skeleton_row = AddPropertyRow(info, "Show Skeleton");
            _check_skeleton = skeleton_row->AddChild<UI::CheckBox>();
            _check_skeleton->GetSlotAs<UI::LinearSlot>()
                    .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(28.0f, 0.0f));
            _check_skeleton->_on_click += [this](bool value)
            {
                _show_skeleton = value;
                if (_animation_preview != nullptr)
                    _animation_preview->SetShowSkeleton(value);
            };
            AddSectionTitle(info, "Usage");
            auto *usage = info->AddChild<UI::Text>("Edit sprite frames and events in the timeline.\nSave writes JSON.");
            StyleText(usage, kMutedTextColor);
            usage->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, 42.0f)).Margin(Vector4f(4.0f, 2.0f, 4.0f, 2.0f));

            auto *right = main->AddChild<UI::SplitView>();
            right->_is_horizontal = true;
            right->SetRatio(0.70f);
            auto *timeline_border = right->AddChild<UI::Border>();
            timeline_border->_bg_color = kCenterColor;
            _timeline_root = timeline_border->AddChild<UI::VerticalBox>();
            _timeline_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _timeline_root->SlotPadding() = UI::Padding(5.0f);

            auto *events_border = right->AddChild<UI::Border>();
            events_border->_bg_color = kPanelColor;
            auto *events_scroll = events_border->AddChild<UI::ScrollView>();
            events_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _events_root = events_scroll->AddChild<UI::VerticalBox>();
            _events_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            _events_root->SlotPadding() = UI::Padding(5.0f);
            _animation_preview = MakeScope<AnimationClipPreview>();
        }

        void AnimationClipEditor::OnAssetReloaded()
        {
            _clip = GetAssetObject<AnimationClip>();
            if (_clip != nullptr)
                OnOpen();
        }

        void AnimationClipEditor::OnBeforeSave()
        {
            if (_clip == nullptr)
                return;
            std::sort(Events().begin(), Events().end(),
                      [](const AnimationEvent &lhs, const AnimationEvent &rhs) { return lhs._time < rhs._time; });
        }

        void AnimationClipEditor::OnAssetSaved()
        {
            _last_edit_snapshot = CaptureAssetObject(GetAsset());
            RefreshAllUI();
        }

        void AnimationClipEditor::Update(f32 dt)
        {
            AssetEditor::Update(dt);
            if (_animation_preview != nullptr)
            {
                if (!Input::IsKeyDownAccurate(EKey::kLBUTTON))
                    _animation_preview->EndCameraDrag();
                if (!Input::IsKeyDownAccurate(EKey::kRBUTTON))
                    _animation_preview->EndCameraPan();
            }
            if (!_clip)
                return;
            if (_animation_preview != nullptr && _preview_image != nullptr)
            {
                const Vector4f preview_rect = _preview_image->GetArrangeRect();
                if (preview_rect.z > 2.0f && preview_rect.w > 2.0f &&
                    _animation_preview->SetViewportSize(preview_rect.zw))
                {
                    RefreshPreview();
                }
            }
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
            if (_is_previewing)
            {
                if (_clip->Duration() <= 0.0f)
                {
                    _is_previewing = false;
                    if (_btn_play)
                        _btn_play->SetText("Play");
                }
                else
                {
                    _preview_time += std::max(dt, 0.0f);
                    if (_clip->IsLooping())
                        _preview_time = std::fmod(_preview_time, _clip->Duration());
                    else if (_preview_time >= _clip->Duration())
                    {
                        _preview_time = _clip->Duration();
                        _is_previewing = false;
                        if (_btn_play)
                            _btn_play->SetText("Play");
                    }
                    if (_animation_timeline)
                        _animation_timeline->SetCurrentTime(_preview_time);
                    else
                        RefreshPreview();
                }
            }
            if (_animation_preview != nullptr && _preview_image != nullptr && _animation_preview->IsRenderPending())
            {
                _animation_preview->RenderIfPending();
                _preview_image->SetTexture(_animation_preview->GetRenderTexture());
            }
        }

        bool AnimationClipEditor::OnOpen()
        {
            _clip = GetAssetObject<AnimationClip>();
            if (_clip == nullptr)
                return false;
            _is_previewing = false;
            _preview_time = 0.0f;
            _preview_sprite.reset();
            _preview_sprite_guid = Guid::EmptyGuid();
            _preview_mesh.reset();
            _preview_mesh_guid = _clip->PreviewMeshGuid();
            _show_skeleton = false;
            _selected_joint = Joint::kInvalidJointIndex;
            if (_animation_preview)
            {
                _animation_preview->SetClip(_clip);
                _animation_preview->SetMesh(nullptr);
                _animation_preview->SetShowSkeleton(false);
                _animation_preview->SetSelectedJoint(_selected_joint);
            }
            _last_edit_snapshot = CaptureAssetObject(GetAsset());
            RefreshAllUI();
            return true;
        }

        void AnimationClipEditor::OnClose()
        {
            _clip = nullptr;
            _is_previewing = false;
            _preview_time = 0.0f;
            _preview_sprite.reset();
            _preview_sprite_guid = Guid::EmptyGuid();
            _preview_mesh.reset();
            _preview_mesh_guid = Guid::EmptyGuid();
            _show_skeleton = false;
            _selected_joint = Joint::kInvalidJointIndex;
            if (_animation_preview)
            {
                _animation_preview->SetClip(nullptr);
                _animation_preview->SetMesh(nullptr);
                _animation_preview->SetShowSkeleton(false);
                _animation_preview->SetSelectedJoint(_selected_joint);
            }
        }

        void AnimationClipEditor::MarkDirty()
        {
            if (_is_refreshing_ui)
                return;
            // AnimationClip is stored through AnimationClipAssetDocument and its runtime fields are
            // intentionally not reflected.  The generic object snapshot therefore cannot detect edits.
            if (Asset *asset = GetAsset(); asset != nullptr && !asset->IsDirty())
                asset->MarkModified();
            if (_txt_status)
                _txt_status->SetText("Modified");
        }

        void AnimationClipEditor::RefreshAllUI()
        {
            _is_refreshing_ui = true;
            if (_input_name)
                _input_name->SetContent(_clip != nullptr ? _clip->Name() : String{}, false);
            if (_input_duration)
                _input_duration->SetContent(std::format("{:.3f}", _clip != nullptr ? _clip->Duration() : 0.0f), false);
            if (_input_rate)
                _input_rate->SetContent(std::format("{:.3f}", _clip != nullptr ? _clip->FrameRate() : 0.0f), false);
            if (_input_frame_duration)
                _input_frame_duration->SetContent(
                    std::format("{:.3f}", _clip != nullptr ? _clip->FrameDuration() : 0.0f), false);
            if (_check_looping)
                _check_looping->SetChecked(_clip != nullptr && _clip->IsLooping());
            if (_check_skeleton)
                _check_skeleton->SetChecked(_show_skeleton);
            if (_clip != nullptr && _preview_mesh_guid != _clip->PreviewMeshGuid() && !_preview_mesh_guid.IsEmpty())
                _clip->PreviewMeshGuid(_preview_mesh_guid);
            if (_preview_mesh_dropdown)
                _preview_mesh_dropdown->SetSelectedGuid(_preview_mesh_guid, false);
            if (_btn_play)
                _btn_play->SetText(_is_previewing ? "Pause" : "Play");
            if (_txt_status)
                _txt_status->SetText(IsDirty() ? "Modified" : "Saved");
            if (_animation_preview)
            {
                _animation_preview->SetClip(_clip);
                if (_preview_mesh == nullptr && !_preview_mesh_guid.IsEmpty())
                {
                    _preview_mesh = ResourceMgr::Get().GetRef<Render::SkeletonMesh>(_preview_mesh_guid);
                    if (_preview_mesh == nullptr)
                        _preview_mesh = ResourceMgr::Get().Load<Render::SkeletonMesh>(_preview_mesh_guid);
                }
                _animation_preview->SetMesh(_preview_mesh.get());
            }
            RefreshTimeline();
            RefreshEvents();
            _is_refreshing_ui = false;
        }

        void AnimationClipEditor::RefreshTimeline()
        {
            if (_timeline_root == nullptr)
                return;
            _animation_timeline = nullptr;
            _timeline_root->ClearChildren();
            _preview_image = nullptr;
            _txt_preview_time = nullptr;
            AddSectionTitle(_timeline_root, _clip != nullptr && _clip->Size() > 0u ?
                                             std::format("Animation Timeline ({} joints)", _clip->Size()) :
                                             std::format("Sprite Timeline ({} frames)", Frames().size()));
            auto *help = _timeline_root->AddChild<UI::Text>(
                "Scrub the playhead to preview the sampled pose. Sprite frames remain editable below.");
            StyleText(help, kMutedTextColor);
            help->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, 20.0f));
            auto *preview_timeline_split = _timeline_root->AddChild<UI::SplitView>();
            preview_timeline_split->_is_horizontal = false;
            preview_timeline_split->SetRatio(0.40f);
            preview_timeline_split->GetSlotAs<UI::LinearSlot>()
                    .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                    .Margin(Vector4f(0.0f, 3.0f, 0.0f, 3.0f));
            auto *preview_area = preview_timeline_split->AddChild<UI::VerticalBox>();
            auto *preview_canvas = preview_area->AddChild<UI::Border>();
            preview_canvas->_bg_color = Color(0.06f, 0.07f, 0.08f, 1.0f);
            preview_canvas->_border_color = Color(0.30f, 0.34f, 0.40f, 1.0f);
            preview_canvas->Thickness(1.0f);
            preview_canvas->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                    .Margin(Vector4f(0.0f, 0.0f, 0.0f, 3.0f));
            _preview_image = preview_canvas->AddChild<UI::Image>();
            _preview_image->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                    .Margin(Vector4f(3.0f, 3.0f, 3.0f, 3.0f));
            _preview_image->SetWantsMouseEvents(true);
            _preview_image->SetInteractiveEnabled(true);
            _preview_image->OnMouseDown() += [this](UI::UIEvent &event)
            {
                if (_preview_image == nullptr || _animation_preview == nullptr)
                    return;
                const Vector4f rect = _preview_image->GetArrangeRect();
                if (event._key_code == EKey::kLBUTTON)
                {
                    _preview_mouse_down_position = event._mouse_position;
                    _preview_camera_dragged = false;
                    _animation_preview->BeginCameraDrag(event._mouse_position - rect.xy);
                }
                else if (event._key_code == EKey::kRBUTTON)
                    _animation_preview->BeginCameraPan(event._mouse_position - rect.xy);
                event._is_handled = true;
            };
            _preview_image->OnMouseUp() += [this](UI::UIEvent &event)
            {
                if (_animation_preview == nullptr)
                    return;
                if (event._key_code == EKey::kLBUTTON)
                {
                    _animation_preview->EndCameraDrag();
                    if (!_preview_camera_dragged && _preview_mesh != nullptr && _clip != nullptr && _clip->Size() > 0u)
                    {
                        const Vector4f rect = _preview_image->GetArrangeRect();
                        u16 joint_index = Joint::kInvalidJointIndex;
                        _animation_preview->PickJoint(event._mouse_position - rect.xy, joint_index);
                        SelectJoint(joint_index);
                    }
                }
                else if (event._key_code == EKey::kRBUTTON)
                    _animation_preview->EndCameraPan();
                event._is_handled = true;
            };
            _preview_image->OnMouseMove() += [this](UI::UIEvent &event)
            {
                if (_animation_preview == nullptr || _preview_image == nullptr)
                    return;
                const Vector4f rect = _preview_image->GetArrangeRect();
                const Vector2f local_position = event._mouse_position - rect.xy;
                if (Input::IsKeyDown(EKey::kLBUTTON))
                {
                    const Vector2f delta = event._mouse_position - _preview_mouse_down_position;
                    _preview_camera_dragged = _preview_camera_dragged ||
                                              delta.x * delta.x + delta.y * delta.y > 16.0f;
                }
                _animation_preview->DragCamera(local_position);
                _animation_preview->PanCamera(local_position);
                event._is_handled = true;
            };
            _preview_image->OnMouseScroll() += [this](UI::UIEvent &event)
            {
                if (_animation_preview == nullptr)
                    return;
                _animation_preview->ZoomCamera(event._scroll_delta);
                event._is_handled = true;
            };
            auto *preview_info = preview_area->AddChild<UI::VerticalBox>();
            preview_info->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, 58.0f));
            _txt_preview_time = preview_info->AddChild<UI::Text>("Preview 0.000s");
            StyleText(_txt_preview_time, Color(0.88f, 0.88f, 0.88f, 1.0f), 13.0f);
            preview_info->AddChild<UI::Text>("Play/Pause and scrub the timeline to preview the clip.");
            StyleText(preview_info->AddChild<UI::Text>(
                          "Skeleton previews use a private mesh copy; sprite previews use atlas UV."),
                      kMutedTextColor);
            auto *timeline_area = preview_timeline_split->AddChild<UI::VerticalBox>();
            if (_animation_preview != nullptr && _clip != nullptr && _clip->Size() > 0u)
            {
                _animation_timeline = timeline_area->AddChild<AnimationTimeline>();
                _animation_timeline->GetSlotAs<UI::LinearSlot>()
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                        .Margin(Vector4f(0.0f, 0.0f, 0.0f, 0.0f));
                _animation_timeline->SetClip(_clip);
                _animation_timeline->SetSkeleton(_preview_mesh != nullptr && _preview_mesh->GetSkeletonAsset().IsResolved() ?
                                                     &_preview_mesh->GetSkeletonAsset()->GetSkeleton() : nullptr);
                _animation_timeline->SetCurrentTime(_preview_time);
                _animation_timeline->_on_time_changed += [this](f32 time)
                {
                    _preview_time = time;
                    RefreshPreview();
                };
                _animation_timeline->_on_bone_selected += [this](u16 joint_index)
                {
                    SelectJoint(joint_index);
                };
            }
            RefreshPreview();
            UI::VerticalBox *frames_root = nullptr;
            if (_clip == nullptr || _clip->Size() == 0u)
            {
                auto *frames_scroll = timeline_area->AddChild<UI::ScrollView>();
                frames_scroll->GetSlotAs<UI::LinearSlot>()
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
                frames_root = frames_scroll->AddChild<UI::VerticalBox>();
                frames_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
                frames_root->SlotPadding() = UI::Padding(0.0f);
            }
            for (u32 index = 0u; frames_root != nullptr && index < Frames().size(); ++index)
            {
                auto &frame = Frames()[index];
                auto *row = frames_root->AddChild<UI::HorizontalBox>();
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
                    if (index >= Frames().size())
                        return;
                    if (auto parsed = StringUtils::ParseFloat(value); parsed.has_value())
                    {
                        Frames()[index]._time = std::max(parsed.value(), 0.0f);
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
            if (_clip != nullptr && _animation_preview != nullptr)
            {
                if (_preview_mesh_guid != _clip->PreviewMeshGuid())
                    _preview_mesh_guid = _clip->PreviewMeshGuid();
                if (_preview_mesh == nullptr && !_preview_mesh_guid.IsEmpty())
                {
                    _preview_mesh = ResourceMgr::Get().GetRef<Render::SkeletonMesh>(_preview_mesh_guid);
                    if (_preview_mesh == nullptr)
                        _preview_mesh = ResourceMgr::Get().Load<Render::SkeletonMesh>(_preview_mesh_guid);
                }
                _animation_preview->SetClip(_clip);
                _animation_preview->SetMesh(_preview_mesh.get());
                _animation_preview->SetShowSkeleton(_show_skeleton);
                _animation_preview->SetSelectedJoint(_selected_joint);
                _animation_preview->SetTime(_preview_time);
            }
            if (_preview_image == nullptr)
                return;
            if (_animation_preview != nullptr && _preview_mesh != nullptr && _clip != nullptr && _clip->Size() > 0u)
            {
                _preview_image->SetTexture(_animation_preview->GetRenderTexture());
                _preview_image->_uv_rect = Vector4f(0.0f, 0.0f, 1.0f, 1.0f);
                _preview_image->InvalidatePaint();
                if (_txt_preview_time != nullptr)
                    _txt_preview_time->SetText(std::format("Preview {:.3f}s - {}", _preview_time,
                                                            _preview_mesh->Name()));
                return;
            }
            Guid sprite_guid = Guid::EmptyGuid();
            if (!Frames().empty())
            {
                f32 sample_time = _preview_time;
                if (_clip->IsLooping() && _clip->Duration() > 0.0f)
                    sample_time = std::fmod(sample_time, _clip->Duration());
                for (const auto &frame : Frames())
                {
                    if (frame._time > sample_time)
                        break;
                    sprite_guid = frame._sprite;
                }
                if (sprite_guid.IsEmpty())
                    sprite_guid = Frames().front()._sprite;
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

        void AnimationClipEditor::SelectJoint(u16 joint_index)
        {
            _selected_joint = joint_index;
            if (_animation_preview != nullptr)
                _animation_preview->SetSelectedJoint(_selected_joint);
            if (_animation_timeline == nullptr || _clip == nullptr)
                return;
            u32 selected_track = 0u;
            for (u32 track_index = 0u; track_index < _clip->Size(); ++track_index)
            {
                if (_clip->GetIdAtIndex(track_index) == joint_index)
                {
                    selected_track = track_index + 1u;
                    break;
                }
            }
            _animation_timeline->SetSelectedTrack(selected_track);
        }

        void AnimationClipEditor::RefreshEvents()
        {
            if (_events_root == nullptr)
                return;
            _events_root->ClearChildren();
            AddSectionTitle(_events_root, std::format("Events ({})", Events().size()));
            auto *help = _events_root->AddChild<UI::Text>("Event ID is delivered to the animation event queue.");
            StyleText(help, kMutedTextColor);
            help->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, 34.0f));
            for (u32 index = 0u; index < Events().size(); ++index)
            {
                auto &event = Events()[index];
                AddFloatInput(_events_root, std::format("{} Time", index + 1u), event._time, [this, index](f32 value)
                {
                    if (index < Events().size())
                    {
                        Events()[index]._time = std::max(value, 0.0f);
                        MarkDirty();
                    }
                });
                auto *id_input = AddTextInput(_events_root, "Event ID", std::to_string(event._event_id), [this, index](String value)
                {
                    if (index < Events().size())
                    {
                        if (auto parsed = StringUtils::ParseFloat(value); parsed.has_value())
                            Events()[index]._event_id = static_cast<u32>(std::max(parsed.value(), 0.0f));
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
                    if (index < Events().size())
                    {
                        Events()[index]._kind = selected == 0 ? EAnimationEventKind::kGameplay : EAnimationEventKind::kCosmetic;
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
            frame._time = Frames().empty() ? 0.0f : Frames().back()._time +
                                                   (_clip->FrameDuration() > 0.0f ? _clip->FrameDuration() : 0.1f);
            if (!Frames().empty())
                frame._sprite = Frames().back()._sprite;
            Frames().push_back(frame);
            _clip->FrameCount(static_cast<u32>(Frames().size()));
            if (_clip->Duration() < frame._time)
                _clip->Duration(frame._time);
            MarkDirty();
            RefreshAllUI();
        }

        void AnimationClipEditor::AddEvent()
        {
            Events().push_back(AnimationEvent{0.0f, 0u, EAnimationEventKind::kCosmetic});
            MarkDirty();
            RefreshEvents();
        }

        void AnimationClipEditor::RemoveFrame(u32 index)
        {
            if (index >= Frames().size())
                return;
            Frames().erase(Frames().begin() + index);
            _clip->FrameCount(static_cast<u32>(Frames().size()));
            MarkDirty();
            RefreshTimeline();
        }

        void AnimationClipEditor::RemoveEvent(u32 index)
        {
            if (index >= Events().size())
                return;
            Events().erase(Events().begin() + index);
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
                if (frame_index < Frames().size())
                    Frames()[frame_index]._sprite = Guid::EmptyGuid();
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
                    if (frame_index < Frames().size())
                        Frames()[frame_index]._sprite = guid;
                    MarkDirty();
                    UIManager::Get()->HidePopup();
                    RefreshTimeline();
                };
                 list->AddItem(item);
             }
             for (const auto &entry : ResourceMgr::Get().GetSubAssets(Render::Sprite::StaticType()))
             {
                 auto sprite = ResourceMgr::Get().GetRef<Render::Sprite>(entry._guid);
                 if (!sprite)
                     sprite = ResourceMgr::Get().Load<Render::Sprite>(entry._guid);
                 if (!sprite)
                     continue;
                 const String name = sprite->Name().empty() ? entry._name : sprite->Name();
                 auto item = MakeRef<UI::Text>(name);
                 const Guid guid = entry._guid;
                 item->OnMouseClick() += [this, frame_index, guid](UI::UIEvent &)
                 {
                    if (frame_index < Frames().size())
                        Frames()[frame_index]._sprite = guid;
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
