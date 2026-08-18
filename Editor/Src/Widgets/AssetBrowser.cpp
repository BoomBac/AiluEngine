#include "Widgets/AssetBrowser.h"

#include "Assets/AssetTypeRegistry.h"
#include "Assets/PrefabAsset.h"
#include "Assets/ScriptAsset.h"
#include "Audio/Audio.h"
#include "Audio/AudioClip.h"
#include "Common/EditorPopup.h"
#include "Dock/DockManager.h"
#include "Editors/ProjectSettingsEditor.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "Objects/JsonArchive.h"
#include "Project/ProjectManager.h"
#include "Render/Mesh.h"
#include "Scene/Scene.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/DragDrop.h"
#include "UI/TextRenderer.h"
#include "UI/TreeView.h"
#include "Widgets/AssetEditorRegistry.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <tuple>

namespace Ailu
{
    namespace Editor
    {
        using namespace UI;
        namespace
        {
            constexpr f32 kIconLabelHeight = 22.0f;
            constexpr f32 kIconCellMinWidth = 88.0f;
            constexpr f32 kIconCellGap = 8.0f;
            constexpr f32 kIconCellPadding = 4.0f;
            constexpr f32 kListViewIconThreshold = 60.0f;
            constexpr f32 kListRowHeight = 24.0f;
            constexpr f32 kListIconSize = 18.0f;
            constexpr f32 kListTextLeftPadding = 6.0f;
            AudioHandle s_audio_preview_handle;

            bool IsListView(f32 icon_size)
            {
                return icon_size < kListViewIconThreshold;
            }

            String FitTextToWidth(const String &text, f32 max_width, f32 font_size)
            {
                if (text.empty() || max_width <= 0.0f)
                    return {};
                if (UI::TextRenderer::CalculateTextSize(text, font_size).x <= max_width)
                    return text;

                constexpr const char *kEllipsis = "...";
                const f32 ellipsis_width = UI::TextRenderer::CalculateTextSize(kEllipsis, font_size).x;
                if (ellipsis_width > max_width)
                    return {};

                u64 left = 0u;
                u64 right = text.size();
                while (left < right)
                {
                    const u64 mid = (left + right + 1u) / 2u;
                    const String candidate = text.substr(0u, mid) + kEllipsis;
                    if (UI::TextRenderer::CalculateTextSize(candidate, font_size).x <= max_width)
                        left = mid;
                    else
                        right = mid - 1u;
                }
                return text.substr(0u, left) + kEllipsis;
            }

            bool IsWorldOutlineEntityDrag(const DragPayload &payload)
            {
                if (payload._type != EDragType::kTreeItem || payload._data == nullptr)
                    return false;
                const auto *tree_payload = static_cast<const TreeViewDragPayload *>(payload._data);
                auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
                return tree_payload != nullptr && tree_payload->_source_tree != nullptr &&
                       tree_payload->_source_tree->Name() == "WorldOutlineTree" && scene != nullptr &&
                       scene->IsValidEntity(static_cast<ECS::Entity>(tree_payload->_item));
            }

            bool PlayAudioClipAsset(Asset *asset)
            {
                if (asset == nullptr || asset->_asset_type != StaticClass<AudioClip>())
                    return false;
                if (asset->_p_obj == nullptr)
                    ResourceMgr::Get().Load<AudioClip>(asset->_asset_path);
                if (asset->_p_obj == nullptr)
                    return false;

                if (s_audio_preview_handle.IsValid())
                    Audio::Stop(s_audio_preview_handle);

                AudioPlayOptions options;
                options._bus = EAudioBus::kUi;
                options._volume = 1.0f;
                s_audio_preview_handle = Audio::Play(asset->GetGuid(), options);
                return s_audio_preview_handle.IsValid();
            }

            void StopAudioPreview()
            {
                if (!s_audio_preview_handle.IsValid())
                    return;
                Audio::Stop(s_audio_preview_handle);
                s_audio_preview_handle = AudioHandle::Invalid();
            }
        }// namespace

        AssetBrowser::AssetBrowser() : DockWindow("Asset Browser")
        {
            _sv = _content_root->AddChild<UI::SplitView>();
            _sv->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _sv->SlotPadding() = UI::Padding(_content_root->Thickness());
            _sv->InvalidateLayout();
            auto left = _sv->AddChild<UI::VerticalBox>();
            left->SlotPadding() = UI::Padding(2.0f);
            left->InvalidateLayout();
            _directory_tree_data_source = new DirectoryTreeDataSource();
            _directory_tree = left->AddChild<UI::TreeView>();
            _directory_tree->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill).CrossAlignment(UI::EAlignment::kFill);
            _directory_tree->SetDataSource(_directory_tree_data_source);
            _directory_tree->_row_height = 20.0f;
            _directory_tree->_on_selection_changed += [this](UI::TreeItemId item)
            {
                if (item == UI::kInvalidTreeItemId || _directory_tree_data_source == nullptr)
                    return;
                NavigateToPath(_directory_tree_data_source->GetPath(item));
            };

            _right = _sv->AddChild<UI::VerticalBox>();
            _right->SlotPadding() = UI::Padding(2.0f, 2.0f, 0.0f, 0.0f);
            _right->InvalidateLayout();
            _on_size_change += [this](Vector2f new_size)
            {
                auto t = _content_root->Thickness();
                _sv->GetSlot()->Size({new_size.x, new_size.y - kTitleBarHeight});
            };
            auto roots = _content.GetRoots();
            _current_path = roots.empty() ? fs::path(ResourceMgr::Get().EngineResRootPath()) : roots.front()._sys_path;
            auto hb = _right->AddChild<UI::HorizontalBox>();
            hb->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).CrossAlignment(UI::EAlignment::kFill);
            auto back_btn = hb->AddChild<UI::Button>();
            back_btn->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed).Size({22.0f, 22.0f});
            back_btn->OnMouseClick() += [&](UI::UIEvent& e)
            {
                const fs::path parent_path = _current_path.parent_path();
                if (auto root = _content.FindRoot(_current_path); root.has_value() && _content.IsInsideRoot(parent_path))
                    NavigateToPath(parent_path);
            };
            back_btn->SetText("<");
            if (ProjectManager::Get().HasOpenedProject())
            {
                auto project_settings_btn = hb->AddChild<UI::Button>("Project Settings");
                project_settings_btn->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                    .Size({124.0f, 22.0f});
                project_settings_btn->OnMouseClick() += [](UI::UIEvent &event)
                {
                    auto editor = MakeRef<ProjectSettingsEditor>();
                    editor->Open();
                    DockManager::Get().AddDock(editor);
                    event._is_handled = true;
                };
            }
            _path_bar = hb->AddChild<UI::HorizontalBox>();
            _path_bar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({0.0f, 22.0f}).FillRate(1.0f);
            _path_title = _path_bar->AddChild<UI::Text>("Current Path");
            _path_title->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kFixed).Size({120.0f, 22.0f});
            _path_title->_horizontal_align = EAlignment::kLeft;
            auto search_label = hb->AddChild<UI::Text>("Search");
            search_label->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed).Size({48.0f, 22.0f});
            search_label->_horizontal_align = EAlignment::kCenter;
            _search_input = hb->AddChild<UI::InputBlock>();
            _search_input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed).Size({180.0f, 22.0f});
            _search_input->_on_content_changed += [this](String value)
            {
                String lowered = std::move(value);
                std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });
                _search_text = std::move(lowered);
                _content_dirty = true;
            };
            _search_input->SetContent("", false);
            _icon_area = _right->AddChild<UI::ScrollView>();
            _icon_area->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill).CrossAlignment(UI::EAlignment::kFill);
            auto slider = _right->AddChild<UI::Slider>();
            slider->_range = {50.0f, 200.0f};
            slider->SetValue(64.0f);
            _is_list_view = IsListView(_icon_size);
            _icon_content = _icon_area->AddChild<UI::Canvas>();
            _icon_content->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kAuto);
            const auto blank_context_handler = [this](UI::UIEvent &e)
            {
                if (e._key_code != EKey::kRBUTTON)
                    return;
                if (e._target != _icon_area && e._target != _icon_content)
                    return;
                ShowBlankAreaContextMenu(e._mouse_position);
                e._is_handled = true;
            };
            _icon_area->OnMouseDown() += blank_context_handler;
            _icon_content->OnMouseDown() += blank_context_handler;
            _icon_area->OnKeyDown() += [this](UI::UIEvent &e)
            {
                if (e._key_code != EKey::kF2 || _selected_item_root == nullptr || _selected_item_text == nullptr)
                    return;

                if (_selected_asset != nullptr)
                    BeginAssetRename(_selected_asset, _selected_item_root, _selected_item_text);
                else if (!_selected_folder_path.empty())
                    BeginFolderRename(_selected_folder_path, _selected_item_root, _selected_item_text);
                else
                    return;
                e._is_handled = true;
            };
            slider->_on_value_change += [&](f32 value)
            {
                const bool was_list_view = _is_list_view;
                _icon_size = value;
                _is_list_view = IsListView(_icon_size);
                if (_is_list_view != was_list_view)
                    _content_dirty = true;
                else
                    _layout_dirty = true;
            };
            DropHandler handler;
            handler._can_drop = [](const DragPayload &payload) -> bool
            {
                return IsWorldOutlineEntityDrag(payload);
            };
            handler._on_drop = [this](const DragPayload &payload, f32 x, f32 y)
            {
                if (payload._type != EDragType::kTreeItem || payload._data == nullptr)
                    return;
                const auto *tree_payload = static_cast<const TreeViewDragPayload *>(payload._data);
                if (tree_payload != nullptr && CreatePrefabAsset(static_cast<ECS::Entity>(tree_payload->_item), _current_path))
                    _content_dirty = true;
            };
            _icon_area->SetDropHandler(handler);
            const auto file_drop_handler = [this](UI::UIEvent &e) { HandleFileDrop(e); };
            _content_root->OnFileDrop() += file_drop_handler;
            _right->OnFileDrop() += file_drop_handler;
            _icon_area->OnFileDrop() += file_drop_handler;
            _icon_content->OnFileDrop() += file_drop_handler;

            _import_controller.SetOnImported([this]() { _content_dirty = true; });
        }

        AssetBrowser::~AssetBrowser()
        {
            delete _directory_tree_data_source;
            _directory_tree_data_source = nullptr;
        }

        void AssetBrowser::HandleShortcuts()
        {
            if (!IsFocus() || !Input::IsKeyJustPressed(EKey::kF2))
                return;

            if (_selected_asset != nullptr)
                BeginAssetRename(_selected_asset, _selected_item_root, _selected_item_text);
            else if (!_selected_folder_path.empty())
                BeginFolderRename(_selected_folder_path, _selected_item_root, _selected_item_text);
        }

        void AssetBrowser::NavigateToPath(const fs::path &path)
        {
            if (path.empty() || !fs::exists(path) || !fs::is_directory(path))
                return;

            fs::path target_path = path;
            if (!_content.FindRoot(target_path).has_value())
            {
                auto roots = _content.GetRoots();
                target_path = roots.empty() ? fs::path(ResourceMgr::Get().EngineResRootPath()) : roots.front()._sys_path;
            }

            _current_path = target_path;
            LOG_INFO("AssetBrowser: navigate to {}", _current_path.string());
            _content_dirty = true;
            _layout_dirty = true;
        }

        void AssetBrowser::RefreshDirectoryTree()
        {
            if (_directory_tree == nullptr || _directory_tree_data_source == nullptr)
                return;

            _directory_tree_data_source->Rebuild();
            _directory_tree->Refresh();
            const UI::TreeItemId current_item = _directory_tree_data_source->FindItemByPath(_current_path);
            if (current_item != UI::kInvalidTreeItemId)
            {
                _directory_tree->ExpandParents(current_item);
                _directory_tree->SetSelectedItem(current_item, false);
            }
            _directory_tree_dirty = false;
        }

        void AssetBrowser::RefreshContent()
        {
            if (!fs::exists(_current_path))
            {
                _content_dirty = false;
                return;
            }

            const auto entries = _content.Query(_current_path, _search_text);

            _icon_content->ClearChildren();
            ClearSelection();

            for (const auto &entry: entries)
            {
                if (entry._type == AssetBrowserEntry::EType::kFolder)
                    CreateFolderWidget(entry);
                else
                    CreateAssetWidget(entry);
            }

            UpdatePathButtons();

            _content_dirty = false;
            _layout_dirty = true;
        }

        std::tuple<Ref<UI::UIElement>, UI::Image *, UI::Text *> AssetBrowser::CreateEntryWidgetRoot(const String &display_name)
        {
            Ref<UI::UIElement> root;
            UI::Image *icon = nullptr;
            UI::Text *text = nullptr;
            if (_is_list_view)
            {
                auto hb = MakeRef<UI::HorizontalBox>();
                hb->SlotPadding() = UI::Padding(2.0f);
                hb->InvalidateLayout();
                icon = hb->AddChild<UI::Image>();
                icon->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                        .Size({kListIconSize, kListIconSize}).Margin({2.0f, 1.0f, 4.0f, 1.0f});
                text = hb->AddChild<UI::Text>();
                text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                        .Size({0.0f, kListRowHeight}).FillRate(1.0f).CrossAlignment(UI::EAlignment::kFill);
                text->_horizontal_align = UI::EAlignment::kLeft;
                root = hb;
            }
            else
            {
                auto vb = MakeRef<UI::VerticalBox>();
                vb->SlotPadding() = UI::Padding(kIconCellPadding);
                vb->InvalidateLayout();
                icon = vb->AddChild<UI::Image>();
                icon->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                        .Size({_icon_size, _icon_size}).CrossAlignment(UI::EAlignment::kCenter);
                text = vb->AddChild<UI::Text>();
                text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                        .Size({kIconCellMinWidth, kIconLabelHeight}).CrossAlignment(UI::EAlignment::kCenter);
                text->_horizontal_align = UI::EAlignment::kCenter;
                root = vb;
            }
            root->Name(display_name);
            text->Name(display_name);
            text->SetText(display_name);
            text->_vertical_align = UI::EAlignment::kCenter;
            text->SlotPadding() = UI::Padding(kListTextLeftPadding, 0.0f, kListTextLeftPadding, 0.0f);
            text->InvalidateLayout();
            text->FontSize(14.0f);
            return std::make_tuple(root, icon, text);
        }

        void AssetBrowser::CreateFolderWidget(const AssetBrowserEntry &entry)
        {
            static auto s_folder_icon = ResourceMgr::Get().Get<Texture2D>(EnginePath::kEngineIconPathW + L"folder.alasset");
            auto [vb, icon, text] = CreateEntryWidgetRoot(entry._display_name);
            icon->Name(entry._display_name);
            icon->SetTexture(s_folder_icon);
            const fs::path item_path = entry._sys_path;
            const WString folder_sys_path = item_path.wstring();
            vb->OnMouseDown() += [this, folder_sys_path, item_path, item_root = vb.get(), item_text = text](UI::UIEvent &e)
            {
                SelectFolder(item_path, item_root, item_text);
                if (e._key_code != EKey::kRBUTTON)
                    return;
                ShowFolderContextMenu(folder_sys_path, e._mouse_position, item_root, item_text);
                e._is_handled = true;
            };
            icon->OnMouseEnter() += [this, icon](UI::UIEvent &e)
            {
                _hover_item = e._current_target;
                e._current_target->As<Image>()->_tint_color = Colors::kYellow;
            };
            icon->OnMouseExit() += [this](UI::UIEvent &e)
            {
                if (_hover_item == e._current_target)
                {
                    _hover_item = nullptr;
                    e._current_target->As<Image>()->_tint_color = Colors::kWhite;
                }
            };
            icon->OnMouseDoubleClick() += [this, item_path](UI::UIEvent &e)
            {
                NavigateToPath(item_path);
            };
            DropHandler folder_drop_handler;
            folder_drop_handler._can_drop = [](const DragPayload &payload)
            {
                return IsWorldOutlineEntityDrag(payload);
            };
            folder_drop_handler._on_drop = [this, item_path](const DragPayload &payload, f32 x, f32 y)
            {
                if (payload._type != EDragType::kTreeItem || payload._data == nullptr)
                    return;
                const auto *tree_payload = static_cast<const TreeViewDragPayload *>(payload._data);
                if (tree_payload != nullptr && CreatePrefabAsset(static_cast<ECS::Entity>(tree_payload->_item), item_path))
                    _content_dirty = true;
            };
            vb->SetDropHandler(folder_drop_handler);
            _icon_content->AddChild(vb);
        }

        void AssetBrowser::CreateAssetWidget(const AssetBrowserEntry &entry)
        {
            Asset *asset = entry._asset;
            if (asset == nullptr)
                return;
            const String display_name = entry._display_name;
            auto [vb, icon, text] = CreateEntryWidgetRoot(display_name);
            Color tint = asset->_p_obj ? Colors::kWhite : Colors::kGray;
            icon->Name(asset->Name());
            icon->_tint_color = tint;
            vb->OnMouseDown() += [this, asset, item_root = vb.get(), item_text = text](UI::UIEvent &e)
            {
                SelectAsset(asset, item_root, item_text);
                if (e._key_code != EKey::kRBUTTON)
                    return;
                ShowAssetContextMenu(asset, e._mouse_position, item_root, item_text);
                e._is_handled = true;
            };
            icon->SetTexture(AssetTypeRegistry::Get().GetIcon(asset));
            icon->OnMouseEnter() += [this, icon](UI::UIEvent &e)
            {
                _hover_item = e._current_target;
                e._current_target->As<Image>()->_tint_color = Colors::kYellow;
            };
            icon->OnMouseExit() += [this, tint](UI::UIEvent &e)
            {
                if (_hover_item == e._current_target)
                {
                    _hover_item = nullptr;
                    e._current_target->As<Image>()->_tint_color = tint;
                }
            };
            icon->OnMouseDoubleClick() += [this, asset](UI::UIEvent &e)
            {
                OpenAsset(asset);
            };
            _icon_content->AddChild(vb);
            if (asset->_asset_type == StaticClass<Render::Mesh>() || asset->_asset_type == ScriptAsset::StaticType() ||
                asset->_asset_type == PrefabAssetDocument::StaticType())
            {
                icon->OnMouseDown() += [this, icon, asset, display_name](UI::UIEvent &e)
                {
                    _is_dragging = false;
                    _drag_start_pos = e._mouse_position;
                };
                icon->OnMouseMove() += [this, icon, asset, display_name](UI::UIEvent &e)
                {
                    if (Input::IsKeyDown(EKey::kLBUTTON))
                    {
                        if (!_is_dragging)
                        {
                            f32 dist = Magnitude(e._mouse_position - _drag_start_pos);
                            if (dist > kDragThreshold)
                            {
                                _is_dragging = true;
                                EDragType drag_type = EDragType::kMesh;
                                if (asset->_asset_type == ScriptAsset::StaticType())
                                    drag_type = EDragType::kScript;
                                else if (asset->_asset_type == PrefabAssetDocument::StaticType())
                                    drag_type = EDragType::kPrefab;
                                auto payload = DragPayload{drag_type, asset};
                                DragDropManager::Get().BeginDrag(payload, display_name);
                            }
                        }
                    }
                };
            }
        }

        void AssetBrowser::SelectFolder(const fs::path &path, UI::UIElement *root, UI::Text *text)
        {
            _selected_item_root = root;
            _selected_item_text = text;
            _selected_asset = nullptr;
            _selected_folder_path = path.wstring();
        }

        void AssetBrowser::SelectAsset(Asset *asset, UI::UIElement *root, UI::Text *text)
        {
            _selected_item_root = root;
            _selected_item_text = text;
            _selected_asset = asset;
            _selected_folder_path.clear();
        }

        void AssetBrowser::ClearSelection()
        {
            _selected_item_root = nullptr;
            _selected_item_text = nullptr;
            _selected_asset = nullptr;
            _selected_folder_path.clear();
        }

        void AssetBrowser::RefreshContentLayout()
        {
            const Vector2f parent_size = _icon_area->GetContentRect().zw;
            const f32 cell_width = _is_list_view ? parent_size.x : std::max(kIconCellMinWidth, _icon_size + kIconCellPadding * 2.0f + kIconCellGap);
            const f32 icon_draw_size = _is_list_view ? kListIconSize : std::max(1.0f, _icon_size);
            const f32 cell_height = _is_list_view ? kListRowHeight : icon_draw_size + kIconLabelHeight + kIconCellPadding * 2.0f;
            const f32 label_width = _is_list_view ? std::max(0.0f, parent_size.x - kListIconSize - kListTextLeftPadding * 2.0f - 12.0f) : std::max(0.0f, cell_width - kIconCellPadding * 2.0f);
            f32 x = 0.0f;
            f32 y = 0.0f;
            u32 num_per_row = _is_list_view ? 1u : (u32) (parent_size.x / cell_width);
            if (num_per_row == 0u)
                num_per_row = 1u;
            for (u32 i = 0; i < (u32) _icon_content->GetChildren().size(); i++)
            {
                auto child = _icon_content->ChildAt(i);
                child->GetSlotAs<UI::CanvasSlot>().Position({x, y}).Size({cell_width, cell_height});
                if (_is_list_view)
                {
                    if (auto row = child->As<UI::HorizontalBox>())
                    {
                        if (auto icon = row->ChildAt(0u); icon != nullptr)
                            icon->GetSlotAs<UI::LinearSlot>().Size({icon_draw_size, icon_draw_size});
                        if (auto text = row->ChildAt(1u)->As<UI::Text>(); text != nullptr)
                        {
                            text->GetSlotAs<UI::LinearSlot>().Size({label_width, kListRowHeight});
                            text->SetText(FitTextToWidth(text->Name(), label_width, text->FontSize()));
                        }
                    }
                }
                else if (auto tile = child->As<UI::VerticalBox>())
                {
                    if (auto icon = tile->ChildAt(0u); icon != nullptr)
                        icon->GetSlotAs<UI::LinearSlot>().Size({icon_draw_size, icon_draw_size});
                    if (auto text = tile->ChildAt(1u)->As<UI::Text>(); text != nullptr)
                    {
                        text->GetSlotAs<UI::LinearSlot>().Size({label_width, kIconLabelHeight});
                        text->SetText(FitTextToWidth(text->Name(), label_width, text->FontSize()));
                    }
                }
                if ((i + 1) % num_per_row == 0)
                {
                    x = 0.0f;
                    y += cell_height;
                }
                else
                {
                    x += cell_width;
                }
            }
            _layout_dirty = false;
        }

        void AssetBrowser::UpdatePathButtons()
        {
            if (_path_bar == nullptr)
                return;

            _path_bar->ClearChildren();
            const auto root = _content.FindRoot(_current_path);
            if (!root.has_value())
            {
                _path_title = _path_bar->AddChild<UI::Text>(_current_path.string());
                _path_title->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({0.0f, 22.0f});
                _path_title->_horizontal_align = EAlignment::kLeft;
                return;
            }

            Vector<std::pair<String, fs::path>> crumbs;
            crumbs.push_back({root->_label, root->_sys_path});
            const WString relative_directory = StripLogicalAssetPathScheme(_content.GetAssetDirectory(_current_path));
            if (!relative_directory.empty())
            {
                fs::path walk_path = root->_sys_path;
                for (const auto &part: fs::path(relative_directory))
                {
                    walk_path /= part;
                    crumbs.push_back({part.string(), walk_path});
                }
            }

            for (size_t i = 0u; i < crumbs.size(); ++i)
            {
                auto *button = _path_bar->AddChild<UI::Button>();
                const f32 button_width = std::max(56.0f, static_cast<f32>(crumbs[i].first.size()) * 8.0f + 20.0f);
                button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed).Size({button_width, 22.0f});
                button->SetText(crumbs[i].first);
                button->OnMouseClick() += [this, target_path = crumbs[i].second](UI::UIEvent &e)
                {
                    NavigateToPath(target_path);
                    e._is_handled = true;
                };

                if (i + 1u < crumbs.size())
                {
                    auto *separator = _path_bar->AddChild<UI::Text>(">");
                    separator->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed).Size({14.0f, 22.0f});
                    separator->_horizontal_align = EAlignment::kCenter;
                }
            }
        }

        void AssetBrowser::HandleFileDrop(UI::UIEvent &e)
        {
            if (e._drop_files.empty())
                return;

            _import_controller.QueueFiles(e._drop_files, _current_path, e._mouse_position);
            e._is_handled = true;
        }

        void AssetBrowser::Update(f32 dt)
        {
            DockWindow::Update(dt);

            HandleShortcuts();
            AssetTypeRegistry::Get().BeginFrame();

            if (_directory_tree_dirty)
                RefreshDirectoryTree();

            const Vector2f content_size = _icon_area->GetContentRect().zw;
            if (content_size.x <= 0.0f || content_size.y <= 0.0f)
                return;

            if (!NearbyEqual(content_size, _last_icon_area_size))
            {
                _last_icon_area_size = content_size;
                _layout_dirty = true;
            }

            if (_content_dirty)
                RefreshContent();

            if (_layout_dirty)
                RefreshContentLayout();
        }

        void AssetBrowser::OpenAsset(Asset *asset)
        {
            if (asset == nullptr)
                return;
            if (AssetEditorRegistry::Get().Open(asset))
                _content_dirty = true;
        }

        void AssetBrowser::BuildCreateAssetActions(Vector<PopupMenuAction> &actions, const fs::path &target_directory, Vector2f popup_pos)
        {
            actions.push_back({"New Folder", [this, popup_pos, target_directory]()
            {
                EditorPopup::ShowTextInputAt(popup_pos, "Create Folder",
                                             _operations.MakeUniqueEntryName(target_directory, "NewFolder", L"", true),
                                             [this, target_directory](const String &input) -> std::optional<String>
                                             {
                                                 const String name = TrimNameCopy(input);
                                                 if (auto error = ValidateEntryName(name); error.has_value())
                                                     return error;
                                                 if (!_operations.CreateFolder(target_directory, name))
                                                     return String("Folder already exists.");
                                                 _content_dirty = true;
                                                 _directory_tree_dirty = true;
                                                 return std::nullopt;
                                             });
            }});

            for (const auto &creator: AssetTypeRegistry::Get().Creators())
            {
                if (creator._create_dialog)
                {
                    actions.push_back({creator._menu_name, [this, target_directory, popup_pos, creator]()
                    {
                        creator._create_dialog(target_directory, popup_pos);
                    }});
                }
                else if (creator._create)
                {
                    actions.push_back({creator._menu_name, [this, target_directory, popup_pos, creator]()
                    {
                        EditorPopup::ShowTextInputAt(popup_pos, creator._dialog_title,
                                                     _operations.MakeUniqueEntryName(target_directory, creator._default_name, creator._extension, false),
                                                     [this, target_directory, creator](const String &input) -> std::optional<String>
                                                     {
                                                         const String name = TrimNameCopy(input);
                                                         if (auto error = ValidateEntryName(name); error.has_value())
                                                             return error;
                                                         if (!creator._create(target_directory, name))
                                                             return creator._exists_message;
                                                         _content_dirty = true;
                                                         return std::nullopt;
                                                     });
                    }});
                }
            }
        }

        void AssetBrowser::ShowBlankAreaContextMenu(Vector2f popup_pos)
        {
            Vector<PopupMenuAction> actions;
            BuildCreateAssetActions(actions, _current_path, popup_pos);
            actions.push_back({"Refresh", [this]() { _content_dirty = true; }});
            EditorPopup::ShowActionMenuAt(popup_pos, actions);
        }

        void AssetBrowser::ShowFolderContextMenu(const WString &folder_sys_path, Vector2f popup_pos, UI::UIElement *item_root,
                                                 UI::Text *item_text)
        {
            const fs::path folder_path(folder_sys_path);
            const String folder_name = folder_path.filename().string();
            Vector<PopupMenuAction> actions;
            actions.push_back({"Open", [this, folder_sys_path]()
            {
                NavigateToPath(folder_sys_path);
            }});
            actions.push_back({"Rename", [this, folder_sys_path, item_root, item_text]()
            {
                BeginFolderRename(folder_sys_path, item_root, item_text);
            }});
            actions.push_back({"Delete", [this, folder_name, folder_path, popup_pos]()
            {
                EditorPopup::ShowConfirmAt(popup_pos, std::format("Delete folder \"{}\"?", folder_name),
                                           [this, folder_path]()
                                           {
                                               if (_operations.DeleteFolder(folder_path))
                                               {
                                                   _content_dirty = true;
                                                   _directory_tree_dirty = true;
                                               }
                                           });
            }, true});
            BuildCreateAssetActions(actions, folder_path, popup_pos);
            EditorPopup::ShowActionMenuAt(popup_pos, actions);
        }

        void AssetBrowser::ShowAssetContextMenu(Asset *asset, Vector2f popup_pos, UI::UIElement *item_root, UI::Text *item_text)
        {
            if (asset == nullptr)
                return;

            const String asset_name = asset->Name();
            Vector<PopupMenuAction> actions;
            if (AssetEditorRegistry::Get().CanOpen(asset->_asset_type))
            {
                actions.push_back({"Open", [this, asset]() { OpenAsset(asset); }});
            }
            if (asset->_asset_type == StaticClass<AudioClip>())
            {
                actions.push_back({"Play Audio", [asset]() { PlayAudioClipAsset(asset); }});
                actions.push_back({"Stop Audio", []() { StopAudioPreview(); }});
            }
            actions.push_back({"Rename", [this, asset, item_root, item_text]()
            {
                BeginAssetRename(asset, item_root, item_text);
            }});
            actions.push_back({"Delete", [this, asset_name, asset, popup_pos]()
            {
                EditorPopup::ShowConfirmAt(popup_pos, std::format("Delete asset \"{}\"?", asset_name),
                                           [this, asset]()
                                           {
                                               if (_operations.DeleteAsset(asset))
                                                   _content_dirty = true;
                                           });
            }, true});
            EditorPopup::ShowActionMenuAt(popup_pos, actions);
        }

        void AssetBrowser::BeginFolderRename(const WString &folder_sys_path, UI::UIElement *item_root, UI::Text *item_text)
        {
            if (item_root == nullptr || item_text == nullptr)
                return;

            const String folder_name = fs::path(folder_sys_path).filename().string();
            EditorPopup::BeginInlineTextInput(item_root, item_text, folder_name,
                                              [this, folder_sys_path](const String &input) -> std::optional<String>
                                              {
                                                  const String name = TrimNameCopy(input);
                                                  if (auto error = ValidateEntryName(name); error.has_value())
                                                      return error;
                                                  if (!_operations.RenameFolder(folder_sys_path, name))
                                                      return String("Folder rename failed.");
                                                  _content_dirty = true;
                                                  _directory_tree_dirty = true;
                                                  return std::nullopt;
                                              });
        }

        void AssetBrowser::BeginAssetRename(Asset *asset, UI::UIElement *item_root, UI::Text *item_text)
        {
            if (asset == nullptr || item_root == nullptr || item_text == nullptr)
                return;

            const String asset_name = asset->Name();
            EditorPopup::BeginInlineTextInput(item_root, item_text, asset_name,
                                              [this, asset](const String &input) -> std::optional<String>
                                              {
                                                  const String name = TrimNameCopy(input);
                                                  if (auto error = ValidateEntryName(name); error.has_value())
                                                      return error;
                                                  if (!_operations.RenameAsset(asset, name))
                                                      return String("Asset rename failed.");
                                                  _content_dirty = true;
                                                  return std::nullopt;
                                              });
        }

        void AssetBrowser::SaveDockLayoutState(JsonArchive &ar)
        {
            if (_sv)
                _split_ratio = _sv->GetRatio();
            ar.BeginObject("_split_ratio");
            ar << _split_ratio;
            ar.EndObject();
        }

        void AssetBrowser::LoadDockLayoutState(JsonArchive &ar)
        {
            if (ar.HasField("_split_ratio"))
            {
                ar.BeginObject("_split_ratio");
                ar >> _split_ratio;
                ar.EndObject();
            }
        }

        void AssetBrowser::OnDockLayoutLoaded()
        {
            if (_sv)
                _sv->SetRatio(_split_ratio);
        }
    }// namespace Editor
}// namespace Ailu