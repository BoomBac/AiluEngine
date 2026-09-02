#include "Graph/GraphEditorWindow.h"
#include "Common/AssetEditorLayout.h"
#include "Dock/DockManager.h"
#include "Framework/Common/ResourceMgr.h"
#include "Graph/GraphCanvas.h"
#include "Graph/GraphNodeRegistry.h"
#include "Objects/JsonArchive.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"

#include <algorithm>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            constexpr f32 kToolbarHeight = 30.0f;
            constexpr f32 kStatusBarHeight = 22.0f;
            const Color kPanelBgColor = {0.13f, 0.14f, 0.15f, 1.0f};
            const Color kTextColor = {0.86f, 0.86f, 0.86f, 1.0f};
            const Color kMutedTextColor = {0.58f, 0.61f, 0.65f, 1.0f};

            UI::Text *AddText(UI::UIElement *parent, const String &text)
            {
                auto *label = parent->AddChild<UI::Text>(text);
                label->_color = kTextColor;
                label->FontSize(12.0f);
                label->_horizontal_align = UI::EAlignment::kLeft;
                label->_vertical_align = UI::EAlignment::kCenter;
                label->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size({0.0f, 24.0f}).Margin({4.0f, 1.0f, 4.0f, 1.0f});
                return label;
            }

            String ValidationSeverityText(EGraphValidationSeverity severity)
            {
                switch (severity)
                {
                    case EGraphValidationSeverity::kError:
                        return "Error";
                    case EGraphValidationSeverity::kWarning:
                        return "Warning";
                    default:
                        return "Info";
                }
            }
        }

        GraphEditorWindow::GraphEditorWindow() : AssetEditor("Graph Editor", {1120.0f, 700.0f})
        {
            SetPosition({140.0f, 70.0f});
            _document = MakeScope<GraphDocument>();
            _preview_asset = MakeScope<GraphAsset>("GraphEditorPreview");
            _document->Open(_preview_asset.get());
            BuildPreviewGraph();
            BuildContent();
        }

        GraphEditorWindow::~GraphEditorWindow() = default;

        void GraphEditorWindow::Update(f32 dt)
        {
            AssetEditor::Update(dt);
            if (IsDirty() != _last_known_dirty)
                RefreshWindowTitle();
            RefreshStatusBar();
            const String selection_signature = MakeSelectionSignature();
            if (selection_signature != _details_selection_signature)
            {
                _details_selection_signature = selection_signature;
                RefreshDetails();
            }
        }

        bool GraphEditorWindow::OnOpen()
        {
            GraphAsset *asset = GetAssetObject<GraphAsset>();
            if (asset == nullptr)
                return false;
            _asset = asset;
            if (_document == nullptr)
                _document = MakeScope<GraphDocument>();
            if (!_document->Open(asset, GetAsset()))
                return false;
            if (_canvas != nullptr)
            {
                _canvas->SetDocument(_document.get());
                _canvas->SetView(_saved_view_offset, _saved_zoom);
            }
            _details_selection_signature.clear();
            RefreshWindowTitle();
            RefreshDetails();
            RefreshStatusBar();
            return true;
        }

        void GraphEditorWindow::OnClose()
        {
            if (_canvas != nullptr)
                _canvas->SetDocument(nullptr);
            if (_document != nullptr)
                _document->Close();
            _asset = nullptr;
            _details_selection_signature.clear();
            RefreshWindowTitle();
            RefreshDetails();
            RefreshStatusBar();
        }

        void GraphEditorWindow::SaveDockLayoutState(JsonArchive &ar)
        {
            if (_main_split != nullptr)
                _left_panel_ratio = _main_split->GetRatio();
            if (_right_split != nullptr)
                _right_panel_ratio = _right_split->GetRatio();
            if (_canvas != nullptr)
            {
                _saved_view_offset = _canvas->ViewOffset();
                _saved_zoom = _canvas->Zoom();
            }
            ar.BeginObject("_left_panel_ratio");
            ar << _left_panel_ratio;
            ar.EndObject();
            ar.BeginObject("_right_panel_ratio");
            ar << _right_panel_ratio;
            ar.EndObject();
            ar.BeginObject("_view_offset_x");
            ar << _saved_view_offset.x;
            ar.EndObject();
            ar.BeginObject("_view_offset_y");
            ar << _saved_view_offset.y;
            ar.EndObject();
            ar.BeginObject("_zoom");
            ar << _saved_zoom;
            ar.EndObject();
        }

        void GraphEditorWindow::LoadDockLayoutState(JsonArchive &ar)
        {
            if (ar.HasField("_left_panel_ratio"))
            {
                ar.BeginObject("_left_panel_ratio");
                ar >> _left_panel_ratio;
                ar.EndObject();
            }
            if (ar.HasField("_right_panel_ratio"))
            {
                ar.BeginObject("_right_panel_ratio");
                ar >> _right_panel_ratio;
                ar.EndObject();
            }
            if (ar.HasField("_view_offset_x"))
            {
                ar.BeginObject("_view_offset_x");
                ar >> _saved_view_offset.x;
                ar.EndObject();
            }
            if (ar.HasField("_view_offset_y"))
            {
                ar.BeginObject("_view_offset_y");
                ar >> _saved_view_offset.y;
                ar.EndObject();
            }
            if (ar.HasField("_zoom"))
            {
                ar.BeginObject("_zoom");
                ar >> _saved_zoom;
                ar.EndObject();
            }
        }

        void GraphEditorWindow::OnDockLayoutLoaded()
        {
            if (_main_split != nullptr)
                _main_split->SetRatio(_left_panel_ratio);
            if (_right_split != nullptr)
                _right_split->SetRatio(_right_panel_ratio);
            if (_canvas != nullptr)
                _canvas->SetView(_saved_view_offset, _saved_zoom);
        }

        void GraphEditorWindow::BuildContent()
        {
            _content_root->ClearChildren();
            _content_root->CornerRadius(6.0f);
            auto *root = _content_root->AddChild<UI::VerticalBox>();
            root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto *toolbar = root->AddChild<UI::HorizontalBox>();
            toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                   .Size({0.0f, kToolbarHeight});
            BuildToolbar(toolbar);

            _main_split = root->AddChild<UI::SplitView>();
            _main_split->_is_horizontal = true;
            _main_split->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _main_split->SetRatio(_left_panel_ratio);

            auto *palette_border = _main_split->AddChild<UI::Border>();
            palette_border->_bg_color = kPanelBgColor;
            palette_border->Thickness(0.0f);
            palette_border->SlotPadding() = UI::Padding(4.0f);
            auto *palette_scroll = palette_border->AddChild<UI::ScrollView>();
            palette_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _palette_root = palette_scroll->AddChild<UI::VerticalBox>();
            _palette_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            BuildPalette(_palette_root);

            _right_split = _main_split->AddChild<UI::SplitView>();
            _right_split->_is_horizontal = true;
            _right_split->SetRatio(_right_panel_ratio);

            auto *center = _right_split->AddChild<UI::Border>();
            center->Thickness(0.0f);
            BuildCenterPanel(center);

            auto *details_border = _right_split->AddChild<UI::Border>();
            details_border->_bg_color = kPanelBgColor;
            details_border->Thickness(0.0f);
            details_border->SlotPadding() = UI::Padding(4.0f);
            auto *details_scroll = details_border->AddChild<UI::ScrollView>();
            details_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _details_root = details_scroll->AddChild<UI::VerticalBox>();
            _details_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            BuildDetails(_details_root);

            auto *status_bar = root->AddChild<UI::HorizontalBox>();
            status_bar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                      .Size({0.0f, kStatusBarHeight});
            BuildStatusBar(status_bar);
            RefreshStatusBar();
        }

        void GraphEditorWindow::BuildToolbar(UI::HorizontalBox *toolbar)
        {
            AddAssetMenu(toolbar);
            auto add_button = [this, toolbar](const String &text, std::function<void()> callback) -> UI::Button *
            {
                auto *button = toolbar->AddChild<UI::Button>(text);
                button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                      .Size({72.0f, 24.0f}).Margin({4.0f, 3.0f, 0.0f, 3.0f});
                button->OnMouseClick() += [this, callback](UI::UIEvent &e)
                {
                    callback();
                    e._is_handled = true;
                };
                return button;
            };
            _save_button = add_button("Save", [this]() { AssetEditor::Save(); });
            auto *spacer = toolbar->AddChild<UI::Text>("");
            spacer->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            auto *find_label = toolbar->AddChild<UI::Text>("Find");
            find_label->_color = kMutedTextColor;
            find_label->_horizontal_align = UI::EAlignment::kRight;
            find_label->_vertical_align = UI::EAlignment::kCenter;
            find_label->FontSize(12.0f);
            find_label->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                      .Size({38.0f, 24.0f}).Margin({4.0f, 3.0f, 4.0f, 3.0f});
            auto *find_input = toolbar->AddChild<UI::InputBlock>("");
            find_input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                      .Size({160.0f, 24.0f}).Margin({0.0f, 3.0f, 4.0f, 3.0f});
            find_input->_on_content_changed += [this](String content)
            {
                if (_canvas != nullptr && !content.empty())
                    _canvas->SearchAndFocusNode(content);
            };
            auto *focus_all = toolbar->AddChild<UI::Button>("Focus All");
            focus_all->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                     .Size({82.0f, 24.0f}).Margin({4.0f, 3.0f, 4.0f, 3.0f});
            focus_all->OnMouseClick() += [this](UI::UIEvent &e)
            {
                if (_canvas != nullptr)
                    _canvas->FocusAll();
                e._is_handled = true;
            };
        }

        void GraphEditorWindow::BuildPalette(UI::VerticalBox *palette)
        {
            palette->ClearChildren();
            AssetEditorLayout::AddSectionTitle(palette, "Palette");
            Vector<const GraphNodeDesc *> nodes = GraphNodeRegistry::Get().FindNodes("");
            std::sort(nodes.begin(), nodes.end(), [](const GraphNodeDesc *lhs, const GraphNodeDesc *rhs)
            {
                return lhs->_type_id < rhs->_type_id;
            });
            for (const GraphNodeDesc *node_desc : nodes)
            {
                const String label = node_desc->_display_name.empty() ? node_desc->_type_id : node_desc->_display_name;
                auto *button = palette->AddChild<UI::Button>(label);
                button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                      .Size({0.0f, 24.0f}).Margin({4.0f, 1.0f, 4.0f, 1.0f});
                button->OnMouseClick() += [this, node_type = node_desc->_type_id](UI::UIEvent &e)
                {
                    AddNodeFromPalette(node_type);
                    e._is_handled = true;
                };
            }
        }

        void GraphEditorWindow::BuildCenterPanel(UI::Border *center)
        {
            _canvas = center->AddChild<GraphCanvas>(_document.get());
            _canvas->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _canvas->SetView(_saved_view_offset, _saved_zoom);
        }

        void GraphEditorWindow::BuildDetails(UI::VerticalBox *details)
        {
            _details_root = details;
            RefreshDetails();
        }

        void GraphEditorWindow::BuildStatusBar(UI::HorizontalBox *status_bar)
        {
            _status_text = status_bar->AddChild<UI::Text>("Ready");
            _status_text->_color = kMutedTextColor;
            _status_text->FontSize(12.0f);
            _status_text->_horizontal_align = UI::EAlignment::kLeft;
            _status_text->_vertical_align = UI::EAlignment::kCenter;
            _status_text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                        .Margin({6.0f, 0.0f, 6.0f, 0.0f});
        }

        void GraphEditorWindow::BuildPreviewGraph()
        {
            if (_document == nullptr || !_document->Nodes().empty())
                return;

            const Guid entry_id = _document->AddNode("Flow.Entry", {0.0f, 10.0f});
            const Guid branch_id = _document->AddNode("Flow.Branch", {250.0f, 0.0f});
            const Guid bool_id = _document->AddNode("Literal.Bool", {250.0f, 190.0f});
            const Guid print_true_id = _document->AddNode("Flow.Print", {540.0f, -20.0f});
            const Guid print_false_id = _document->AddNode("Flow.Print", {540.0f, 150.0f});

            const GraphNodeData *entry = _document->FindNode(entry_id);
            const GraphNodeData *branch = _document->FindNode(branch_id);
            const GraphNodeData *literal_bool = _document->FindNode(bool_id);
            const GraphNodeData *print_true = _document->FindNode(print_true_id);
            const GraphNodeData *print_false = _document->FindNode(print_false_id);
            if (entry == nullptr || branch == nullptr || literal_bool == nullptr || print_true == nullptr ||
                print_false == nullptr)
                return;

            _document->AddLink(entry->_pins[0]._id, branch->_pins[0]._id);
            _document->AddLink(literal_bool->_pins[0]._id, branch->_pins[1]._id);
            _document->AddLink(branch->_pins[2]._id, print_true->_pins[0]._id);
            _document->AddLink(branch->_pins[3]._id, print_false->_pins[0]._id);
            _document->Apply();
        }

        void GraphEditorWindow::OnBeforeSave()
        {
            if (_document != nullptr)
                _document->Apply();
        }

        void GraphEditorWindow::OnAssetSaved()
        {
            RefreshWindowTitle();
            RefreshStatusBar();
        }

        void GraphEditorWindow::OnAssetReloaded()
        {
            GraphAsset *asset = GetAssetObject<GraphAsset>();
            if (asset != nullptr)
                OnOpen();
        }

        void GraphEditorWindow::RefreshDetails()
        {
            if (_details_root == nullptr)
                return;
            _details_root->ClearChildren();
            AssetEditorLayout::AddSectionTitle(_details_root, "Details");
            if (_document == nullptr || _canvas == nullptr)
            {
                AddText(_details_root, "No document");
                AddValidationPanel();
                return;
            }
            const auto &selected_nodes = _canvas->SelectedNodes();
            const auto &selected_comments = _canvas->SelectedComments();
            if (selected_nodes.empty() && selected_comments.size() == 1u)
            {
                GraphCommentData *comment = _document->FindComment(selected_comments[0]);
                if (comment == nullptr)
                {
                    AddText(_details_root, "Selection is invalid");
                    AddValidationPanel();
                    return;
                }
                auto *title_input = AssetEditorLayout::AddPropertyRow(_details_root, "Title")
                    ->AddChild<UI::InputBlock>(comment->_title);
                title_input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
                title_input->_on_content_changed += [this, comment_id = comment->_id](String content)
                {
                    if (_document != nullptr)
                        _document->Commands().Execute(MakeScope<SetGraphCommentTitleCommand>(comment_id, std::move(content)));
                    RefreshWindowTitle();
                    RefreshStatusBar();
                };
                AddValidationPanel();
                return;
            }
            if (selected_nodes.size() != 1u)
            {
                AddText(_details_root, selected_nodes.empty() ? "No node selected" : "Multiple items selected");
                AddValidationPanel();
                return;
            }
            GraphNodeData *node = _document->FindNode(selected_nodes[0]);
            if (node == nullptr)
            {
                AddText(_details_root, "Selection is invalid");
                AddValidationPanel();
                return;
            }

            AssetEditorLayout::AddPropertyRow(_details_root, "Type")->AddChild<UI::Text>(node->_node_type)
                ->GetSlotAs<UI::LinearSlot>()
                .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            auto *property_input = AssetEditorLayout::AddPropertyRow(_details_root, "Property")
                                            ->AddChild<UI::InputBlock>(node->_property_data);
            property_input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            property_input->_on_content_changed += [this, node_id = node->_id](String content)
            {
                if (_document != nullptr)
                    _document->Commands().Execute(MakeScope<SetGraphNodePropertyCommand>(node_id, std::move(content)));
                RefreshWindowTitle();
                RefreshStatusBar();
            };

            AssetEditorLayout::AddSectionTitle(_details_root, "Pins");
            for (const GraphPinData &pin : node->_pins)
            {
                auto *row = AssetEditorLayout::AddPropertyRow(
                    _details_root, std::format("{} {}", PinDirectionText(pin._direction), pin._name));
                auto *input = row->AddChild<UI::InputBlock>(pin._default_value);
                input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
                input->_on_content_changed += [this, pin_id = pin._id](String content)
                {
                    if (_document != nullptr)
                        _document->Commands().Execute(
                                MakeScope<SetGraphPinDefaultValueCommand>(pin_id, std::move(content)));
                    RefreshWindowTitle();
                    RefreshStatusBar();
                };
            }
            AddValidationPanel();
        }

        void GraphEditorWindow::AddValidationPanel()
        {
            if (_details_root == nullptr)
                return;

            AssetEditorLayout::AddSectionTitle(_details_root, "Validation");
            if (_document == nullptr)
            {
                AddText(_details_root, "No document");
                return;
            }

            const auto &messages = _document->ValidationMessages();
            if (messages.empty())
            {
                AddText(_details_root, "No messages");
                return;
            }

            for (const GraphValidationMessage &message : messages)
            {
                auto *button = _details_root->AddChild<UI::Button>(
                        std::format("{}: {}", ValidationSeverityText(message._severity), message._message));
                button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                      .Size({0.0f, 30.0f}).Margin({4.0f, 1.0f, 4.0f, 1.0f});
                button->OnMouseClick() += [this, message](UI::UIEvent &e)
                {
                    FocusValidationMessage(message);
                    e._is_handled = true;
                };
            }
        }

        void GraphEditorWindow::RefreshStatusBar()
        {
            if (_status_text == nullptr || _document == nullptr)
                return;
            const String dirty_text = IsDirty() ? "Dirty" : "Clean";
            const u32 error_count = static_cast<u32>(_document->ValidationMessages().size());
            _status_text->SetText(std::format("{} | Nodes: {} | Links: {} | Messages: {}", dirty_text,
                                              _document->Nodes().size(), _document->Links().size(), error_count),
                                  false);
        }

        void GraphEditorWindow::RefreshWindowTitle()
        {
            const String asset_name = _asset != nullptr ? _asset->Name() : "No Asset";
            _last_known_dirty = IsDirty();
            SetTitle(std::format("Graph Editor - {}{}", asset_name, _last_known_dirty ? "*" : ""));
        }

        void GraphEditorWindow::AddNodeFromPalette(const String &node_type)
        {
            if (_document == nullptr)
                return;
            Vector2f position = Vector2f::kZero;
            if (_canvas != nullptr)
            {
                const Vector4f rect = _canvas->GetArrangeRect();
                position = _canvas->ScreenToGraph({rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f});
            }
            _document->Commands().Execute(MakeScope<AddGraphNodeCommand>(node_type, position));
            RefreshWindowTitle();
            RefreshStatusBar();
        }

        void GraphEditorWindow::FocusValidationMessage(const GraphValidationMessage &message)
        {
            if (_canvas == nullptr || _document == nullptr)
                return;
            if (message._node_id != Guid::EmptyGuid() && _canvas->FocusNode(message._node_id, true))
                return;
            if (message._pin_id != Guid::EmptyGuid())
            {
                const GraphNodeData *node = _document->FindNodeByPin(message._pin_id);
                if (node != nullptr && _canvas->FocusNode(node->_id, true))
                    return;
            }
            if (message._link_id != Guid::EmptyGuid())
                _canvas->FocusLink(message._link_id, true);
        }

        String GraphEditorWindow::MakeSelectionSignature() const
        {
            if (_canvas == nullptr)
                return {};
            String signature;
            for (const Guid &node_id : _canvas->SelectedNodes())
                signature += node_id.ToString() + ";";
            signature += "|";
            for (const Guid &link_id : _canvas->SelectedLinks())
                signature += link_id.ToString() + ";";
            signature += "|";
            for (const Guid &comment_id : _canvas->SelectedComments())
                signature += comment_id.ToString() + ";";
            return signature;
        }

        String GraphEditorWindow::PinDirectionText(EGraphPinDirection direction)
        {
            return direction == EGraphPinDirection::kInput ? "In" : "Out";
        }
    } // namespace Editor
} // namespace Ailu
