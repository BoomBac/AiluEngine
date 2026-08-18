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

            bool IsKeyDownNow(EKey key)
            {
                return Input::IsKeyDown(key) || Input::IsKeyDownAccurate(key);
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
            _directory_tree->SetExternalCanDropCallback([this](const DragPayload &payload, UI::TreeItemId item)
            {
                return item != UI::kInvalidTreeItemId && _directory_tree_data_source != nullptr &&
                       CanAcceptAssetDrag(payload) && fs::is_directory(_directory_tree_data_source->GetPath(item));
            });
            _directory_tree->SetExternalDropCallback([this](const DragPayload &payload, UI::TreeItemId item, Vector2f pos)
            {
                if (item == UI::kInvalidTreeItemId || _directory_tree_data_source == nullptr)
                    return;
                ShowAssetTransferDialog(payload, _directory_tree_data_source->GetPath(item), pos);
            });

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
                if (e._target != _icon_area && e._target != _icon_content)
                    return;
                if (e._key_code == EKey::kLBUTTON)
                {
                    ClearSelection();
                    e._is_handled = true;
                    return;
                }
                if (e._key_code != EKey::kRBUTTON)
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
            handler._can_drop = [this](const DragPayload &payload) -> bool
            {
                return IsWorldOutlineEntityDrag(payload) || CanAcceptAssetDrag(payload);
            };
            handler._on_drop = [this](const DragPayload &payload, f32 x, f32 y)
            {
                if (CanAcceptAssetDrag(payload))
                {
                    ShowAssetTransferDialog(payload, _current_path, {x, y});
                    return;
                }
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
            if (!IsFocus() || (_search_input != nullptr && _search_input->IsEditing()))
                return;

            const bool control_down = IsKeyDownNow(EKey::kCONTROL) || IsKeyDownNow(EKey::kLCONTROL) ||
                                      IsKeyDownNow(EKey::kRCONTROL);
            if (control_down && Input::IsKeyJustPressed(EKey::kC))
            {
                CopySelectionToClipboard();
                return;
            }
            if (control_down && Input::IsKeyJustPressed(EKey::kV))
            {
                PasteClipboard();
                return;
            }
            if (!Input::IsKeyJustPressed(EKey::kF2))
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

            _visible_entries = entries;
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
            auto border = MakeRef<UI::Border>();
            border->Thickness(0.0f);
            border->GetStyleOverride().SetBorderWidth(0.0f);
            border->GetStyleOverride().SetBorderColor(Colors::kTransparent);
            UI::UIBrush transparent_brush;
            transparent_brush._type = UI::EUIBrushType::kColor;
            transparent_brush._tint = Colors::kTransparent;
            border->GetStyleOverride().SetBackground(transparent_brush);
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
                border->AddChild(hb);
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
                border->AddChild(vb);
            }
            root = border;
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
            const u32 entry_index = static_cast<u32>(&entry - _visible_entries.data());
            vb->OnMouseDown() += [this, folder_sys_path, item_path, entry_index, item_root = vb.get(), item_text = text](UI::UIEvent &e)
            {
                SelectFolder(item_path, entry_index, item_root, item_text, e._key_code != EKey::kRBUTTON);
                if (e._key_code != EKey::kRBUTTON)
                    return;
                ShowFolderContextMenu(folder_sys_path, e._mouse_position, item_root, item_text);
                e._is_handled = true;
            };
            vb->OnMouseEnter() += [this, item_root = vb.get()](UI::UIEvent &e)
            {
                _hover_item = item_root;
                UpdateEntryVisual(item_root, true);
            };
            vb->OnMouseExit() += [this, item_root = vb.get()](UI::UIEvent &e)
            {
                if (_hover_item == item_root)
                {
                    _hover_item = nullptr;
                    UpdateEntryVisual(item_root, false);
                }
            };
            icon->OnMouseDoubleClick() += [this, item_path](UI::UIEvent &e)
            {
                NavigateToPath(item_path);
            };
            DropHandler folder_drop_handler;
            folder_drop_handler._can_drop = [this](const DragPayload &payload)
            {
                return IsWorldOutlineEntityDrag(payload) || CanAcceptAssetDrag(payload);
            };
            folder_drop_handler._on_drop = [this, item_path](const DragPayload &payload, f32 x, f32 y)
            {
                if (CanAcceptAssetDrag(payload))
                {
                    ShowAssetTransferDialog(payload, item_path, {x, y});
                    return;
                }
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
            const u32 entry_index = static_cast<u32>(&entry - _visible_entries.data());
            Color tint = asset->_p_obj ? Colors::kWhite : Colors::kGray;
            icon->Name(asset->Name());
            icon->_tint_color = tint;
            vb->OnMouseDown() += [this, asset, entry_index, item_root = vb.get(), item_text = text](UI::UIEvent &e)
            {
                SelectAsset(asset, entry_index, item_root, item_text, e._key_code != EKey::kRBUTTON);
                if (e._key_code != EKey::kRBUTTON)
                    return;
                ShowAssetContextMenu(asset, e._mouse_position, item_root, item_text);
                e._is_handled = true;
            };
            icon->SetTexture(AssetTypeRegistry::Get().GetIcon(asset));
            vb->OnMouseEnter() += [this, item_root = vb.get()](UI::UIEvent &e)
            {
                _hover_item = item_root;
                UpdateEntryVisual(item_root, true);
            };
            vb->OnMouseExit() += [this, item_root = vb.get()](UI::UIEvent &e)
            {
                if (_hover_item == item_root)
                {
                    _hover_item = nullptr;
                    UpdateEntryVisual(item_root, false);
                }
            };
            icon->OnMouseDoubleClick() += [this, asset](UI::UIEvent &e)
            {
                OpenAsset(asset);
            };
            _icon_content->AddChild(vb);
            {
                icon->OnMouseDown() += [this, asset](UI::UIEvent &e)
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
                                BeginAssetDrag(asset, display_name);
                            }
                        }
                    }
                };
            }
        }

        void AssetBrowser::SelectFolder(const fs::path &path, u32 index, UI::UIElement *root, UI::Text *text, bool preserve_modifiers)
        {
            SelectEntry(path, nullptr, index, root, text, preserve_modifiers);
        }

        void AssetBrowser::SelectAsset(Asset *asset, u32 index, UI::UIElement *root, UI::Text *text, bool preserve_modifiers)
        {
            if (asset != nullptr)
                SelectEntry(fs::path(ResourceMgr::GetResSysPath(asset->_asset_path)), asset, index, root, text, preserve_modifiers);
        }

        bool AssetBrowser::IsSelected(const fs::path &path) const
        {
            return std::any_of(_selected_entries.begin(), _selected_entries.end(), [&path](const SelectedEntry &entry)
            {
                return entry._path == path;
            });
        }

        void AssetBrowser::UpdateEntryVisual(UI::UIElement *root, bool is_hovered)
        {
            auto *border = root != nullptr ? root->As<UI::Border>() : nullptr;
            if (border == nullptr)
                return;

            const bool is_selected = std::any_of(_selected_entries.begin(), _selected_entries.end(), [root](const SelectedEntry &entry)
            {
                return entry._root == root;
            });
            Color background = Colors::kTransparent;
            if (is_selected)
                background = Color(0.18f, 0.34f, 0.62f, 0.9f);
            else if (is_hovered)
                background = Color(0.20f, 0.25f, 0.38f, 0.75f);

            UI::UIBrush brush;
            brush._type = UI::EUIBrushType::kColor;
            brush._tint = background;
            border->GetStyleOverride().SetBackground(brush);
            border->InvalidateStyle(UI::EStyleInvalidation::kPaintOnly);
        }

        void AssetBrowser::UpdateSelectionVisuals()
        {
            if (_icon_content == nullptr)
                return;
            for (u32 i = 0u; i < static_cast<u32>(_icon_content->GetChildren().size()); ++i)
            {
                UI::UIElement *root = _icon_content->ChildAt(i);
                UpdateEntryVisual(root, root == _hover_item);
            }
        }

        void AssetBrowser::SyncPrimarySelection()
        {
            _selected_item_root = nullptr;
            _selected_item_text = nullptr;
            _selected_asset = nullptr;
            _selected_folder_path.clear();
            if (_selected_entries.empty())
                return;

            const SelectedEntry *primary = &_selected_entries.back();
            if (_selection_anchor >= 0 && _selection_anchor < static_cast<i32>(_visible_entries.size()))
            {
                const fs::path anchor_path = _visible_entries[_selection_anchor]._sys_path;
                for (const auto &entry: _selected_entries)
                {
                    if (entry._path == anchor_path || (entry._asset != nullptr &&
                        fs::path(ResourceMgr::GetResSysPath(entry._asset->_asset_path)) == anchor_path))
                    {
                        primary = &entry;
                        break;
                    }
                }
            }
            _selected_item_root = primary->_root;
            _selected_item_text = primary->_text;
            _selected_asset = primary->_asset;
            if (_selected_asset == nullptr)
                _selected_folder_path = primary->_path.wstring();
        }

        void AssetBrowser::SelectEntry(const fs::path &path, Asset *asset, u32 index, UI::UIElement *root, UI::Text *text,
                                       bool preserve_modifiers)
        {
            const bool control_down = IsKeyDownNow(EKey::kCONTROL) || IsKeyDownNow(EKey::kLCONTROL) ||
                                      IsKeyDownNow(EKey::kRCONTROL);
            const bool shift_down = IsKeyDownNow(EKey::kSHIFT) || IsKeyDownNow(EKey::kLSHIFT) ||
                                    IsKeyDownNow(EKey::kRSHIFT);
            const bool use_control = preserve_modifiers && control_down;
            const bool use_shift = preserve_modifiers && shift_down;
            auto make_entry = [this](u32 entry_index)
            {
                SelectedEntry selected;
                selected._path = _visible_entries[entry_index]._sys_path;
                selected._asset = _visible_entries[entry_index]._asset;
                selected._root = _icon_content->ChildAt(entry_index);
                UI::UIElement *content = selected._root != nullptr ? selected._root->ChildAt(0u) : nullptr;
                selected._text = content != nullptr && content->ChildAt(1u) != nullptr ? content->ChildAt(1u)->As<UI::Text>() : nullptr;
                return selected;
            };

            if (use_shift && _selection_anchor >= 0 && _selection_anchor < static_cast<i32>(_visible_entries.size()))
            {
                if (!use_control)
                    _selected_entries.clear();
                const u32 begin = std::min(static_cast<u32>(_selection_anchor), index);
                const u32 end = std::max(static_cast<u32>(_selection_anchor), index);
                for (u32 i = begin; i <= end; ++i)
                {
                    if (!IsSelected(_visible_entries[i]._sys_path))
                        _selected_entries.push_back(make_entry(i));
                }
            }
            else if (use_control)
            {
                auto it = std::find_if(_selected_entries.begin(), _selected_entries.end(), [&path](const SelectedEntry &entry)
                {
                    return entry._path == path;
                });
                if (it != _selected_entries.end())
                    _selected_entries.erase(it);
                else
                    _selected_entries.push_back({path, asset, root, text});
                _selection_anchor = static_cast<i32>(index);
            }
            else
            {
                _selected_entries.clear();
                _selected_entries.push_back({path, asset, root, text});
                _selection_anchor = static_cast<i32>(index);
            }

            SyncPrimarySelection();
            UpdateSelectionVisuals();
        }

        void AssetBrowser::ClearSelection()
        {
            _selected_entries.clear();
            _selection_anchor = -1;
            _selected_item_root = nullptr;
            _selected_item_text = nullptr;
            _selected_asset = nullptr;
            _selected_folder_path.clear();
        }

        Vector<Asset *> AssetBrowser::GetSelectedAssets() const
        {
            Vector<Asset *> assets;
            for (const auto &entry: _selected_entries)
            {
                if (entry._asset != nullptr && std::find(assets.begin(), assets.end(), entry._asset) == assets.end())
                    assets.push_back(entry._asset);
            }
            return assets;
        }

        Vector<Asset *> AssetBrowser::GetDraggedAssets(const UI::DragPayload &payload) const
        {
            if (payload._data == nullptr)
                return {};
            if (payload._type == UI::EDragType::kFile)
            {
                if (payload._data != &_asset_drag_data)
                    return {};
                const auto *drag_data = static_cast<const AssetDragData *>(payload._data);
                return drag_data != nullptr ? drag_data->_assets : Vector<Asset *>();
            }
            if (payload._type == UI::EDragType::kMesh || payload._type == UI::EDragType::kScript ||
                payload._type == UI::EDragType::kPrefab)
                return {static_cast<Asset *>(payload._data)};
            return {};
        }

        bool AssetBrowser::CanAcceptAssetDrag(const UI::DragPayload &payload) const
        {
            const Vector<Asset *> assets = GetDraggedAssets(payload);
            return !assets.empty() && std::all_of(assets.begin(), assets.end(), [](Asset *asset) { return asset != nullptr; });
        }

        void AssetBrowser::ShowAssetTransferDialog(const UI::DragPayload &payload, const fs::path &target_directory, Vector2f popup_pos)
        {
            const Vector<Asset *> assets = GetDraggedAssets(payload);
            if (assets.empty() || !fs::is_directory(target_directory))
                return;

            const String operation_text = assets.size() == 1u ? assets.front()->Name() : std::format("{} assets", assets.size());
            EditorPopup::ShowDialogAt(popup_pos, "AssetTransferPrompt", "Move or Copy Asset", {300.0f, 128.0f},
                                      [operation_text](UI::VerticalBox *content, UI::Text *)
                                      {
                                          auto *message = content->AddChild<UI::Text>(std::format("Transfer {} to this folder?", operation_text));
                                          message->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
                                          message->_horizontal_align = UI::EAlignment::kCenter;
                                          message->_vertical_align = UI::EAlignment::kCenter;
                                      },
                                      {
                                          {"Move", [this, assets, target_directory]() -> std::optional<String>
                                          {
                                              if (!_operations.MoveAssets(assets, target_directory))
                                                  return String("Move failed.");
                                              _content_dirty = true;
                                              _directory_tree_dirty = true;
                                              return std::nullopt;
                                          }},
                                          {"Copy", [this, assets, target_directory]() -> std::optional<String>
                                          {
                                              if (!_operations.CopyAssets(assets, target_directory))
                                                  return String("Copy failed.");
                                              _content_dirty = true;
                                              return std::nullopt;
                                          }},
                                          {"Cancel", []() -> std::optional<String> { return std::nullopt; }}
                                      });
        }

        void AssetBrowser::BeginAssetDrag(Asset *asset, const String &display_name)
        {
            _asset_drag_data._assets = GetSelectedAssets();
            if (_asset_drag_data._assets.empty() && asset != nullptr)
                _asset_drag_data._assets.push_back(asset);
            if (_asset_drag_data._assets.empty())
                return;

            UI::EDragType drag_type = UI::EDragType::kFile;
            void *drag_data = &_asset_drag_data;
            if (_asset_drag_data._assets.size() == 1u)
            {
                Asset *dragged_asset = _asset_drag_data._assets.front();
                drag_data = dragged_asset;
                if (dragged_asset->_asset_type == StaticClass<Render::Mesh>())
                    drag_type = UI::EDragType::kMesh;
                else if (dragged_asset->_asset_type == ScriptAsset::StaticType())
                    drag_type = UI::EDragType::kScript;
                else if (dragged_asset->_asset_type == PrefabAssetDocument::StaticType())
                    drag_type = UI::EDragType::kPrefab;
                else
                    drag_data = &_asset_drag_data;
            }
            auto payload = UI::DragPayload{drag_type, drag_data};
            UI::DragDropManager::Get().BeginDrag(payload, display_name);
        }

        void AssetBrowser::CopySelectionToClipboard()
        {
            _clipboard_assets = GetSelectedAssets();
        }

        void AssetBrowser::PasteClipboard()
        {
            if (_clipboard_assets.empty() || !_operations.CopyAssets(_clipboard_assets, _current_path))
                return;
            _content_dirty = true;
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
                    UI::UIElement *content = child->ChildAt(0u);
                    if (auto row = content != nullptr ? content->As<UI::HorizontalBox>() : nullptr)
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
                else
                {
                    UI::UIElement *content = child->ChildAt(0u);
                    if (auto tile = content != nullptr ? content->As<UI::VerticalBox>() : nullptr)
                    {
                        if (auto icon = tile->ChildAt(0u); icon != nullptr)
                            icon->GetSlotAs<UI::LinearSlot>().Size({icon_draw_size, icon_draw_size});
                        if (auto text = tile->ChildAt(1u)->As<UI::Text>(); text != nullptr)
                        {
                            text->GetSlotAs<UI::LinearSlot>().Size({label_width, kIconLabelHeight});
                            text->SetText(FitTextToWidth(text->Name(), label_width, text->FontSize()));
                        }
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
            if (!_clipboard_assets.empty())
                actions.push_back({"Paste", [this]() { PasteClipboard(); }});
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
            actions.push_back({"Copy", [this, asset]()
            {
                _clipboard_assets = {asset};
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
