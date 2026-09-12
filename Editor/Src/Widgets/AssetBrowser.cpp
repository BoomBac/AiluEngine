#include "Widgets/AssetBrowser.h"
#include "Framework/Common/Allocator.hpp"

#include "Animation/AnimationControllerAsset.h"
#include "Animation/Clip.h"
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
#include "Framework/Platform/Platform.h"
#include "Objects/JsonArchive.h"
#include "Project/ProjectManager.h"
#include "Render/2D/Sprite.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/Texture.h"
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
#include <thread>
#if AL_PLATFORM_WINDOWS
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#endif
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

            UI::UIElement *GetEntryContent(UI::UIElement *root)
            {
                if (root == nullptr)
                    return nullptr;
                UI::UIElement *canvas = root->ChildAt(0u);
                if (canvas != nullptr && canvas->As<UI::Canvas>() != nullptr)
                    return canvas->ChildAt(0u);
                return canvas;
            }

            UI::Text *GetEntryText(UI::UIElement *root)
            {
                UI::UIElement *content = GetEntryContent(root);
                if (content == nullptr)
                    return nullptr;
                for (const auto &child: content->GetChildren())
                {
                    if (auto *text = child->As<UI::Text>(); text != nullptr)
                        return text;
                }
                return nullptr;
            }

            UI::Button *AddSubAssetToggle(UI::Canvas *canvas, bool is_list_view, bool is_expanded,
                                          std::function<void()> on_toggle)
            {
                if (canvas == nullptr)
                    return nullptr;

                auto toggle = canvas->AddChild<UI::Button>(is_list_view ? (is_expanded ? "^" : "v")
                                                                         : (is_expanded ? "<" : ">"));
                toggle->Name("SubAssetToggle");
                toggle->GetSlotAs<UI::CanvasSlot>().Size({18.0f, 18.0f});

                UI::UIBrush transparent_brush;
                transparent_brush._type = UI::EUIBrushType::kColor;
                transparent_brush._tint = Colors::kTransparent;
                UI::UIControlVisual normal;
                normal._background = transparent_brush;
                normal._content_color = Colors::kGray;
                UI::UIControlVisual hovered = normal;
                hovered._background._tint = Color(0.25f, 0.25f, 0.25f, 0.8f);
                hovered._content_color = Colors::kWhite;
                toggle->GetStyleOverride().SetNormal(normal);
                toggle->GetStyleOverride().SetHovered(hovered);
                toggle->GetStyleOverride().SetPressed(hovered);
                toggle->GetStyleOverride().SetMinSize({0.0f, 0.0f});
                toggle->GetStyleOverride().SetPadding(Padding(0.0f));
                toggle->GetStyleOverride().SetFontSize(14.0f);
                toggle->OnMouseDown() += [](UI::UIEvent &e)
                {
                    e._is_handled = true;
                };
                toggle->OnMouseClick() += [on_toggle = std::move(on_toggle)](UI::UIEvent &e)
                {
                    on_toggle();
                    e._is_handled = true;
                };
                return toggle;
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
            _directory_tree_data_source = AL_NEW_TAG(EMemoryTag::kEditor, DirectoryTreeDataSource);
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
                fs::path selected_asset_directory;
                if (!_search_text.empty() && value.empty())
                {
                    for (const auto &selected_entry: _selected_entries)
                    {
                        if (selected_entry._asset != nullptr)
                        {
                            selected_asset_directory =
                                fs::path(ResourceMgr::GetResSysPath(selected_entry._asset->_asset_path)).parent_path();
                            break;
                        }
                    }
                }

                String lowered = std::move(value);
                std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });
                _search_text = std::move(lowered);
                if (!selected_asset_directory.empty())
                    NavigateToPath(selected_asset_directory);
                _content_dirty = true;
            };
            _search_input->SetContent("", false);
            _icon_area = _right->AddChild<UI::ScrollView>();
            _icon_area->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill).CrossAlignment(UI::EAlignment::kFill);
            _selected_path_title = _right->AddChild<UI::Text>();
            _selected_path_title->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                .Size({0.0f, 20.0f});
            _selected_path_title->_color = Colors::kGray;
            _selected_path_title->_horizontal_align = EAlignment::kLeft;
            _selected_path_title->SlotPadding() = UI::Padding(4.0f, 0.0f, 4.0f, 0.0f);
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
            AL_DELETE(_directory_tree_data_source);
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
            if (Input::IsKeyJustPressed(EKey::kDELETE))
            {
                ShowDeleteSelectionConfirm(Input::GetGlobalMousePos());
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

            _visible_entries.clear();
            _icon_content->ClearChildren();
            ClearSelection();

            for (const auto &entry: entries)
            {
                _visible_entries.push_back(entry);
                if (entry._type == AssetBrowserEntry::EType::kAsset && IsAssetExpanded(entry._asset))
                {
                    const auto sub_assets = _content.GetSubAssets(entry._asset);
                    _visible_entries.insert(_visible_entries.end(), sub_assets.begin(), sub_assets.end());
                }
            }

            for (const auto &entry: _visible_entries)
            {
                if (entry._type == AssetBrowserEntry::EType::kFolder)
                    CreateFolderWidget(entry);
                else if (entry._type == AssetBrowserEntry::EType::kSubAsset)
                    CreateSubAssetWidget(entry);
                else
                    CreateAssetWidget(entry);
            }

            UpdatePathButtons();

            _content_dirty = false;
            _layout_dirty = true;
        }

        std::tuple<Ref<UI::UIElement>, UI::Image *, UI::Text *> AssetBrowser::CreateEntryWidgetRoot(const String &display_name,
                                                                                                     bool is_sub_asset)
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
            auto canvas = MakeRef<UI::Canvas>();
            border->AddChild(canvas);
            canvas->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
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
                text->SlotPadding() = UI::Padding(kListTextLeftPadding + (is_sub_asset ? 12.0f : 0.0f), 0.0f,
                                                  kListTextLeftPadding, 0.0f);
                canvas->AddChild(hb);
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
                canvas->AddChild(vb);
            }
            root = border;
            root->Name(display_name);
            text->Name(display_name);
            text->SetText(display_name);
            text->_vertical_align = UI::EAlignment::kCenter;
            if (!_is_list_view)
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
                const bool is_right_button = e._key_code == EKey::kRBUTTON;
                if (!is_right_button || !IsSelected(item_path))
                    SelectFolder(item_path, entry_index, item_root, item_text, !is_right_button);
                if (!is_right_button)
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
            const fs::path asset_sys_path = entry._sys_path;
            auto [vb, icon, text] = CreateEntryWidgetRoot(display_name);
            const u32 entry_index = static_cast<u32>(&entry - _visible_entries.data());
            Color tint = asset->_p_obj ? Colors::kWhite : Colors::kGray;
            icon->Name(asset->Name());
            icon->_tint_color = tint;
            vb->OnMouseDown() += [this, asset, asset_sys_path, entry_index, item_root = vb.get(), item_text = text](UI::UIEvent &e)
            {
                const bool is_right_button = e._key_code == EKey::kRBUTTON;
                if (!is_right_button || !IsSelected(asset_sys_path))
                    SelectAsset(asset, entry_index, item_root, item_text, !is_right_button);
                if (!is_right_button)
                    return;
                ShowAssetContextMenu(asset, asset_sys_path, e._mouse_position, item_root, item_text);
                e._is_handled = true;
            };
            icon->SetTexture(AssetTypeRegistry::Get().GetIcon(asset));
            if (!_content.GetSubAssets(asset).empty())
            {
                auto *canvas = vb->ChildAt(0u)->As<UI::Canvas>();
                AddSubAssetToggle(canvas, _is_list_view, IsAssetExpanded(asset), [this, asset]()
                {
                    ToggleAssetExpanded(asset);
                });
            }
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
                    if (e._key_code != EKey::kLBUTTON)
                        return;
                    _drag_source_asset = asset;
                    _is_dragging = false;
                    _drag_start_pos = e._mouse_position;
                };
                icon->OnMouseMove() += [this, asset, display_name](UI::UIEvent &e)
                {
                    if (_drag_source_asset != asset || !Input::IsKeyDown(EKey::kLBUTTON) || _is_dragging)
                        return;
                    f32 dist = Magnitude(e._mouse_position - _drag_start_pos);
                    if (dist > kDragThreshold)
                    {
                        _is_dragging = true;
                        BeginAssetDrag(asset, display_name);
                    }
                };
                icon->OnMouseUp() += [this, asset](UI::UIEvent &)
                {
                    if (_drag_source_asset != asset)
                        return;
                    _drag_source_asset = nullptr;
                    _is_dragging = false;
                };
            }
        }

        void AssetBrowser::CreateSubAssetWidget(const AssetBrowserEntry &entry)
        {
            Asset *owner = entry._asset;
            if (owner == nullptr || entry._sub_asset_guid.IsEmpty())
                return;

            auto [root, icon, text] = CreateEntryWidgetRoot(entry._display_name, true);
            const u32 entry_index = static_cast<u32>(&entry - _visible_entries.data());
            const Ref<Object> sub_asset_object = ResourceMgr::Get().Load<Object>(entry._sub_asset_guid);
            const Type *sub_asset_type = sub_asset_object != nullptr ? sub_asset_object->GetType() : entry._sub_asset_type;
            icon->Name(entry._display_name);
            icon->_tint_color = Colors::kWhite;
            icon->SetTexture(AssetTypeRegistry::Get().GetIcon(entry._sub_asset_guid, sub_asset_type, sub_asset_object));
            root->OnMouseDown() += [this, entry, entry_index, item_root = root.get(), item_text = text](UI::UIEvent &e)
            {
                const bool is_right_button = e._key_code == EKey::kRBUTTON;
                if (!is_right_button || !IsSelected(entry._sys_path, entry._sub_asset_guid))
                    SelectSubAsset(entry, entry_index, item_root, item_text, !is_right_button);
                if (!is_right_button)
                    return;
                ShowAssetContextMenu(entry._asset, entry._sys_path, e._mouse_position, item_root, item_text);
                e._is_handled = true;
            };
            root->OnMouseEnter() += [this, item_root = root.get()](UI::UIEvent &e)
            {
                _hover_item = item_root;
                UpdateEntryVisual(item_root, true);
            };
            root->OnMouseExit() += [this, item_root = root.get()](UI::UIEvent &e)
            {
                if (_hover_item == item_root)
                {
                    _hover_item = nullptr;
                    UpdateEntryVisual(item_root, false);
                }
            };
            icon->OnMouseDoubleClick() += [this, owner](UI::UIEvent &e)
            {
                OpenAsset(owner);
                e._is_handled = true;
            };
            _icon_content->AddChild(root);
        }

        void AssetBrowser::SelectFolder(const fs::path &path, u32 index, UI::UIElement *root, UI::Text *text, bool preserve_modifiers)
        {
            SelectEntry(path, nullptr, index, root, text, Guid::EmptyGuid(), preserve_modifiers);
        }

        void AssetBrowser::SelectAsset(Asset *asset, u32 index, UI::UIElement *root, UI::Text *text, bool preserve_modifiers)
        {
            if (asset != nullptr)
                SelectEntry(fs::path(ResourceMgr::GetResSysPath(asset->_asset_path)), asset, index, root, text,
                            Guid::EmptyGuid(), preserve_modifiers);
        }

        void AssetBrowser::SelectSubAsset(const AssetBrowserEntry &entry, u32 index, UI::UIElement *root, UI::Text *text,
                                          bool preserve_modifiers)
        {
            SelectEntry(entry._sys_path, entry._asset, index, root, text, entry._sub_asset_guid, preserve_modifiers);
        }

        bool AssetBrowser::IsSelected(const fs::path &path, const Guid &sub_asset_guid) const
        {
            return std::any_of(_selected_entries.begin(), _selected_entries.end(), [&path, &sub_asset_guid](const SelectedEntry &entry)
            {
                return entry._path == path && entry._sub_asset_guid == sub_asset_guid;
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
            {
                UpdateSelectedPathDisplay();
                return;
            }

            const SelectedEntry *primary = &_selected_entries.back();
            if (_selection_anchor >= 0 && _selection_anchor < static_cast<i32>(_visible_entries.size()))
            {
                const fs::path anchor_path = _visible_entries[_selection_anchor]._sys_path;
                const Guid anchor_sub_asset_guid = _visible_entries[_selection_anchor]._sub_asset_guid;
                for (const auto &entry: _selected_entries)
                {
                    if (entry._path == anchor_path && entry._sub_asset_guid == anchor_sub_asset_guid)
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
            UpdateSelectedPathDisplay();
        }

        void AssetBrowser::SelectEntry(const fs::path &path, Asset *asset, u32 index, UI::UIElement *root, UI::Text *text,
                                       const Guid &sub_asset_guid, bool preserve_modifiers)
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
                selected._sub_asset_guid = _visible_entries[entry_index]._sub_asset_guid;
                selected._root = _icon_content->ChildAt(entry_index);
                selected._text = GetEntryText(selected._root);
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
                    if (!IsSelected(_visible_entries[i]._sys_path, _visible_entries[i]._sub_asset_guid))
                        _selected_entries.push_back(make_entry(i));
                }
            }
            else if (use_control)
            {
                auto it = std::find_if(_selected_entries.begin(), _selected_entries.end(), [&path, &sub_asset_guid](const SelectedEntry &entry)
                {
                    return entry._path == path && entry._sub_asset_guid == sub_asset_guid;
                });
                if (it != _selected_entries.end())
                    _selected_entries.erase(it);
                else
                    _selected_entries.push_back({path, asset, sub_asset_guid, root, text});
                _selection_anchor = static_cast<i32>(index);
            }
            else
            {
                _selected_entries.clear();
                _selected_entries.push_back({path, asset, sub_asset_guid, root, text});
                _selection_anchor = static_cast<i32>(index);
            }

            SyncPrimarySelection();
            UpdateSelectionVisuals();
        }

        bool AssetBrowser::IsAssetExpanded(const Asset *asset) const
        {
            return asset != nullptr && std::find(_expanded_asset_guids.begin(), _expanded_asset_guids.end(), asset->GetGuid()) !=
                                             _expanded_asset_guids.end();
        }

        void AssetBrowser::ToggleAssetExpanded(Asset *asset)
        {
            if (asset == nullptr)
                return;

            auto it = std::find(_expanded_asset_guids.begin(), _expanded_asset_guids.end(), asset->GetGuid());
            if (it == _expanded_asset_guids.end())
                _expanded_asset_guids.push_back(asset->GetGuid());
            else
                _expanded_asset_guids.erase(it);
            _content_dirty = true;
        }

        void AssetBrowser::ClearSelection()
        {
            _selected_entries.clear();
            _selection_anchor = -1;
            _selected_item_root = nullptr;
            _selected_item_text = nullptr;
            _selected_asset = nullptr;
            _selected_folder_path.clear();
            UpdateSelectedPathDisplay();
        }

        void AssetBrowser::UpdateSelectedPathDisplay()
        {
            if (_selected_path_title == nullptr)
                return;
            if (_selected_entries.empty())
            {
                _selected_path_title->SetText("");
                return;
            }

            const SelectedEntry &first = _selected_entries.front();
            String path;
            if (first._asset != nullptr)
                path = ToChar(FormatLogicalAssetPath(first._asset->_asset_path));
            else
                path = ToChar(_content.GetAssetDirectory(first._path));
            if (_selected_entries.size() > 1u)
                path += "...";
            _selected_path_title->SetText(path);
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
                payload._type == UI::EDragType::kPrefab || payload._type == UI::EDragType::kTexture ||
                payload._type == UI::EDragType::kMaterial || payload._type == UI::EDragType::kAsset ||
                payload._type == UI::EDragType::kAnimation)
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
                if (dragged_asset->_asset_type == StaticClass<Render::Mesh>() ||
                    dragged_asset->_asset_type == StaticClass<Render::SkeletonMesh>())
                    drag_type = UI::EDragType::kMesh;
                else if (dragged_asset->_asset_type == ScriptAsset::StaticType())
                    drag_type = UI::EDragType::kScript;
                else if (dragged_asset->_asset_type == PrefabAssetDocument::StaticType())
                    drag_type = UI::EDragType::kPrefab;
                else if (dragged_asset->_asset_type == Render::Texture2D::StaticType())
                    drag_type = UI::EDragType::kTexture;
                else if (dragged_asset->_asset_type == Render::Material::StaticType())
                    drag_type = UI::EDragType::kMaterial;
                else if (dragged_asset->_asset_type == Render::Sprite::StaticType())
                    drag_type = UI::EDragType::kAsset;
                else if (dragged_asset->_asset_type == AnimationClip::StaticType() ||
                         dragged_asset->_asset_type == AnimationControllerAsset::StaticType())
                    drag_type = UI::EDragType::kAnimation;
                else
                    drag_type = UI::EDragType::kAsset;
            }
            auto payload = UI::DragPayload{drag_type, drag_data};
            auto *dragged_mesh = asset != nullptr ? asset->As<Render::Mesh>() : nullptr;
            LOG_INFO("[AssetBrowser] BeginAssetDrag asset={} selected={} type={} data={} obj={} mesh={} vb={} ib={}",
                     asset != nullptr ? asset->Name() : "<null>", _asset_drag_data._assets.size(),
                     StaticEnum<UI::EDragType>()->GetNameByEnum(drag_type), drag_data,
                     asset != nullptr ? asset->_p_obj.get() : nullptr, dragged_mesh,
                     dragged_mesh != nullptr ? dragged_mesh->GetVertexBuffer().get() : nullptr,
                     dragged_mesh != nullptr && dragged_mesh->SubmeshCount() > 0u ? dragged_mesh->GetIndexBuffer().get() : nullptr);
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
            const Vector2f parent_size = _icon_area->GetViewportSize();
            const f32 cell_width = _is_list_view ? parent_size.x : std::max(kIconCellMinWidth, _icon_size + kIconCellPadding * 2.0f + kIconCellGap);
            const f32 icon_draw_size = _is_list_view ? kListIconSize : std::max(1.0f, _icon_size);
            const f32 cell_height = _is_list_view ? kListRowHeight : icon_draw_size + kIconLabelHeight + kIconCellPadding * 2.0f;
            f32 x = 0.0f;
            f32 y = 0.0f;
            u32 num_per_row = _is_list_view ? 1u : (u32) (parent_size.x / cell_width);
            if (num_per_row == 0u)
                num_per_row = 1u;
            for (u32 i = 0; i < (u32) _icon_content->GetChildren().size(); i++)
            {
                auto child = _icon_content->ChildAt(i);
                child->GetSlotAs<UI::CanvasSlot>().Position({x, y}).Size({cell_width, cell_height});
                auto canvas = child->ChildAt(0u) != nullptr ? child->ChildAt(0u)->As<UI::Canvas>() : nullptr;
                if (canvas == nullptr || canvas->ChildAt(0u) == nullptr)
                    continue;

                auto content = canvas->ChildAt(0u);
                const bool has_toggle = canvas->GetChildren().size() > 1u;
                const f32 toggle_space = _is_list_view && has_toggle ? 20.0f : 0.0f;
                const f32 label_width = _is_list_view ? std::max(0.0f, parent_size.x - kListIconSize -
                                                                    kListTextLeftPadding * 2.0f - 12.0f - toggle_space) :
                                                         std::max(0.0f, cell_width - kIconCellPadding * 2.0f);
                content->GetSlotAs<UI::CanvasSlot>().Position({toggle_space, 0.0f})
                    .Size({std::max(0.0f, cell_width - toggle_space), cell_height});
                if (has_toggle)
                {
                    auto toggle = canvas->ChildAt(1u);
                    const f32 toggle_x = _is_list_view ? 0.0f : std::max(0.0f, cell_width - 20.0f);
                    toggle->GetSlotAs<UI::CanvasSlot>().Position({toggle_x,
                                                                   std::max(0.0f, (cell_height - 18.0f) * 0.5f)}).Size({18.0f, 18.0f});
                }
                if (_is_list_view)
                {
                    if (auto row = content->As<UI::HorizontalBox>(); row != nullptr)
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
                    if (auto tile = content->As<UI::VerticalBox>(); tile != nullptr)
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

            if (!Input::IsKeyDown(EKey::kLBUTTON))
            {
                _drag_source_asset = nullptr;
                _is_dragging = false;
            }

            HandleShortcuts();
            AssetTypeRegistry::Get().BeginFrame();

            // 启动时预览可能因为 GPU 资源还没就绪而只能先用静态图标；等它就绪后重建一次，
            // 把图标换成真正的预览（AssetTypeRegistry 在重试成功后会抬高版本号）。
            if (const u32 preview_revision = AssetTypeRegistry::Get().PreviewRevision(); preview_revision != _preview_revision)
            {
                _preview_revision = preview_revision;
                _content_dirty = true;
            }

            if (_directory_tree_dirty)
                RefreshDirectoryTree();

            // 用 ScrollView 真正给 Canvas 的可用视口（有滚动条时要扣掉条宽），
            // 这样列数/省略宽度和实际排布宽度一致；同时滚动条出现/消失也会被发现。
            const Vector2f content_size = _icon_area->GetViewportSize();
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
                        creator._create_dialog(target_directory, popup_pos, [this]() { _content_dirty = true; });
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
            Vector<PopupMenuAction> create_actions;
            BuildCreateAssetActions(create_actions, _current_path, popup_pos);
            actions.push_back({"Create", {}, false, std::move(create_actions)});
            if (!_clipboard_assets.empty())
                actions.push_back({"Paste", [this]() { PasteClipboard(); }});
            actions.push_back({"Open in File Explorer", [this]()
            {
                OpenInFileExplorer(_current_path);
            }});
            actions.push_back({"Refresh", [this]() { _content_dirty = true; }});
            EditorPopup::ShowActionMenuAt(popup_pos, actions);
        }

        void AssetBrowser::ShowDeleteSelectionConfirm(Vector2f popup_pos, Asset *fallback_asset)
        {
            Vector<Asset *> assets = GetSelectedAssets();
            if (assets.empty() && fallback_asset != nullptr)
                assets.push_back(fallback_asset);
            if (assets.empty())
                return;

            const String message = assets.size() == 1u ? std::format("Delete asset \"{}\"?", assets.front()->Name()) :
                                                         std::format("Delete {} selected assets?", assets.size());
            EditorPopup::ShowConfirmAt(popup_pos, message, [this, assets]()
            {
                bool deleted = false;
                for (auto *asset: assets)
                    deleted = _operations.DeleteAsset(asset) || deleted;
                if (deleted)
                {
                    ClearSelection();
                    _content_dirty = true;
                }
            });
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
            actions.push_back({"Open in File Explorer", [this, folder_path]()
            {
                OpenInFileExplorer(folder_path);
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
            Vector<PopupMenuAction> create_actions;
            BuildCreateAssetActions(create_actions, folder_path, popup_pos);
            actions.push_back({"Create", {}, false, std::move(create_actions)});
            EditorPopup::ShowActionMenuAt(popup_pos, actions);
        }

        void AssetBrowser::ShowAssetContextMenu(Asset *asset, const fs::path &asset_sys_path, Vector2f popup_pos,
                                                UI::UIElement *item_root, UI::Text *item_text)
        {
            if (asset == nullptr)
                return;

            Vector<PopupMenuAction> actions;
            if (AssetEditorRegistry::Get().CanOpen(asset->_asset_type))
            {
                actions.push_back({"Open", [this, asset]() { OpenAsset(asset); }});
            }
            actions.push_back({"Open in File Explorer", [this, asset_sys_path]()
            {
                OpenInFileExplorer(asset_sys_path, true);
            }});
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
            actions.push_back({"Delete", [this, asset, popup_pos]()
            {
                ShowDeleteSelectionConfirm(popup_pos, asset);
            }, true});
            EditorPopup::ShowActionMenuAt(popup_pos, actions);
        }

        void AssetBrowser::OpenInFileExplorer(const fs::path &path, bool select_path)
        {
#if AL_PLATFORM_WINDOWS
            if (path.empty())
                return;

            fs::path explorer_path = fs::path(PathUtils::ToPlatformPath(path.wstring()));
            std::error_code path_error;
            if (select_path && !fs::exists(path, path_error))
            {
                if (path_error)
                {
                    LOG_WARNING(L"AssetBrowser: cannot inspect path {}", path.wstring());
                    return;
                }
                explorer_path = path.parent_path();
                select_path = false;
            }
            if (explorer_path.empty())
                return;

            if (select_path)
            {
                const fs::path target_path = explorer_path;
                std::thread([target_path]()
                {
                    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                    const bool should_uninitialize = com_result == S_OK || com_result == S_FALSE;
                    if (SUCCEEDED(com_result) || com_result == RPC_E_CHANGED_MODE)
                    {
                        PIDLIST_ABSOLUTE item_id_list = ILCreateFromPathW(target_path.c_str());
                        if (item_id_list != nullptr)
                        {
                            const HRESULT select_result = SHOpenFolderAndSelectItems(item_id_list, 0, nullptr, 0);
                            ILFree(item_id_list);
                            if (should_uninitialize)
                                CoUninitialize();
                            if (SUCCEEDED(select_result))
                                return;
                        }
                        else if (should_uninitialize)
                        {
                            CoUninitialize();
                        }
                    }

                    const std::wstring arguments = std::format(L"/select,\"{}\"", target_path.wstring());
                    const HINSTANCE fallback_result = ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(),
                                                                     nullptr, SW_SHOWNORMAL);
                    if (reinterpret_cast<INT_PTR>(fallback_result) <= 32)
                        LOG_WARNING(L"AssetBrowser: select path in File Explorer failed, {}", target_path.wstring());
                }).detach();
                return;
            }

            const HINSTANCE result = ShellExecuteW(nullptr, L"open", explorer_path.c_str(), nullptr, nullptr,
                                                    SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(result) <= 32)
                LOG_WARNING(L"AssetBrowser: open path in File Explorer failed, {}", explorer_path.wstring());
#else
            (void)path;
            (void)select_path;
            LOG_WARNING("AssetBrowser: opening paths in File Explorer is only supported on Windows.");
#endif
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
