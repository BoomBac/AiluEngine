#include "Editors/AnimationControllerEditor.h"

#include "Animation/Clip.h"
#include "Animation/BlendSpace.h"
#include "Assets/Asset.h"
#include "Common/Undo.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "Graph/GraphCanvas.h"
#include "Graph/GraphNodeRegistry.h"
#include "Common/EditorUIHelpers.h"
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
            constexpr f32 kInputHeight = 22.0f;
            const Color kPanelColor = Color(0.16f, 0.17f, 0.19f, 1.0f);
            const Color kCenterColor = Color(0.12f, 0.13f, 0.14f, 1.0f);
            const Color kTextColor = Color(0.75f, 0.75f, 0.75f, 1.0f);
            const Color kMutedTextColor = Color(0.55f, 0.55f, 0.55f, 1.0f);

            void StyleText(UI::Text *text, Color color = kTextColor, f32 size = 11.0f)
            {
                text->_color = color;
                text->FontSize(size);
            }

            UI::InputBlock *AddInputToRow(UI::HorizontalBox *row, const String &value)
            {
                auto *input = row->AddChild<UI::InputBlock>(value);
                input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                        .Margin(Vector4f(2.0f, 1.0f, 2.0f, 1.0f));
                return input;
            }

            Vector<String> ParameterTypeNames()
            {
                return {"Float", "Int", "Bool", "Trigger"};
            }

            Vector<String> MotionTypeNames()
            {
                return {"Clip", "BlendSpace"};
            }

            Vector<String> ConditionOperationNames()
            {
                return {"Equal", "Not Equal", "Greater", "Greater Equal", "Less", "Less Equal", "Triggered"};
            }

            String MotionAssetName(const AnimationMotion &motion)
            {
                if (motion._asset.IsEmpty())
                    return {};
                if (motion._type == EAnimationMotionType::kBlendSpace)
                {
                    auto blend_space = ResourceMgr::Get().Load<BlendSpaceAsset>(motion._asset);
                    return blend_space != nullptr ? blend_space->Name() : String();
                }
                auto clip = ResourceMgr::Get().Load<AnimationClip>(motion._asset);
                return clip != nullptr ? clip->Name() : String();
            }

            String StateDisplayName(const AnimationState &state, u32 index)
            {
                const String asset_name = MotionAssetName(state._motion);
                if (!asset_name.empty())
                    return asset_name;
                return state._name.empty() ? std::format("State{}", index + 1u) : state._name;
            }

            String StateName(const Vector<AnimationState> &states, u16 index)
            {
                return index < states.size() ? StateDisplayName(states[index], index) : String("Any State");
            }

            const GraphPinData *FindPin(const GraphNodeData &node, StringView name, EGraphPinDirection direction)
            {
                for (const auto &pin : node._pins)
                {
                    if (pin._name == name && pin._direction == direction)
                        return &pin;
                }
                return nullptr;
            }

            i32 ParseStateIndex(StringView property_data)
            {
                constexpr StringView kPrefix = "state:";
                if (property_data.size() <= kPrefix.size() || property_data.substr(0, kPrefix.size()) != kPrefix)
                    return -1;
                i32 index = -1;
                const auto result = std::from_chars(property_data.data() + kPrefix.size(), property_data.data() + property_data.size(), index);
                return result.ec == std::errc() ? index : -1;
            }

            String MakeGraphSignature(const Vector<GraphNodeData> &nodes, const Vector<GraphLinkData> &links)
            {
                String signature;
                for (const auto &node : nodes)
                    signature += node._node_type + ":" + node._property_data + ";";
                signature += "|";
                for (const auto &link : links)
                    signature += link._output_pin.ToString() + ":" + link._input_pin.ToString() + ";";
                return signature;
            }

            void RegisterAnimationGraphNode(GraphNodeDesc desc)
            {
                auto &registry = GraphNodeRegistry::Get();
                if (registry.FindNode(desc._type_id) == nullptr)
                    registry.RegisterNode(std::move(desc));
            }
        }

        AnimationControllerEditor::AnimationControllerEditor()
            : AssetEditor("Animation Controller Editor", Vector2f(1240.0f, 760.0f))
        {
            EnsureGraphNodeRegistry();
            _graph_asset = MakeScope<GraphAsset>("AnimationControllerPreview");
            _graph_asset->SchemaType("AnimationControllerGraphSchema");
            _graph_document = MakeScope<GraphDocument>();

            SetPosition(Vector2f(80.0f, 35.0f));
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
            auto *add_parameter = add_button("+ Parameter", 90.0f);
            add_parameter->OnMouseClick() += [this](UI::UIEvent &e) { AddParameter(); e._is_handled = true; };
            auto *add_state = add_button("+ State", 64.0f);
            add_state->OnMouseClick() += [this](UI::UIEvent &e) { AddState(); e._is_handled = true; };
            auto *add_transition = add_button("+ Transition", 92.0f);
            add_transition->OnMouseClick() += [this](UI::UIEvent &e) { AddTransition(); e._is_handled = true; };
            _txt_status = toolbar->AddChild<UI::Text>("Saved");
            StyleText(_txt_status, kMutedTextColor);
            _txt_status->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                    .Margin(Vector4f(8.0f, 0.0f, 0.0f, 0.0f));

            auto *main = root->AddChild<UI::SplitView>();
            main->_is_horizontal = true;
            main->SetRatio(0.27f);
            main->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto *left_border = main->AddChild<UI::Border>();
            left_border->_bg_color = kPanelColor;
            auto *left_scroll = left_border->AddChild<UI::ScrollView>();
            left_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            auto *left = left_scroll->AddChild<UI::VerticalBox>();
            left->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            left->SlotPadding() = UI::Padding(5.0f);
            AddSectionTitle(left, "Parameters");
            _parameters_root = left->AddChild<UI::VerticalBox>();
            _parameters_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            AddSectionTitle(left, "States");
            _states_root = left->AddChild<UI::VerticalBox>();
            _states_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);

            auto *right = main->AddChild<UI::SplitView>();
            right->_is_horizontal = true;
            right->SetRatio(0.72f);
            auto *graph_border = right->AddChild<UI::Border>();
            graph_border->_bg_color = kCenterColor;
            _graph_canvas = graph_border->AddChild<GraphCanvas>(_graph_document.get());
            _graph_canvas->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            GraphCanvasPresentation presentation;
            presentation._link_route = EGraphLinkRoute::kStateTransition;
            presentation._pin_presentation = EGraphPinPresentation::kHoverOnly;
            presentation._draw_direction_arrow = true;
            presentation._separate_bidirectional_links = true;
            presentation._node_as_link_target = true;
            presentation._allow_reroute = false;
            _graph_canvas->SetPresentation(presentation);

            auto *inspector_border = right->AddChild<UI::Border>();
            inspector_border->_bg_color = kPanelColor;
            auto *inspector_scroll = inspector_border->AddChild<UI::ScrollView>();
            inspector_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _details_root = inspector_scroll->AddChild<UI::VerticalBox>();
            _details_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            _details_root->SlotPadding() = UI::Padding(6.0f);
            _transitions_root = nullptr;
        }

        void AnimationControllerEditor::OnBeforeSave()
        {
            if (_controller != nullptr)
                WriteToAsset();
        }

        void AnimationControllerEditor::OnAssetSaved()
        {
            _last_edit_snapshot = CaptureAssetObject(GetAsset());
            RefreshAllUI();
        }

        void AnimationControllerEditor::OnAssetReloaded()
        {
            _controller = GetAssetObject<AnimationControllerAsset>();
            if (_controller != nullptr)
                Open(_controller);
        }

        void AnimationControllerEditor::Update(f32 dt)
        {
            AssetEditor::Update(dt);
            if (!_controller)
                return;
            const bool ctrl = Input::IsKeyDown(EKey::kLCONTROL) || Input::IsKeyDown(EKey::kRCONTROL);
            if (ctrl && Input::IsKeyDownAccurate(EKey::kZ) && g_pCommandMgr != nullptr)
            {
                g_pCommandMgr->Undo();
                ReadFromAsset();
                RefreshGraphFromController();
                RefreshAllUI();
            }
            if (ctrl && Input::IsKeyDownAccurate(EKey::kY) && g_pCommandMgr != nullptr)
            {
                g_pCommandMgr->Redo();
                ReadFromAsset();
                RefreshGraphFromController();
                RefreshAllUI();
            }
            if (_graph_document != nullptr)
            {
                SyncGraphPositions();
                const String node_signature = MakeGraphSignature(_graph_document->Nodes(), {});
                const String link_signature = MakeGraphSignature({}, _graph_document->Links());
                const bool nodes_changed = node_signature != _graph_node_signature;
                const bool links_changed = link_signature != _graph_link_signature;
                if (nodes_changed || links_changed)
                    SyncControllerFromGraph(nodes_changed);

                if (_graph_canvas != nullptr && !_graph_canvas->SelectedNodes().empty())
                {
                    const GraphNodeData *selected_node = _graph_document->FindNode(_graph_canvas->SelectedNodes().front());
                    if (selected_node != nullptr)
                    {
                        const i32 state_index = ParseStateIndex(selected_node->_property_data);
                        if (state_index >= 0 && state_index < static_cast<i32>(_editing_states.size()) &&
                            state_index != _selected_state)
                        {
                            _selected_state = state_index;
                            _selected_transition = -1;
                            RefreshAllUI();
                        }
                    }
                }
                if (_graph_canvas != nullptr && !_graph_canvas->SelectedLinks().empty())
                {
                    const auto transition_it = _transition_link_map.find(_graph_canvas->SelectedLinks().front());
                    if (transition_it != _transition_link_map.end() && transition_it->second != _selected_transition)
                    {
                        _selected_transition = transition_it->second;
                        _selected_state = -1;
                        RefreshAllUI();
                    }
                }
            }
        }

        void AnimationControllerEditor::Open(AnimationControllerAsset *controller)
        {
            if (!controller)
                return;
            BindAsset(ResourceMgr::Get().GetLinkedAsset(controller));
            _controller = controller;
            ReadFromAsset();
            _last_edit_snapshot = CaptureAssetObject(GetAsset());
            _selected_state = _editing_states.empty() ? -1 : 0;
            _selected_transition = _editing_transitions.empty() ? -1 : 0;
            _transition_link_map.clear();
            _graph_view_initialized = false;
            RefreshGraphFromController();
            RefreshAllUI();
        }

        void AnimationControllerEditor::Close()
        {
            _controller = nullptr;
            _editing_parameters.clear();
            _editing_states.clear();
            _editing_transitions.clear();
            _editing_any_state_transitions.clear();
            _transition_link_map.clear();
            _graph_view_initialized = false;
            if (_graph_document != nullptr)
                _graph_document->Close();
            AssetEditor::Close();
        }

        void AnimationControllerEditor::ReadFromAsset()
        {
            _editing_parameters = _controller->Parameters();
            _editing_states = _controller->States();
            _editing_transitions = _controller->Transitions();
            _editing_any_state_transitions = _controller->AnyStateTransitions();
            _editing_entry_state = _controller->EntryState();
            for (auto &state : _editing_states)
            {
                const String asset_name = MotionAssetName(state._motion);
                if (!asset_name.empty())
                    state._name = asset_name;
            }
        }

        void AnimationControllerEditor::WriteToAsset()
        {
            RebuildTransitionLinks();
            _controller->Parameters() = _editing_parameters;
            _controller->States() = _editing_states;
            _controller->Transitions() = _editing_transitions;
            _controller->AnyStateTransitions() = _editing_any_state_transitions;
            _controller->EntryState(_editing_entry_state);
        }

        void AnimationControllerEditor::MarkDirty()
        {
            if (_controller != nullptr)
                WriteToAsset();
            if (GetAsset() != nullptr)
            {
                const String snapshot = CaptureAssetObject(GetAsset());
                if (snapshot != _last_edit_snapshot)
                {
                    const String before = _last_edit_snapshot;
                    _last_edit_snapshot = snapshot;
                    if (g_pCommandMgr != nullptr)
                        g_pCommandMgr->ExecuteCommand(std::make_unique<AssetSnapshotCommand>(
                            GetAsset(), before, snapshot, "Animation Controller Edit"));
                    else if (!GetAsset()->IsDirty())
                        GetAsset()->MarkModified();
                }
            }
            if (_txt_status)
                _txt_status->SetText("Modified");
        }

        void AnimationControllerEditor::EnsureGraphNodeRegistry()
        {
            RegisterAnimationGraphNode({"Animation.Entry", "Entry", "Animation", "Controller entry point", {0.20f, 0.35f, 0.55f, 1.0f},
                                        {170.0f, 70.0f}, {{"Out", "", EGraphPinDirection::kOutput, EGraphPinKind::kExecution, ""}},
                                        GraphFlag(EGraphNodeFlag::kEntryNode)});
            RegisterAnimationGraphNode({"Animation.AnyState", "Any State", "Animation", "Any state transition source",
                                        {0.35f, 0.28f, 0.55f, 1.0f}, {170.0f, 70.0f},
                                        {{"Out", "", EGraphPinDirection::kOutput, EGraphPinKind::kExecution, ""}}, 0u});
            RegisterAnimationGraphNode({"Animation.State", "State", "Animation", "Animation state",
                                        {0.24f, 0.42f, 0.28f, 1.0f}, {220.0f, 110.0f},
                                        {{"In", "", EGraphPinDirection::kInput, EGraphPinKind::kExecution, ""},
                                         {"Out", "", EGraphPinDirection::kOutput, EGraphPinKind::kExecution, ""}},
                                        GraphFlag(EGraphNodeFlag::kCanDelete) | GraphFlag(EGraphNodeFlag::kCanDuplicate)});
        }

        void AnimationControllerEditor::RefreshGraphFromController()
        {
            if (_graph_asset == nullptr || _graph_document == nullptr)
                return;

            const bool preserve_view = _graph_view_initialized && _graph_canvas != nullptr;
            const Vector2f view_offset = preserve_view ? _graph_canvas->ViewOffset() : Vector2f::kZero;
            const f32 zoom = preserve_view ? _graph_canvas->Zoom() : 1.0f;

            Vector<Vector2f> positions(_editing_states.size());
            for (u32 index = 0u; index < positions.size(); ++index)
                positions[index] = {360.0f + static_cast<f32>(index % 3u) * 280.0f,
                                    90.0f + static_cast<f32>(index / 3u) * 170.0f};
            for (const auto &node : _graph_document->Nodes())
            {
                const i32 state_index = ParseStateIndex(node._property_data);
                if (state_index >= 0 && state_index < static_cast<i32>(positions.size()))
                    positions[state_index] = node._position;
            }

            auto &nodes = _graph_asset->MutableNodes();
            auto &links = _graph_asset->MutableLinks();
            nodes.clear();
            links.clear();
            _transition_link_map.clear();
            _graph_asset->MutableComments().clear();

            GraphNodeData entry;
            GraphNodeRegistry::Get().InitializeNode("Animation.Entry", entry);
            entry._position = {40.0f, 80.0f};
            nodes.emplace_back(std::move(entry));
            GraphNodeData any_state;
            GraphNodeRegistry::Get().InitializeNode("Animation.AnyState", any_state);
            any_state._position = {40.0f, 260.0f};
            nodes.emplace_back(std::move(any_state));
            for (u32 index = 0u; index < _editing_states.size(); ++index)
            {
                GraphNodeData node;
                GraphNodeRegistry::Get().InitializeNode("Animation.State", node);
                node._display_name = StateDisplayName(_editing_states[index], index);
                node._property_data = std::format("state:{}", index);
                node._position = positions[index];
                nodes.emplace_back(std::move(node));
            }

            auto add_link = [this, &nodes, &links](u32 output_node_index, u32 input_node_index,
                                                   i32 transition_index = -1)
            {
                if (output_node_index >= nodes.size() || input_node_index >= nodes.size())
                    return;
                const GraphPinData *output = FindPin(nodes[output_node_index], "Out", EGraphPinDirection::kOutput);
                const GraphPinData *input = FindPin(nodes[input_node_index], "In", EGraphPinDirection::kInput);
                if (output == nullptr || input == nullptr)
                    return;
                const Guid link_id = Guid::Generate();
                links.push_back({link_id, output->_id, input->_id, GraphFlag(EGraphLinkFlag::kNone)});
                if (transition_index >= 0)
                    _transition_link_map[link_id] = transition_index;
            };
            if (_editing_entry_state < _editing_states.size())
                add_link(0u, 2u + _editing_entry_state);
            for (u32 transition_index = 0u; transition_index < _editing_transitions.size(); ++transition_index)
            {
                const auto &transition = _editing_transitions[transition_index];
                if (transition._to_state >= _editing_states.size())
                    continue;
                const u32 source_index =
                    transition._from_state == kInvalidAnimationState ? 1u : 2u + transition._from_state;
                if (source_index < nodes.size())
                    add_link(source_index, 2u + transition._to_state,
                             static_cast<i32>(transition_index));
            }

            _graph_document->Open(_graph_asset.get());
            RefreshGraphNodeTitles();
            if (_graph_canvas != nullptr)
            {
                _graph_canvas->SetDocument(_graph_document.get());
                _graph_canvas->ClearSelection();
                if (!preserve_view)
                {
                    _graph_canvas->FocusAll();
                    _graph_view_initialized = true;
                }
                else
                {
                    if (_selected_transition >= 0 &&
                        _selected_transition < static_cast<i32>(_editing_transitions.size()))
                    {
                        for (const auto &entry : _transition_link_map)
                        {
                            if (entry.second == _selected_transition)
                            {
                                _graph_canvas->FocusLink(entry.first);
                                break;
                            }
                        }
                    }
                    else if (_selected_state >= 0 && _selected_state < static_cast<i32>(_editing_states.size()))
                    {
                        for (const auto &node : _graph_document->Nodes())
                        {
                            if (ParseStateIndex(node._property_data) == _selected_state)
                            {
                                _graph_canvas->FocusNode(node._id);
                                break;
                            }
                        }
                    }
                    _graph_canvas->SetView(view_offset, zoom);
                }
            }
            _graph_node_signature = MakeGraphSignature(_graph_document->Nodes(), {});
            _graph_link_signature = MakeGraphSignature({}, _graph_document->Links());
        }

        void AnimationControllerEditor::SyncControllerFromGraph(bool nodes_changed)
        {
            if (_graph_document == nullptr)
                return;

            Vector<Guid> state_node_ids;
            Vector<i32> old_indices;
            Vector<bool> used(_editing_states.size(), false);
            Vector<AnimationState> states;
            for (const auto &node : _graph_document->Nodes())
            {
                if (node._node_type != "Animation.State")
                    continue;
                const i32 old_index = ParseStateIndex(node._property_data);
                if (old_index >= 0 && old_index < static_cast<i32>(_editing_states.size()) && !used[old_index])
                {
                    used[old_index] = true;
                    states.push_back(_editing_states[old_index]);
                    old_indices.push_back(old_index);
                }
                else
                {
                    AnimationState state;
                    state._name = node._display_name.empty() ? std::format("State{}", states.size() + 1u) : node._display_name;
                    states.push_back(std::move(state));
                    old_indices.push_back(-1);
                }
                state_node_ids.push_back(node._id);
            }
            for (u32 index = 0u; index < state_node_ids.size(); ++index)
            {
                if (auto *node = _graph_document->FindNode(state_node_ids[index]))
                {
                    node->_property_data = std::format("state:{}", index);
                    const String asset_name = MotionAssetName(states[index]._motion);
                    if (!asset_name.empty())
                        states[index]._name = asset_name;
                    else if (old_indices[index] < 0 && !node->_display_name.empty())
                        states[index]._name = node->_display_name;
                }
            }

            auto state_index_for_node = [this, &state_node_ids](const Guid &node_id) -> i32
            {
                for (u32 index = 0u; index < state_node_ids.size(); ++index)
                    if (state_node_ids[index] == node_id)
                        return static_cast<i32>(index);
                return -1;
            };
            auto node_for_pin = [this](const Guid &pin_id) -> const GraphNodeData *
            {
                return _graph_document->FindNodeByPin(pin_id);
            };
            auto old_index_for_new = [&old_indices](i32 index) -> u16
            {
                return index >= 0 && index < static_cast<i32>(old_indices.size()) && old_indices[index] >= 0 ?
                           static_cast<u16>(old_indices[index]) : kInvalidAnimationState;
            };
            auto find_existing = [this](u16 from_state, u16 to_state) -> const AnimationTransition *
            {
                for (const auto &transition : _editing_transitions)
                    if (transition._from_state == from_state && transition._to_state == to_state)
                        return &transition;
                return nullptr;
            };

            Vector<AnimationTransition> transitions;
            Vector<Guid> transition_link_ids;
            u16 entry_state = kInvalidAnimationState;
            for (const auto &link : _graph_document->Links())
            {
                const GraphNodeData *source = node_for_pin(link._output_pin);
                const GraphNodeData *target = node_for_pin(link._input_pin);
                if (source == nullptr || target == nullptr || target->_node_type != "Animation.State")
                    continue;
                const i32 target_index = state_index_for_node(target->_id);
                if (target_index < 0)
                    continue;
                if (source->_node_type == "Animation.Entry")
                {
                    entry_state = static_cast<u16>(target_index);
                    continue;
                }
                const i32 source_index = source->_node_type == "Animation.AnyState" ? -1 : state_index_for_node(source->_id);
                if (source->_node_type != "Animation.AnyState" && source_index < 0)
                    continue;
                const u16 from_state = source_index < 0 ? kInvalidAnimationState : static_cast<u16>(source_index);
                const u16 old_from = source_index < 0 ? kInvalidAnimationState : old_index_for_new(source_index);
                const u16 old_to = old_index_for_new(target_index);
                AnimationTransition transition;
                if (const auto *existing = find_existing(old_from, old_to))
                    transition = *existing;
                transition._from_state = from_state;
                transition._to_state = static_cast<u16>(target_index);
                transitions.push_back(std::move(transition));
                transition_link_ids.push_back(link._id);
            }
            _editing_states = std::move(states);
            _editing_transitions = std::move(transitions);
            _transition_link_map.clear();
            for (u32 index = 0u; index < transition_link_ids.size(); ++index)
                _transition_link_map[transition_link_ids[index]] = static_cast<i32>(index);
            _editing_entry_state = entry_state;
            RebuildTransitionLinks();
            _selected_state = _editing_states.empty() ? -1 : std::clamp(_selected_state, 0, static_cast<i32>(_editing_states.size()) - 1);
            _selected_transition = _editing_transitions.empty() ? -1 : std::clamp(_selected_transition, 0,
                                                                                    static_cast<i32>(_editing_transitions.size()) - 1);
            MarkDirty();
            RefreshGraphNodeTitles();
            if (nodes_changed)
                RefreshAllUI();
            else
                RefreshTransitions();
            _graph_node_signature = MakeGraphSignature(_graph_document->Nodes(), {});
            _graph_link_signature = MakeGraphSignature({}, _graph_document->Links());
        }

        void AnimationControllerEditor::SyncGraphPositions()
        {
            // Positions are intentionally kept in the in-memory graph document. AnimationControllerAsset remains runtime JSON.
        }

        void AnimationControllerEditor::RefreshGraphNodeTitles()
        {
            if (_graph_document == nullptr)
                return;
            for (const auto &node : _graph_document->Nodes())
            {
                const i32 state_index = ParseStateIndex(node._property_data);
                if (state_index < 0 || state_index >= static_cast<i32>(_editing_states.size()))
                    continue;
                if (auto *mutable_node = _graph_document->FindNode(node._id))
                    mutable_node->_display_name = StateDisplayName(_editing_states[state_index], state_index);
            }
        }

        void AnimationControllerEditor::RefreshAllUI()
        {
            if (_txt_status)
                _txt_status->SetText(IsDirty() ? "Modified" : "Saved");
            RefreshParameters();
            RefreshStates();
            if (_details_root == nullptr)
                return;
            _details_root->ClearChildren();
            _transitions_root = nullptr;
            AddSectionTitle(_details_root, "State Details");
            if (_selected_state < 0 || _selected_state >= static_cast<i32>(_editing_states.size()))
            {
                StyleText(_details_root->AddChild<UI::Text>("Select a state to edit its motion."), kMutedTextColor);
            }
            else
            {
                auto &state = _editing_states[_selected_state];
                AddTextInput(_details_root, "Name", state._name, [this](String value)
                {
                    if (_selected_state >= 0 && _selected_state < static_cast<i32>(_editing_states.size()))
                    {
                        _editing_states[_selected_state]._name = std::move(value);
                        MarkDirty();
                        RefreshStates();
                        RefreshGraphFromController();
                    }
                });
                auto *motion_row = AddPropertyRow(_details_root, "Motion");
                auto *motion = motion_row->AddChild<UI::Dropdown>(MotionTypeNames());
                motion->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
                motion->SetSelectedIndex(static_cast<i32>(state._motion._type));
                motion->_on_selected_changed += [this](i32 index)
                {
                    if (_selected_state >= 0 && _selected_state < static_cast<i32>(_editing_states.size()))
                    {
                        _editing_states[_selected_state]._motion._type = index == 1 ? EAnimationMotionType::kBlendSpace :
                                                                                     EAnimationMotionType::kClip;
                        MarkDirty();
                    }
                };
                auto *asset_button = AddButtonRow(_details_root, "Asset", "None");
                if (!state._motion._asset.IsEmpty())
                {
                    const String asset_name = MotionAssetName(state._motion);
                    if (!asset_name.empty())
                        asset_button->SetText(asset_name);
                    else
                        asset_button->SetText(state._motion._asset.ToString());
                }
                asset_button->OnMouseClick() += [this](UI::UIEvent &event)
                {
                    ShowMotionPicker(static_cast<u32>(_selected_state), event._current_target);
                    event._is_handled = true;
                };
                AddFloatInput(_details_root, "Speed", state._speed, [this](f32 value)
                {
                    if (_selected_state >= 0 && _selected_state < static_cast<i32>(_editing_states.size()))
                    {
                        _editing_states[_selected_state]._speed = std::max(value, 0.0f);
                        MarkDirty();
                    }
                });
                auto *loop_row = AddPropertyRow(_details_root, "Loop");
                auto *loop = loop_row->AddChild<UI::CheckBox>();
                loop->SetChecked(state._loop);
                loop->_on_click += [this](bool value)
                {
                    if (_selected_state >= 0 && _selected_state < static_cast<i32>(_editing_states.size()))
                    {
                        _editing_states[_selected_state]._loop = value;
                        MarkDirty();
                    }
                };
                auto *entry = _details_root->AddChild<UI::Button>(_editing_entry_state == _selected_state ? "Entry State" : "Set Entry State");
                entry->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                        .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(0.0f, 5.0f, 0.0f, 5.0f));
                entry->OnMouseClick() += [this](UI::UIEvent &event)
                {
                    _editing_entry_state = static_cast<u16>(_selected_state);
                    MarkDirty();
                    RefreshGraphFromController();
                    RefreshAllUI();
                    event._is_handled = true;
                };
            }

            _transitions_root = _details_root->AddChild<UI::VerticalBox>();
            _transitions_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            _transitions_root->SlotPadding() = UI::Padding(0.0f, 8.0f, 0.0f, 0.0f);
            RefreshTransitions();
        }

        void AnimationControllerEditor::RefreshParameters()
        {
            if (!_parameters_root)
                return;
            _parameters_root->ClearChildren();
            if (_editing_parameters.empty())
            {
                StyleText(_parameters_root->AddChild<UI::Text>("None"), kMutedTextColor);
                return;
            }
            for (u32 index = 0u; index < _editing_parameters.size(); ++index)
            {
                auto &parameter = _editing_parameters[index];
                auto *row = _parameters_root->AddChild<UI::HorizontalBox>();
                row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                        .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(0.0f, 1.0f, 0.0f, 1.0f));
                auto *name = AddInputToRow(row, parameter._name);
                name->GetSlotAs<UI::LinearSlot>().FillRate(2.0f);
                name->_on_content_changed += [this, index](String value)
                {
                    if (index < _editing_parameters.size())
                    {
                        _editing_parameters[index]._name = std::move(value);
                        _editing_parameters[index]._name_hash = AnimationParameterNameHash(_editing_parameters[index]._name);
                        MarkDirty();
                    }
                };
                auto *type = row->AddChild<UI::Dropdown>(ParameterTypeNames());
                type->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                        .FillRate(1.5f).Margin(Vector4f(2.0f, 1.0f, 2.0f, 1.0f));
                type->SetSelectedIndex(static_cast<i32>(parameter._type));
                type->_on_selected_changed += [this, index](i32 selected)
                {
                    if (index < _editing_parameters.size() && selected >= 0 && selected <= 3)
                    {
                        _editing_parameters[index]._type = static_cast<EAnimationParameterType>(selected);
                        MarkDirty();
                        RefreshTransitions();
                    }
                };
                auto *remove = row->AddChild<UI::Button>("X");
                remove->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                        .Size(Vector2f(24.0f, 0.0f));
                remove->OnMouseClick() += [this, index](UI::UIEvent &event)
                {
                    RemoveParameter(index);
                    event._is_handled = true;
                };
            }
        }

        void AnimationControllerEditor::RefreshStates()
        {
            if (!_states_root)
                return;
            _states_root->ClearChildren();
            if (_editing_states.empty())
            {
                StyleText(_states_root->AddChild<UI::Text>("None"), kMutedTextColor);
                return;
            }
            for (u32 index = 0u; index < _editing_states.size(); ++index)
            {
                auto *row = _states_root->AddChild<UI::HorizontalBox>();
                row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                        .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(0.0f, 1.0f, 0.0f, 1.0f));
                auto *select = row->AddChild<UI::Button>(StateDisplayName(_editing_states[index], index));
                select->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                        .Margin(Vector4f(0.0f, 0.0f, 2.0f, 0.0f));
                select->SetInteractiveEnabled(static_cast<i32>(index) != _selected_state);
                select->OnMouseClick() += [this, index](UI::UIEvent &event)
                {
                    _selected_state = static_cast<i32>(index);
                    if (_graph_canvas != nullptr && _graph_document != nullptr)
                    {
                        for (const auto &node : _graph_document->Nodes())
                        {
                            if (ParseStateIndex(node._property_data) == static_cast<i32>(index))
                            {
                                _graph_canvas->FocusNode(node._id);
                                break;
                            }
                        }
                    }
                    RefreshAllUI();
                    event._is_handled = true;
                };
                auto *remove = row->AddChild<UI::Button>("X");
                remove->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                        .Size(Vector2f(24.0f, 0.0f));
                remove->OnMouseClick() += [this, index](UI::UIEvent &event)
                {
                    RemoveState(index);
                    event._is_handled = true;
                };
            }
        }

        void AnimationControllerEditor::RefreshTransitions()
        {
            if (!_transitions_root)
                return;
            _transitions_root->ClearChildren();
            AddSectionTitle(_transitions_root, std::format("Transitions ({})", _editing_transitions.size()));
            for (u32 index = 0u; index < _editing_transitions.size(); ++index)
            {
                const auto &transition = _editing_transitions[index];
                auto *button = _transitions_root->AddChild<UI::Button>(std::format("{} -> {}", StateName(_editing_states, transition._from_state),
                                                                                    StateName(_editing_states, transition._to_state)));
                button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                        .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(0.0f, 1.0f, 0.0f, 1.0f));
                button->SetInteractiveEnabled(static_cast<i32>(index) != _selected_transition);
                button->OnMouseClick() += [this, index](UI::UIEvent &event)
                {
                    _selected_transition = static_cast<i32>(index);
                    RefreshAllUI();
                    event._is_handled = true;
                };
            }
            if (_selected_transition < 0 || _selected_transition >= static_cast<i32>(_editing_transitions.size()))
                return;
            auto &transition = _editing_transitions[_selected_transition];
            AddSectionTitle(_transitions_root, "Transition Details");
            Vector<String> from_items = {"Any State"};
            Vector<String> to_items;
            for (const auto &state : _editing_states)
            {
                from_items.push_back(state._name);
                to_items.push_back(state._name);
            }
            auto *from_row = AddPropertyRow(_transitions_root, "From");
            auto *from = from_row->AddChild<UI::Dropdown>(from_items);
            from->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            from->SetSelectedIndex(transition._from_state == kInvalidAnimationState ? 0 : static_cast<i32>(transition._from_state) + 1);
            from->_on_selected_changed += [this](i32 selected)
            {
                if (_selected_transition < 0 || _selected_transition >= static_cast<i32>(_editing_transitions.size()))
                    return;
                _editing_transitions[_selected_transition]._from_state = selected <= 0 ? kInvalidAnimationState : static_cast<u16>(selected - 1);
                RebuildTransitionLinks();
                MarkDirty();
                RefreshGraphFromController();
                RefreshTransitions();
            };
            auto *to_row = AddPropertyRow(_transitions_root, "To");
            auto *to = to_row->AddChild<UI::Dropdown>(to_items);
            to->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            to->SetSelectedIndex(transition._to_state < _editing_states.size() ? static_cast<i32>(transition._to_state) : -1);
            to->_on_selected_changed += [this](i32 selected)
            {
                if (_selected_transition < 0 || _selected_transition >= static_cast<i32>(_editing_transitions.size()))
                    return;
                _editing_transitions[_selected_transition]._to_state = selected < 0 ? kInvalidAnimationState : static_cast<u16>(selected);
                MarkDirty();
                RefreshGraphFromController();
                RefreshTransitions();
            };
            AddFloatInput(_transitions_root, "Duration", transition._duration, [this](f32 value)
            {
                if (_selected_transition >= 0 && _selected_transition < static_cast<i32>(_editing_transitions.size()))
                {
                    _editing_transitions[_selected_transition]._duration = std::max(value, 0.0f);
                    MarkDirty();
                }
            });
            auto *exit_row = AddPropertyRow(_transitions_root, "Exit Time");
            auto *exit_check = exit_row->AddChild<UI::CheckBox>();
            exit_check->SetChecked(transition._has_exit_time);
            exit_check->_on_click += [this](bool value)
            {
                if (_selected_transition >= 0 && _selected_transition < static_cast<i32>(_editing_transitions.size()))
                {
                    _editing_transitions[_selected_transition]._has_exit_time = value;
                    MarkDirty();
                }
            };
            AddFloatInput(_transitions_root, "Exit Normalized", transition._exit_time, [this](f32 value)
            {
                if (_selected_transition >= 0 && _selected_transition < static_cast<i32>(_editing_transitions.size()))
                {
                    _editing_transitions[_selected_transition]._exit_time = std::clamp(value, 0.0f, 1.0f);
                    MarkDirty();
                }
            });
            auto *add_condition = _transitions_root->AddChild<UI::Button>("+ Condition");
            add_condition->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(0.0f, 5.0f, 0.0f, 2.0f));
            add_condition->OnMouseClick() += [this](UI::UIEvent &event)
            {
                AddCondition();
                event._is_handled = true;
            };
            for (u32 condition_index = 0u; condition_index < transition._conditions.size(); ++condition_index)
            {
                auto &condition = transition._conditions[condition_index];
                Vector<String> parameter_names;
                for (const auto &parameter : _editing_parameters)
                    parameter_names.push_back(parameter._name);
                auto *parameter_row = AddPropertyRow(_transitions_root, "Parameter");
                auto *parameter = parameter_row->AddChild<UI::Dropdown>(parameter_names);
                parameter->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
                parameter->SetSelectedIndex(condition._parameter_index < _editing_parameters.size() ? condition._parameter_index : -1);
                parameter->_on_selected_changed += [this, condition_index](i32 selected)
                {
                    if (_selected_transition >= 0 && _selected_transition < static_cast<i32>(_editing_transitions.size()) && selected >= 0)
                    {
                        _editing_transitions[_selected_transition]._conditions[condition_index]._parameter_index = static_cast<u16>(selected);
                        MarkDirty();
                        RefreshTransitions();
                    }
                };
                Vector<String> operation_names;
                Vector<EAnimationConditionOp> operation_values;
                if (condition._parameter_index < _editing_parameters.size())
                {
                    switch (_editing_parameters[condition._parameter_index]._type)
                    {
                    case EAnimationParameterType::kTrigger:
                        operation_names = {"Triggered"};
                        operation_values = {EAnimationConditionOp::kTriggered};
                        break;
                    case EAnimationParameterType::kBool:
                        operation_names = {"Equal", "Not Equal"};
                        operation_values = {EAnimationConditionOp::kEqual, EAnimationConditionOp::kNotEqual};
                        break;
                    default:
                        operation_names = {"Equal", "Not Equal", "Greater", "Greater Equal", "Less", "Less Equal"};
                        operation_values = {EAnimationConditionOp::kEqual, EAnimationConditionOp::kNotEqual,
                                            EAnimationConditionOp::kGreater, EAnimationConditionOp::kGreaterEqual,
                                            EAnimationConditionOp::kLess, EAnimationConditionOp::kLessEqual};
                        break;
                    }
                }
                else
                {
                    operation_names = ConditionOperationNames();
                    operation_values = {EAnimationConditionOp::kEqual, EAnimationConditionOp::kNotEqual,
                                        EAnimationConditionOp::kGreater, EAnimationConditionOp::kGreaterEqual,
                                        EAnimationConditionOp::kLess, EAnimationConditionOp::kLessEqual,
                                        EAnimationConditionOp::kTriggered};
                }
                auto *operation_row = AddPropertyRow(_transitions_root, "Operation");
                auto *operation = operation_row->AddChild<UI::Dropdown>(operation_names);
                operation->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
                i32 operation_index = 0;
                for (i32 op_index = 0; op_index < static_cast<i32>(operation_values.size()); ++op_index)
                {
                    if (operation_values[op_index] == condition._op)
                    {
                        operation_index = op_index;
                        break;
                    }
                }
                operation->SetSelectedIndex(operation_index);
                operation->_on_selected_changed += [this, condition_index, operation_values](i32 selected)
                {
                    if (_selected_transition >= 0 && _selected_transition < static_cast<i32>(_editing_transitions.size()) &&
                        selected >= 0 && selected < static_cast<i32>(operation_values.size()))
                    {
                        _editing_transitions[_selected_transition]._conditions[condition_index]._op = operation_values[selected];
                        MarkDirty();
                    }
                };
                if (condition._parameter_index < _editing_parameters.size() &&
                    _editing_parameters[condition._parameter_index]._type != EAnimationParameterType::kTrigger)
                {
                    f32 value = condition._float_value;
                    if (_editing_parameters[condition._parameter_index]._type == EAnimationParameterType::kInt)
                        value = static_cast<f32>(condition._int_value);
                    else if (_editing_parameters[condition._parameter_index]._type == EAnimationParameterType::kBool)
                        value = condition._bool_value ? 1.0f : 0.0f;
                    AddFloatInput(_transitions_root, "Value", value, [this, condition_index](f32 input)
                    {
                        if (_selected_transition < 0 || _selected_transition >= static_cast<i32>(_editing_transitions.size()))
                            return;
                        auto &current = _editing_transitions[_selected_transition]._conditions[condition_index];
                        if (current._parameter_index < _editing_parameters.size())
                        {
                            switch (_editing_parameters[current._parameter_index]._type)
                            {
                            case EAnimationParameterType::kInt: current._int_value = static_cast<i32>(input); break;
                            case EAnimationParameterType::kBool: current._bool_value = input != 0.0f; break;
                            default: current._float_value = input; break;
                            }
                        }
                        MarkDirty();
                    });
                }
                auto *remove_condition = _transitions_root->AddChild<UI::Button>("Remove Condition");
                remove_condition->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                        .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(0.0f, 0.0f, 0.0f, 4.0f));
                remove_condition->OnMouseClick() += [this, condition_index](UI::UIEvent &event)
                {
                    if (_selected_transition >= 0 && _selected_transition < static_cast<i32>(_editing_transitions.size()))
                    {
                        auto &conditions = _editing_transitions[_selected_transition]._conditions;
                        if (condition_index < conditions.size())
                            conditions.erase(conditions.begin() + condition_index);
                        MarkDirty();
                        RefreshTransitions();
                    }
                    event._is_handled = true;
                };
            }
            auto *remove = _transitions_root->AddChild<UI::Button>("Remove Transition");
            remove->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(0.0f, 8.0f, 0.0f, 2.0f));
            remove->OnMouseClick() += [this](UI::UIEvent &event)
            {
                RemoveTransition(static_cast<u32>(_selected_transition));
                event._is_handled = true;
            };
        }

        void AnimationControllerEditor::RebuildTransitionLinks()
        {
            for (auto &state : _editing_states)
                state._transitions.clear();
            _editing_any_state_transitions.clear();
            for (u16 index = 0u; index < _editing_transitions.size(); ++index)
            {
                auto &transition = _editing_transitions[index];
                if (transition._from_state == kInvalidAnimationState)
                    _editing_any_state_transitions.push_back(index);
                else if (transition._from_state < _editing_states.size())
                    _editing_states[transition._from_state]._transitions.push_back(index);
            }
        }

        void AnimationControllerEditor::AddParameter()
        {
            AnimationParameterDesc parameter;
            parameter._name = std::format("Parameter{}", _editing_parameters.size() + 1u);
            parameter._name_hash = AnimationParameterNameHash(parameter._name);
            _editing_parameters.push_back(parameter);
            MarkDirty();
            RefreshAllUI();
        }

        void AnimationControllerEditor::AddState()
        {
            AnimationState state;
            state._name = std::format("State{}", _editing_states.size() + 1u);
            _editing_states.push_back(state);
            if (_editing_entry_state == kInvalidAnimationState)
                _editing_entry_state = 0u;
            _selected_state = static_cast<i32>(_editing_states.size()) - 1;
            MarkDirty();
            RefreshGraphFromController();
            RefreshAllUI();
        }

        void AnimationControllerEditor::AddTransition()
        {
            if (_editing_states.size() < 2u)
                return;
            AnimationTransition transition;
            transition._from_state = _selected_state >= 0 ? static_cast<u16>(_selected_state) : kInvalidAnimationState;
            transition._to_state = transition._from_state == 0u ? 1u : 0u;
            _editing_transitions.push_back(std::move(transition));
            _selected_transition = static_cast<i32>(_editing_transitions.size()) - 1;
            RebuildTransitionLinks();
            MarkDirty();
            RefreshGraphFromController();
            RefreshAllUI();
        }

        void AnimationControllerEditor::AddCondition()
        {
            if (_selected_transition < 0 || _selected_transition >= static_cast<i32>(_editing_transitions.size()))
                return;
            if (_editing_parameters.empty())
            {
                AnimationParameterDesc parameter;
                parameter._name = "Parameter1";
                parameter._name_hash = AnimationParameterNameHash(parameter._name);
                _editing_parameters.push_back(std::move(parameter));
            }
            AnimationCondition condition;
            condition._parameter_index = 0u;
            _editing_transitions[_selected_transition]._conditions.push_back(condition);
            MarkDirty();
            RefreshAllUI();
        }

        void AnimationControllerEditor::RemoveParameter(u32 index)
        {
            if (index >= _editing_parameters.size())
                return;
            _editing_parameters.erase(_editing_parameters.begin() + index);
            for (auto &transition : _editing_transitions)
            {
                transition._conditions.erase(std::remove_if(transition._conditions.begin(), transition._conditions.end(),
                                                            [index](const AnimationCondition &condition)
                                                            { return condition._parameter_index == index; }),
                                             transition._conditions.end());
                for (auto &condition : transition._conditions)
                    if (condition._parameter_index > index)
                        --condition._parameter_index;
            }
            MarkDirty();
            RefreshAllUI();
        }

        void AnimationControllerEditor::RemoveState(u32 index)
        {
            if (index >= _editing_states.size())
                return;
            _editing_states.erase(_editing_states.begin() + index);
            _editing_transitions.erase(std::remove_if(_editing_transitions.begin(), _editing_transitions.end(),
                                                      [index](const AnimationTransition &transition)
                                                      {
                                                          return transition._from_state == index || transition._to_state == index;
                                                      }),
                                               _editing_transitions.end());
            for (auto &transition : _editing_transitions)
            {
                if (transition._from_state != kInvalidAnimationState && transition._from_state > index)
                    --transition._from_state;
                if (transition._to_state != kInvalidAnimationState && transition._to_state > index)
                    --transition._to_state;
            }
            if (_editing_entry_state == index)
                _editing_entry_state = _editing_states.empty() ? kInvalidAnimationState : 0u;
            else if (_editing_entry_state != kInvalidAnimationState && _editing_entry_state > index)
                --_editing_entry_state;
            _selected_state = _editing_states.empty() ? -1 : std::min(static_cast<i32>(index), static_cast<i32>(_editing_states.size()) - 1);
            _selected_transition = -1;
            RebuildTransitionLinks();
            MarkDirty();
            RefreshGraphFromController();
            RefreshAllUI();
        }

        void AnimationControllerEditor::RemoveTransition(u32 index)
        {
            if (index >= _editing_transitions.size())
                return;
            _editing_transitions.erase(_editing_transitions.begin() + index);
            _selected_transition = _editing_transitions.empty() ? -1 : std::min(static_cast<i32>(index),
                                                                                  static_cast<i32>(_editing_transitions.size()) - 1);
            RebuildTransitionLinks();
            MarkDirty();
            RefreshGraphFromController();
            RefreshAllUI();
        }

        void AnimationControllerEditor::ShowMotionPicker(u32 state_index, UI::UIElement *anchor)
        {
            if (state_index >= _editing_states.size())
                return;
            const bool is_blend_space = _editing_states[state_index]._motion._type == EAnimationMotionType::kBlendSpace;
            auto list = MakeRef<UI::ListView>();
            list->SetViewportHeight(220.0f);
            auto none = MakeRef<UI::Text>("None");
            none->OnMouseClick() += [this, state_index](UI::UIEvent &)
            {
                if (state_index < _editing_states.size())
                    _editing_states[state_index]._motion._asset = Guid::EmptyGuid();
                MarkDirty();
                UIManager::Get()->HidePopup();
                RefreshAllUI();
            };
            list->AddItem(none);
            for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
            {
                Asset *asset = it->second.get();
                const Type *asset_type = is_blend_space ? BlendSpaceAsset::StaticType() : AnimationClip::StaticType();
                if (asset == nullptr || asset->_asset_type != asset_type)
                    continue;
                const Guid guid = asset->GetGuid();
                AnimationMotion motion;
                motion._type = is_blend_space ? EAnimationMotionType::kBlendSpace : EAnimationMotionType::kClip;
                motion._asset = guid;
                const String asset_name = MotionAssetName(motion);
                if (asset_name.empty())
                    continue;
                auto item = MakeRef<UI::Text>(asset_name);
                item->OnMouseClick() += [this, state_index, guid](UI::UIEvent &)
                {
                    if (state_index < _editing_states.size())
                    {
                        auto &state = _editing_states[state_index];
                        state._motion._asset = guid;
                        const String asset_name = MotionAssetName(state._motion);
                        if (!asset_name.empty())
                            state._name = asset_name;
                    }
                    MarkDirty();
                    UIManager::Get()->HidePopup();
                    RefreshGraphFromController();
                    RefreshAllUI();
                };
                list->AddItem(item);
            }
            const auto rect = anchor->GetArrangeRect();
            UIManager::Get()->ShowPopupAt(rect.x, rect.y + rect.w, list);
        }

        UI::Text *AnimationControllerEditor::AddSectionTitle(UI::UIElement *parent, const String &title)
        {
            auto *text = parent->AddChild<UI::Text>(title);
            StyleText(text, Color(0.88f, 0.88f, 0.88f, 1.0f), 13.0f);
            text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, 25.0f)).Margin(Vector4f(3.0f, 4.0f, 0.0f, 2.0f));
            return text;
        }

        UI::HorizontalBox *AnimationControllerEditor::AddPropertyRow(UI::UIElement *parent, const String &label)
        {
            auto *row = parent->AddChild<UI::HorizontalBox>();
            row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(0.0f, 1.0f, 0.0f, 1.0f));
            auto *text = row->AddChild<UI::Text>(label);
            StyleText(text, kMutedTextColor);
            text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(105.0f, 0.0f));
            return row;
        }

        UI::InputBlock *AnimationControllerEditor::AddTextInput(UI::UIElement *parent, const String &label,
                                                                 const String &value,
                                                                 const std::function<void(String)> &on_changed)
        {
            auto *row = AddPropertyRow(parent, label);
            auto *input = AddInputToRow(row, value);
            input->_on_content_changed += std::function<void(String)>(on_changed);
            return input;
        }

        UI::InputBlock *AnimationControllerEditor::AddFloatInput(UI::UIElement *parent, const String &label, f32 value,
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
