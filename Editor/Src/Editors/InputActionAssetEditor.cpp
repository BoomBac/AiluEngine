#include "Editors/InputActionAssetEditor.h"
#include "Dock/DockManager.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "Input/InputTypes.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"
#include <algorithm>
#include <charconv>
#include <format>

using namespace Ailu::UI;

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            constexpr f32 kToolbarHeight = 28.0f;
            constexpr f32 kStatusBarHeight = 22.0f;
            constexpr f32 kLeftPanelWidth = 300.0f;
            constexpr f32 kRightPanelWidth = 250.0f;
            constexpr f32 kInputHeight = 22.0f;

            const Color kPanelColor = Color(0.16f, 0.17f, 0.19f, 1.0f);
            const Color kCenterColor = Color(0.12f, 0.13f, 0.14f, 1.0f);
            const Color kTextColor = Color(0.72f, 0.72f, 0.72f, 1.0f);
            const Color kMutedTextColor = Color(0.52f, 0.52f, 0.52f, 1.0f);

            Vector<String> ActionTypeItems()
            {
                return {"Button", "Value", "PassThrough"};
            }

            Vector<String> ValueTypeItems()
            {
                return {"Button", "Axis1D", "Axis2D", "Axis3D"};
            }

            Vector<String> MergeStrategyItems()
            {
                return {"MaxMagnitude", "Accumulate", "LastActiveDevice", "PassThrough"};
            }

            i32 ClampIndex(i32 index, size_t size)
            {
                if (size == 0u)
                    return -1;
                return std::clamp(index, 0, static_cast<i32>(size) - 1);
            }

            u32 ParseU32(const String &text, u32 fallback)
            {
                u32 value = fallback;
                std::from_chars(text.data(), text.data() + text.size(), value);
                return value;
            }

            i32 ParseI32(const String &text, i32 fallback)
            {
                i32 value = fallback;
                std::from_chars(text.data(), text.data() + text.size(), value);
                return value;
            }

            String BoolText(bool value)
            {
                return value ? "true" : "false";
            }
        } // namespace

        InputActionAssetEditor::InputActionAssetEditor()
            : DockWindow("Input Action Editor", Vector2f(1050.0f, 660.0f))
        {
            SetPosition(Vector2f(130.0f, 70.0f));

            auto *root_vb = _content_root->AddChild<UI::VerticalBox>();
            root_vb->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto *toolbar = root_vb->AddChild<UI::HorizontalBox>();
            toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, kToolbarHeight));
            BuildToolbar(toolbar);

            auto *main_area = root_vb->AddChild<UI::SplitView>();
            main_area->_is_horizontal = true;
            main_area->SetRatio(kLeftPanelWidth / 1050.0f);
            main_area->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto *left_border = main_area->AddChild<UI::Border>();
            left_border->_bg_color = kPanelColor;
            auto *left_scroll = left_border->AddChild<UI::ScrollView>();
            left_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            auto *left_vb = left_scroll->AddChild<UI::VerticalBox>();
            left_vb->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            BuildLeftPanel(left_vb);

            auto *right_split = main_area->AddChild<UI::SplitView>();
            right_split->_is_horizontal = true;
            right_split->SetRatio((1050.0f - kLeftPanelWidth - kRightPanelWidth) / (1050.0f - kLeftPanelWidth));

            auto *center_border = right_split->AddChild<UI::Border>();
            center_border->_bg_color = kCenterColor;
            auto *center_scroll = center_border->AddChild<UI::ScrollView>();
            center_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            auto *center_vb = center_scroll->AddChild<UI::VerticalBox>();
            center_vb->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            BuildCenterPanel(center_vb);

            auto *right_border = right_split->AddChild<UI::Border>();
            right_border->_bg_color = kPanelColor;
            auto *right_scroll = right_border->AddChild<UI::ScrollView>();
            right_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            auto *right_vb = right_scroll->AddChild<UI::VerticalBox>();
            right_vb->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            BuildRightPanel(right_vb);

            auto *status_bar = root_vb->AddChild<UI::HorizontalBox>();
            status_bar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                      .Size(Vector2f(0.0f, kStatusBarHeight));
            BuildStatusBar(status_bar);
        }

        InputActionAssetEditor::~InputActionAssetEditor() = default;

        void InputActionAssetEditor::Update(f32 dt)
        {
            DockWindow::Update(dt);
            if (!_input_asset)
                return;

            const bool ctrl = Input::IsKeyDown(EKey::kLCONTROL) || Input::IsKeyDown(EKey::kRCONTROL);
            if (ctrl && Input::IsKeyPressed(EKey::kS))
                Apply();
        }

        void InputActionAssetEditor::Open(InputActionAsset *asset)
        {
            if (!asset)
                return;
            _input_asset = asset;
            ReadFromAsset();
            _original_action_maps = _editing_action_maps;
            _original_contexts = _editing_contexts;
            _is_dirty = false;
            if (!_editing_action_maps.empty())
                SelectActionMap(0);
            else if (!_editing_contexts.empty())
                SelectContext(0);
            SetTitle("Input Action Editor - " + asset->Name());
            RefreshAllUI();
        }

        void InputActionAssetEditor::Close()
        {
            _input_asset = nullptr;
            _editing_action_maps.clear();
            _editing_contexts.clear();
            _original_action_maps.clear();
            _original_contexts.clear();
        }

        void InputActionAssetEditor::BuildToolbar(UI::HorizontalBox *toolbar)
        {
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

            toolbar->AddChild<UI::Text>("|")->GetSlotAs<UI::LinearSlot>()
                   .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size(Vector2f(12.0f, 0.0f));

            _btn_add_map = add_button("+ Map", 58.0f);
            _btn_add_map->OnMouseClick() += [this](UI::UIEvent &e) { AddActionMap(); e._is_handled = true; };
            _btn_add_action = add_button("+ Action", 72.0f);
            _btn_add_action->OnMouseClick() += [this](UI::UIEvent &e) { AddAction(); e._is_handled = true; };
            _btn_add_binding = add_button("+ Binding", 78.0f);
            _btn_add_binding->OnMouseClick() += [this](UI::UIEvent &e) { AddBinding(); e._is_handled = true; };
            _btn_add_context = add_button("+ Context", 82.0f);
            _btn_add_context->OnMouseClick() += [this](UI::UIEvent &e) { AddContext(); e._is_handled = true; };
            _btn_delete = add_button("Delete", 62.0f);
            _btn_delete->OnMouseClick() += [this](UI::UIEvent &e)
            {
                if (_selection_type == EInputActionEditorSelection::kBinding)
                    RemoveSelectedBinding();
                else if (_selection_type == EInputActionEditorSelection::kAction)
                    RemoveSelectedAction();
                else if (_selection_type == EInputActionEditorSelection::kActionMap)
                    RemoveSelectedActionMap();
                else if (_selection_type == EInputActionEditorSelection::kContext)
                    RemoveSelectedContext();
                e._is_handled = true;
            };

            auto *spacer = toolbar->AddChild<UI::Text>("");
            spacer->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        }

        void InputActionAssetEditor::BuildLeftPanel(UI::VerticalBox *left)
        {
            left->SlotPadding() = UI::Padding(4.0f);
            AddSectionTitle(left, "Asset Info");
            _txt_asset_name = left->AddChild<UI::Text>("-");
            _txt_asset_path = left->AddChild<UI::Text>("-");
            _txt_asset_guid = left->AddChild<UI::Text>("-");
            for (auto *text : {_txt_asset_name, _txt_asset_path, _txt_asset_guid})
            {
                text->_color = kMutedTextColor;
                text->FontSize(11.0f);
                text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, 15.0f)).Margin(Vector4f(4.0f, 0.0f, 0.0f, 0.0f));
            }

            AddSectionTitle(left, "Input Actions");
            _tree_root = left->AddChild<UI::VerticalBox>();
            _tree_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        }

        void InputActionAssetEditor::BuildCenterPanel(UI::VerticalBox *center)
        {
            center->SlotPadding() = UI::Padding(6.0f);
            _detail_root = center;
        }

        void InputActionAssetEditor::BuildRightPanel(UI::VerticalBox *right)
        {
            right->SlotPadding() = UI::Padding(4.0f);
            AddSectionTitle(right, "Summary");
            auto make_stat = [right](const String &label, UI::Text *&out)
            {
                auto *row = right->AddChild<UI::HorizontalBox>();
                row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                   .Size(Vector2f(0.0f, 18.0f)).Margin(Vector4f(4.0f, 0.0f, 0.0f, 0.0f));
                auto *lbl = row->AddChild<UI::Text>(label);
                lbl->_color = kMutedTextColor;
                lbl->FontSize(11.0f);
                lbl->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                   .Size(Vector2f(70.0f, 0.0f));
                out = row->AddChild<UI::Text>("-");
                out->_color = kTextColor;
                out->FontSize(11.0f);
                out->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            };
            make_stat("Maps", _txt_map_count);
            make_stat("Actions", _txt_action_count);
            make_stat("Bindings", _txt_binding_count);
            make_stat("Contexts", _txt_context_count);

            AddSectionTitle(right, "Control Paths");
            _summary_root = right->AddChild<UI::VerticalBox>();
            _summary_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        }

        void InputActionAssetEditor::BuildStatusBar(UI::HorizontalBox *status_bar)
        {
            status_bar->SlotPadding() = UI::Padding(6.0f, 2.0f, 6.0f, 2.0f);
            _txt_status = status_bar->AddChild<UI::Text>("Ready");
            _txt_status->_color = kMutedTextColor;
            _txt_status->FontSize(11.0f);
            _txt_status->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        }

        void InputActionAssetEditor::ReadFromAsset()
        {
            _editing_action_maps.clear();
            _editing_contexts.clear();
            if (!_input_asset)
                return;
            _editing_action_maps = _input_asset->GetActionMaps();
            _editing_contexts = _input_asset->GetContexts();
        }

        void InputActionAssetEditor::WriteToAsset()
        {
            if (!_input_asset)
                return;
            _input_asset->GetActionMaps() = _editing_action_maps;
            _input_asset->GetContexts() = _editing_contexts;
        }

        void InputActionAssetEditor::Apply()
        {
            if (!_input_asset)
                return;
            WriteToAsset();
            if (auto *linked = ResourceMgr::Get().GetLinkedAsset(_input_asset))
                ResourceMgr::Get().SaveAsset(linked);
            _original_action_maps = _editing_action_maps;
            _original_contexts = _editing_contexts;
            _is_dirty = false;
            RefreshStatusBar();
            LOG_INFO("InputActionAssetEditor: Applied");
        }

        void InputActionAssetEditor::Revert()
        {
            _editing_action_maps = _original_action_maps;
            _editing_contexts = _original_contexts;
            _is_dirty = false;
            ValidateSelection();
            RefreshAllUI();
        }

        void InputActionAssetEditor::MarkDirty()
        {
            _is_dirty = true;
            RefreshSummary();
            RefreshStatusBar();
        }

        void InputActionAssetEditor::ValidateSelection()
        {
            _selected_map = ClampIndex(_selected_map, _editing_action_maps.size());
            if (_selected_map < 0)
                _selected_action = _selected_binding = -1;
            else
            {
                _selected_action = ClampIndex(_selected_action,
                                              _editing_action_maps[_selected_map].GetActions().size());
                if (_selected_action < 0)
                    _selected_binding = -1;
                else
                {
                    auto &bindings = _editing_action_maps[_selected_map].GetActions()[_selected_action].GetBindings();
                    _selected_binding = ClampIndex(_selected_binding, bindings.size());
                }
            }
            _selected_context = ClampIndex(_selected_context, _editing_contexts.size());

            if (_selection_type == EInputActionEditorSelection::kActionMap && _selected_map < 0)
                _selection_type = EInputActionEditorSelection::kNone;
            if (_selection_type == EInputActionEditorSelection::kAction && _selected_action < 0)
            {
                _selection_type = _selected_map >= 0 ? EInputActionEditorSelection::kActionMap
                                                     : EInputActionEditorSelection::kNone;
            }
            if (_selection_type == EInputActionEditorSelection::kBinding && _selected_binding < 0)
            {
                _selection_type = _selected_action >= 0 ? EInputActionEditorSelection::kAction
                                                        : EInputActionEditorSelection::kNone;
            }
            if (_selection_type == EInputActionEditorSelection::kContext && _selected_context < 0)
                _selection_type = EInputActionEditorSelection::kNone;
        }

        void InputActionAssetEditor::RefreshAllUI()
        {
            ValidateSelection();
            RefreshAssetInfo();
            RefreshTree();
            RefreshDetails();
            RefreshSummary();
            RefreshStatusBar();
        }

        void InputActionAssetEditor::RefreshAssetInfo()
        {
            if (!_input_asset)
                return;
            if (_txt_asset_name)
                _txt_asset_name->SetText("Name: " + _input_asset->Name());
            if (auto *linked = ResourceMgr::Get().GetLinkedAsset(_input_asset))
            {
                if (_txt_asset_path)
                    _txt_asset_path->SetText("Path: " + ToChar(linked->_asset_path));
                if (_txt_asset_guid)
                    _txt_asset_guid->SetText("GUID: " + linked->GetGuid().ToString());
            }
        }

        void InputActionAssetEditor::RefreshTree()
        {
            if (!_tree_root)
                return;
            _tree_root->ClearChildren();

            for (i32 map_index = 0; map_index < static_cast<i32>(_editing_action_maps.size()); ++map_index)
            {
                auto &map = _editing_action_maps[map_index];
                auto *map_button = _tree_root->AddChild<UI::Button>(
                        std::format("[Map] {} ({})", map.GetName(), map.ActionCount()));
                map_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                          .Size(Vector2f(0.0f, 22.0f)).Margin(Vector4f(0.0f, 2.0f, 0.0f, 0.0f));
                map_button->OnMouseClick() += [this, map_index](UI::UIEvent &e)
                {
                    SelectActionMap(map_index);
                    e._is_handled = true;
                };

                auto &actions = map.GetActions();
                for (i32 action_index = 0; action_index < static_cast<i32>(actions.size()); ++action_index)
                {
                    auto &action = actions[action_index];
                    auto *action_button = _tree_root->AddChild<UI::Button>(
                            std::format("  - {} [{}]", action.GetName(), action.GetBindings().size()));
                    action_button->GetSlotAs<UI::LinearSlot>()
                                 .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                                 .Size(Vector2f(0.0f, 20.0f)).Margin(Vector4f(8.0f, 1.0f, 0.0f, 0.0f));
                    action_button->OnMouseClick() += [this, map_index, action_index](UI::UIEvent &e)
                    {
                        SelectAction(map_index, action_index);
                        e._is_handled = true;
                    };

                    auto &bindings = action.GetBindings();
                    for (i32 binding_index = 0; binding_index < static_cast<i32>(bindings.size()); ++binding_index)
                    {
                        const auto &binding = bindings[binding_index];
                        auto title = binding._name.empty() ? binding._control_path : binding._name;
                        auto *binding_button = _tree_root->AddChild<UI::Button>(std::format("     {}", title));
                        binding_button->GetSlotAs<UI::LinearSlot>()
                                      .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                                      .Size(Vector2f(0.0f, 18.0f)).Margin(Vector4f(18.0f, 1.0f, 0.0f, 0.0f));
                        binding_button->OnMouseClick() += [this, map_index, action_index, binding_index](
                                UI::UIEvent &e)
                        {
                            SelectBinding(map_index, action_index, binding_index);
                            e._is_handled = true;
                        };
                    }
                }
            }

            AddSectionTitle(_tree_root, "Contexts");
            for (i32 context_index = 0; context_index < static_cast<i32>(_editing_contexts.size()); ++context_index)
            {
                auto &context = _editing_contexts[context_index];
                auto *context_button = _tree_root->AddChild<UI::Button>(std::format("[Context] {}", context.GetName()));
                context_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                              .Size(Vector2f(0.0f, 22.0f)).Margin(Vector4f(0.0f, 2.0f, 0.0f, 0.0f));
                context_button->OnMouseClick() += [this, context_index](UI::UIEvent &e)
                {
                    SelectContext(context_index);
                    e._is_handled = true;
                };
            }
        }

        void InputActionAssetEditor::RefreshDetails()
        {
            if (!_detail_root)
                return;
            _detail_root->ClearChildren();

            if (_selection_type == EInputActionEditorSelection::kNone)
            {
                AddSectionTitle(_detail_root, "Input Action Asset");
                auto *text = _detail_root->AddChild<UI::Text>("Select an action map, action, binding, or context.");
                text->_color = kTextColor;
                return;
            }

            if (auto *map = SelectedMap(); _selection_type == EInputActionEditorSelection::kActionMap && map)
            {
                AddSectionTitle(_detail_root, "Action Map");
                AddTextInput(_detail_root, "Name", map->GetName(), [this, map](String value)
                {
                    map->SetName(value);
                    MarkDirty();
                    RefreshTree();
                });
                AddU32Input(_detail_root, "Id", map->GetId(), [this, map](u32 value)
                {
                    map->SetId(value);
                    MarkDirty();
                });
                return;
            }

            if (auto *action = SelectedAction(); _selection_type == EInputActionEditorSelection::kAction && action)
            {
                AddSectionTitle(_detail_root, "Action");
                AddTextInput(_detail_root, "Name", action->GetName(), [this, action](String value)
                {
                    action->SetName(value);
                    MarkDirty();
                    RefreshTree();
                });
                AddU32Input(_detail_root, "Id", action->GetId(), [this, action](u32 value)
                {
                    action->SetId(value);
                    MarkDirty();
                });
                AddDropdown(_detail_root, "Action Type", ActionTypeItems(), static_cast<i32>(action->GetActionType()),
                            [this, action](i32 value)
                            {
                                action->SetActionType(static_cast<EInputActionType>(value));
                                MarkDirty();
                            });
                AddDropdown(_detail_root, "Value Type", ValueTypeItems(), static_cast<i32>(action->GetValueType()),
                            [this, action](i32 value)
                            {
                                action->SetValueType(static_cast<EInputValueType>(value));
                                MarkDirty();
                            });
                AddDropdown(_detail_root, "Merge", MergeStrategyItems(), static_cast<i32>(action->GetMergeStrategy()),
                            [this, action](i32 value)
                            {
                                action->SetMergeStrategy(static_cast<EBindingMergeStrategy>(value));
                                MarkDirty();
                            });
                auto *info = _detail_root->AddChild<UI::Text>(
                        std::format("Bindings: {}", action->GetBindings().size()));
                info->_color = kMutedTextColor;
                info->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, 18.0f)).Margin(Vector4f(4.0f, 8.0f, 0.0f, 0.0f));
                return;
            }

            if (auto *binding = SelectedBinding(); _selection_type == EInputActionEditorSelection::kBinding && binding)
            {
                AddSectionTitle(_detail_root, "Binding");
                AddTextInput(_detail_root, "Name", binding->_name, [this, binding](String value)
                {
                    binding->_name = value;
                    MarkDirty();
                    RefreshTree();
                });
                AddTextInput(_detail_root, "Control Path", binding->_control_path, [this, binding](String value)
                {
                    binding->_control_path = value;
                    MarkDirty();
                    RefreshTree();
                });
                AddTextInput(_detail_root, "Groups", binding->_groups, [this, binding](String value)
                {
                    binding->_groups = value;
                    MarkDirty();
                });
                AddCheckBox(_detail_root, "Composite", binding->_is_composite, [this, binding](bool value)
                {
                    binding->_is_composite = value;
                    MarkDirty();
                });
                AddCheckBox(_detail_root, "Part", binding->_is_part_of_composite, [this, binding](bool value)
                {
                    binding->_is_part_of_composite = value;
                    MarkDirty();
                });
                AddTextInput(_detail_root, "Part Name", binding->_composite_part_name, [this, binding](String value)
                {
                    binding->_composite_part_name = value;
                    MarkDirty();
                });
                auto *counts = _detail_root->AddChild<UI::Text>(std::format("Processors: {}   Interactions: {}",
                                                                             binding->_processors.size(),
                                                                             binding->_interactions.size()));
                counts->_color = kMutedTextColor;
                counts->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                      .Size(Vector2f(0.0f, 18.0f)).Margin(Vector4f(4.0f, 8.0f, 0.0f, 0.0f));
                return;
            }

            if (auto *context = SelectedContext(); _selection_type == EInputActionEditorSelection::kContext && context)
            {
                AddSectionTitle(_detail_root, "Context");
                AddTextInput(_detail_root, "Name", context->GetName(), [this, context](String value)
                {
                    context->SetName(value);
                    MarkDirty();
                    RefreshTree();
                });
                AddI32Input(_detail_root, "Priority", context->GetPriority(), [this, context](i32 value)
                {
                    context->SetPriority(value);
                    MarkDirty();
                });
                AddCheckBox(_detail_root, "Active", context->IsActive(), [this, context](bool value)
                {
                    context->SetActive(value);
                    MarkDirty();
                });
                AddCheckBox(_detail_root, "Consume", context->ConsumeInput(), [this, context](bool value)
                {
                    context->SetConsumeInput(value);
                    MarkDirty();
                });
                AddCheckBox(_detail_root, "Block Lower", context->BlocksLowerContexts(), [this, context](bool value)
                {
                    context->SetBlocksLowerContexts(value);
                    MarkDirty();
                });

                AddSectionTitle(_detail_root, "Action Maps");
                for (i32 ref_index = 0; ref_index < static_cast<i32>(context->GetActionMaps().size()); ++ref_index)
                {
                    auto *row = _detail_root->AddChild<UI::HorizontalBox>();
                    row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                       .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(4.0f, 0.0f, 4.0f, 1.0f));
                    const auto &map_ref = context->GetActionMaps()[ref_index];
                    auto *text = row->AddChild<UI::Text>(map_ref ? map_ref->GetName() : String("<null>"));
                    text->_color = kTextColor;
                    text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
                    auto *remove = row->AddChild<UI::Button>("X");
                    remove->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                          .Size(Vector2f(24.0f, 0.0f));
                    remove->OnMouseClick() += [this, ref_index](UI::UIEvent &e)
                    {
                        RemoveContextMapRef(ref_index);
                        e._is_handled = true;
                    };
                }

                Vector<String> map_items;
                for (const auto &map : _editing_action_maps)
                    map_items.emplace_back(map.GetName());
                if (!map_items.empty())
                {
                    AddDropdown(_detail_root, "Add Map", map_items, -1, [this, map_items](i32 index)
                    {
                        if (index >= 0 && index < static_cast<i32>(map_items.size()))
                            AddContextMapRef(map_items[index]);
                    });
                }
            }
        }

        void InputActionAssetEditor::RefreshSummary()
        {
            size_t action_count = 0u;
            size_t binding_count = 0u;
            for (const auto &map : _editing_action_maps)
            {
                action_count += map.GetActions().size();
                for (const auto &action : map.GetActions())
                    binding_count += action.GetBindings().size();
            }
            if (_txt_map_count)
                _txt_map_count->SetText(std::to_string(_editing_action_maps.size()));
            if (_txt_action_count)
                _txt_action_count->SetText(std::to_string(action_count));
            if (_txt_binding_count)
                _txt_binding_count->SetText(std::to_string(binding_count));
            if (_txt_context_count)
                _txt_context_count->SetText(std::to_string(_editing_contexts.size()));

            if (_summary_root)
            {
                _summary_root->ClearChildren();
                Vector<String> paths = {"<Keyboard>/space", "<Keyboard>/w", "<Keyboard>/a", "<Keyboard>/s",
                                        "<Keyboard>/d", "<Mouse>/leftButton", "<Mouse>/delta",
                                        "<Gamepad>/buttonSouth", "<Gamepad>/leftStick"};
                for (const String &path : paths)
                {
                    auto *button = _summary_root->AddChild<UI::Button>(path);
                    button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                          .Size(Vector2f(0.0f, 20.0f)).Margin(Vector4f(4.0f, 1.0f, 4.0f, 0.0f));
                    button->OnMouseClick() += [this, path](UI::UIEvent &e)
                    {
                        if (auto *binding = SelectedBinding())
                        {
                            binding->_control_path = path;
                            MarkDirty();
                            RefreshTree();
                            RefreshDetails();
                        }
                        e._is_handled = true;
                    };
                }
            }
        }

        void InputActionAssetEditor::RefreshStatusBar()
        {
            if (!_txt_status)
                return;
            _txt_status->SetText(_is_dirty ? "Modified. Ctrl+S or Apply to save." : "Saved.");
        }

        void InputActionAssetEditor::SelectActionMap(i32 map_index)
        {
            _selection_type = EInputActionEditorSelection::kActionMap;
            _selected_map = map_index;
            _selected_action = -1;
            _selected_binding = -1;
            _selected_context = -1;
            RefreshAllUI();
        }

        void InputActionAssetEditor::SelectAction(i32 map_index, i32 action_index)
        {
            _selection_type = EInputActionEditorSelection::kAction;
            _selected_map = map_index;
            _selected_action = action_index;
            _selected_binding = -1;
            _selected_context = -1;
            RefreshAllUI();
        }

        void InputActionAssetEditor::SelectBinding(i32 map_index, i32 action_index, i32 binding_index)
        {
            _selection_type = EInputActionEditorSelection::kBinding;
            _selected_map = map_index;
            _selected_action = action_index;
            _selected_binding = binding_index;
            _selected_context = -1;
            RefreshAllUI();
        }

        void InputActionAssetEditor::SelectContext(i32 context_index)
        {
            _selection_type = EInputActionEditorSelection::kContext;
            _selected_context = context_index;
            _selected_map = -1;
            _selected_action = -1;
            _selected_binding = -1;
            RefreshAllUI();
        }

        void InputActionAssetEditor::AddActionMap()
        {
            _editing_action_maps.emplace_back(std::format("ActionMap{}", _editing_action_maps.size() + 1u));
            _selected_map = static_cast<i32>(_editing_action_maps.size()) - 1;
            _selection_type = EInputActionEditorSelection::kActionMap;
            MarkDirty();
            RefreshAllUI();
        }

        void InputActionAssetEditor::RemoveSelectedActionMap()
        {
            if (_selected_map < 0 || _selected_map >= static_cast<i32>(_editing_action_maps.size()))
                return;
            const String removed_name = _editing_action_maps[_selected_map].GetName();
            _editing_action_maps.erase(_editing_action_maps.begin() + _selected_map);
            for (auto &context : _editing_contexts)
            {
                auto &refs = context.GetActionMaps();
                refs.erase(std::remove_if(refs.begin(), refs.end(),
                                          [&removed_name](const Ref<InputActionMap> &ref)
                                          {
                                              return ref && ref->GetName() == removed_name;
                                          }),
                           refs.end());
            }
            _selection_type = EInputActionEditorSelection::kNone;
            MarkDirty();
            RefreshAllUI();
        }

        void InputActionAssetEditor::AddAction()
        {
            if (_selected_map < 0 && !_editing_action_maps.empty())
                _selected_map = 0;
            auto *map = SelectedMap();
            if (!map)
                return;
            auto &actions = map->GetActions();
            actions.emplace_back(std::format("Action{}", actions.size() + 1u));
            _selected_action = static_cast<i32>(actions.size()) - 1;
            _selection_type = EInputActionEditorSelection::kAction;
            MarkDirty();
            RefreshAllUI();
        }

        void InputActionAssetEditor::RemoveSelectedAction()
        {
            auto *map = SelectedMap();
            if (!map || _selected_action < 0 || _selected_action >= static_cast<i32>(map->GetActions().size()))
                return;
            map->GetActions().erase(map->GetActions().begin() + _selected_action);
            _selection_type = EInputActionEditorSelection::kActionMap;
            MarkDirty();
            RefreshAllUI();
        }

        void InputActionAssetEditor::AddBinding()
        {
            auto *action = SelectedAction();
            if (!action)
                return;
            InputBinding binding;
            binding._name = std::format("Binding{}", action->GetBindings().size() + 1u);
            binding._control_path = "<Keyboard>/space";
            action->GetBindings().emplace_back(std::move(binding));
            _selected_binding = static_cast<i32>(action->GetBindings().size()) - 1;
            _selection_type = EInputActionEditorSelection::kBinding;
            MarkDirty();
            RefreshAllUI();
        }

        void InputActionAssetEditor::RemoveSelectedBinding()
        {
            auto *action = SelectedAction();
            if (!action || _selected_binding < 0 || _selected_binding >= static_cast<i32>(action->GetBindings().size()))
                return;
            action->GetBindings().erase(action->GetBindings().begin() + _selected_binding);
            _selection_type = EInputActionEditorSelection::kAction;
            MarkDirty();
            RefreshAllUI();
        }

        void InputActionAssetEditor::AddContext()
        {
            _editing_contexts.emplace_back(std::format("Context{}", _editing_contexts.size() + 1u));
            _selected_context = static_cast<i32>(_editing_contexts.size()) - 1;
            _selection_type = EInputActionEditorSelection::kContext;
            MarkDirty();
            RefreshAllUI();
        }

        void InputActionAssetEditor::RemoveSelectedContext()
        {
            if (_selected_context < 0 || _selected_context >= static_cast<i32>(_editing_contexts.size()))
                return;
            _editing_contexts.erase(_editing_contexts.begin() + _selected_context);
            _selection_type = EInputActionEditorSelection::kNone;
            MarkDirty();
            RefreshAllUI();
        }

        void InputActionAssetEditor::AddContextMapRef(const String &map_name)
        {
            auto *context = SelectedContext();
            if (!context)
                return;
            context->AddActionMap(MakeRef<InputActionMap>(map_name));
            MarkDirty();
            RefreshDetails();
        }

        void InputActionAssetEditor::RemoveContextMapRef(i32 ref_index)
        {
            auto *context = SelectedContext();
            if (!context || ref_index < 0 || ref_index >= static_cast<i32>(context->GetActionMaps().size()))
                return;
            context->GetActionMaps().erase(context->GetActionMaps().begin() + ref_index);
            MarkDirty();
            RefreshDetails();
        }

        InputActionMap *InputActionAssetEditor::SelectedMap()
        {
            if (_selected_map < 0 || _selected_map >= static_cast<i32>(_editing_action_maps.size()))
                return nullptr;
            return &_editing_action_maps[_selected_map];
        }

        InputAction *InputActionAssetEditor::SelectedAction()
        {
            auto *map = SelectedMap();
            if (!map || _selected_action < 0 || _selected_action >= static_cast<i32>(map->GetActions().size()))
                return nullptr;
            return &map->GetActions()[_selected_action];
        }

        InputBinding *InputActionAssetEditor::SelectedBinding()
        {
            auto *action = SelectedAction();
            if (!action || _selected_binding < 0 || _selected_binding >= static_cast<i32>(action->GetBindings().size()))
                return nullptr;
            return &action->GetBindings()[_selected_binding];
        }

        InputContext *InputActionAssetEditor::SelectedContext()
        {
            if (_selected_context < 0 || _selected_context >= static_cast<i32>(_editing_contexts.size()))
                return nullptr;
            return &_editing_contexts[_selected_context];
        }

        const InputActionMap *InputActionAssetEditor::SelectedMap() const
        {
            return const_cast<InputActionAssetEditor *>(this)->SelectedMap();
        }

        const InputAction *InputActionAssetEditor::SelectedAction() const
        {
            return const_cast<InputActionAssetEditor *>(this)->SelectedAction();
        }

        const InputBinding *InputActionAssetEditor::SelectedBinding() const
        {
            return const_cast<InputActionAssetEditor *>(this)->SelectedBinding();
        }

        const InputContext *InputActionAssetEditor::SelectedContext() const
        {
            return const_cast<InputActionAssetEditor *>(this)->SelectedContext();
        }

        UI::Text *InputActionAssetEditor::AddSectionTitle(UI::UIElement *parent, const String &title)
        {
            auto *txt = parent->AddChild<UI::Text>(title);
            txt->_color = Color(0.85f, 0.85f, 0.85f, 1.0f);
            txt->FontSize(13.0f);
            txt->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
               .Size(Vector2f(0.0f, 20.0f)).Margin(Vector4f(4.0f, 6.0f, 0.0f, 2.0f));
            return txt;
        }

        UI::HorizontalBox *InputActionAssetEditor::AddPropertyRow(UI::UIElement *parent, const String &label,
                                                                  f32 label_width)
        {
            auto *row = parent->AddChild<UI::HorizontalBox>();
            row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
               .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(4.0f, 0.0f, 4.0f, 2.0f));
            auto *lbl = row->AddChild<UI::Text>(label);
            lbl->_color = kMutedTextColor;
            lbl->FontSize(12.0f);
            lbl->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
               .Size(Vector2f(label_width, 0.0f));
            return row;
        }

        UI::InputBlock *InputActionAssetEditor::AddTextInput(UI::UIElement *parent, const String &label,
                                                             const String &value,
                                                             const std::function<void(String)> &on_changed,
                                                             f32 label_width)
        {
            auto *row = AddPropertyRow(parent, label, label_width);
            auto *input = row->AddChild<UI::InputBlock>(value);
            input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            input->_on_content_changed += [on_changed](String content) { on_changed(content); };
            return input;
        }

        UI::InputBlock *InputActionAssetEditor::AddU32Input(UI::UIElement *parent, const String &label, u32 value,
                                                            const std::function<void(u32)> &on_changed, f32 label_width)
        {
            auto *input = AddTextInput(parent, label, std::to_string(value),
                                       [value, on_changed](String content) { on_changed(ParseU32(content, value)); },
                                       label_width);
            return input;
        }

        UI::InputBlock *InputActionAssetEditor::AddI32Input(UI::UIElement *parent, const String &label, i32 value,
                                                            const std::function<void(i32)> &on_changed, f32 label_width)
        {
            auto *input = AddTextInput(parent, label, std::to_string(value),
                                       [value, on_changed](String content) { on_changed(ParseI32(content, value)); },
                                       label_width);
            return input;
        }

        UI::Dropdown *InputActionAssetEditor::AddDropdown(UI::UIElement *parent, const String &label,
                                                          const Vector<String> &items, i32 selected_index,
                                                          const std::function<void(i32)> &on_changed,
                                                          f32 label_width)
        {
            auto *row = AddPropertyRow(parent, label, label_width);
            auto *dropdown = row->AddChild<UI::Dropdown>(items);
            dropdown->SetSelectedIndex(selected_index);
            dropdown->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            dropdown->_on_selected_changed += [on_changed](i32 index) { on_changed(index); };
            return dropdown;
        }

        UI::CheckBox *InputActionAssetEditor::AddCheckBox(UI::UIElement *parent, const String &label, bool value,
                                                          const std::function<void(bool)> &on_changed, f32 label_width)
        {
            auto *row = AddPropertyRow(parent, label, label_width);
            auto *checkbox = row->AddChild<UI::CheckBox>();
            checkbox->SetChecked(value);
            checkbox->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(22.0f, 0.0f));
            auto *text = row->AddChild<UI::Text>(BoolText(value));
            text->_color = kTextColor;
            text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            checkbox->OnMouseClick() += [checkbox, text, on_changed](UI::UIEvent &e)
            {
                const bool checked = checkbox->IsChecked();
                text->SetText(BoolText(checked));
                on_changed(checked);
                e._is_handled = true;
            };
            return checkbox;
        }
    } // namespace Editor
} // namespace Ailu
