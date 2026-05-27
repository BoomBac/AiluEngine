#include "Widgets/AssetBrowser.h"
#include "Common/EditorPopup.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/ResourceMgr.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"
#include "UI/UIRenderer.h"
#include "UI/DragDrop.h"
#include "Render/AssetPreviewGenerator.h"
#include "Framework/Common/Input.h"

#include <cctype>
#include <memory>

namespace Ailu
{
    namespace Editor
    {
        using namespace UI;

        namespace
        {
            enum class EImportPopupType : u8
            {
                kDirect,
                kTexture,
                kMesh
            };

            String TrimNameCopy(const String &value)
            {
                size_t begin = 0;
                size_t end = value.size();
                while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])))
                    ++begin;
                while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
                    --end;
                return value.substr(begin, end - begin);
            }

            bool HasInvalidFileNameChars(const String &value)
            {
                static const String kInvalidChars = "\\/:*?\"<>|";
                return value.find_first_of(kInvalidChars) != String::npos;
            }

            std::optional<String> ValidateEntryName(const String &value)
            {
                if (value.empty())
                    return String("Name cannot be empty.");
                if (value == "." || value == "..")
                    return String("Name is reserved.");
                if (HasInvalidFileNameChars(value))
                    return String("Name contains invalid characters.");
                return std::nullopt;
            }

            WString NormalizePathWithoutTrailingSlash(const WString &path)
            {
                WString normalized = PathUtils::FormatFilePath(path);
                while (!normalized.empty() && normalized.back() == L'/')
                    normalized.pop_back();
                return normalized;
            }

            WString NormalizeDirectoryPath(const WString &path)
            {
                WString normalized = NormalizePathWithoutTrailingSlash(path);
                if (!normalized.empty())
                    normalized.push_back(L'/');
                return normalized;
            }

            WString AppendChildAssetPath(const WString &directory_asset_path, const WString &file_name)
            {
                if (directory_asset_path.empty())
                    return file_name;
                return NormalizeDirectoryPath(directory_asset_path) + file_name;
            }

            bool RewriteAssetHeaderName(const WString &sys_path, const String &new_name)
            {
                WString content;
                if (!FileManager::ReadFile(sys_path, content))
                    return false;

                auto lines = StringUtils::Split(content, L"\n");
                if (lines.size() < 3u)
                    return false;

                lines[2] = std::format(L"name: {}", ToWChar(new_name.c_str()));
                std::wstringstream buffer;
                for (size_t i = 0; i < lines.size(); ++i)
                {
                    if (!lines[i].empty() && lines[i].back() == L'\r')
                        lines[i].pop_back();
                    buffer << lines[i];
                    if (i + 1u < lines.size())
                        buffer << L'\n';
                }
                return FileManager::WriteFile(sys_path, false, buffer.str());
            }

            EImportPopupType GetImportPopupType(const WString &sys_path)
            {
                const String ext = fs::path(sys_path).extension().string();
                if (ext == ".fbx" || ext == ".FBX")
                    return EImportPopupType::kMesh;
                if (ResourceMgr::kHDRImageExt.contains(ext) || ResourceMgr::kLDRImageExt.contains(ext))
                    return EImportPopupType::kTexture;
                return EImportPopupType::kDirect;
            }
        }// namespace

        AssetBrowser::AssetBrowser() : DockWindow("Asset Browser")
        {
            _sv = _content_root->AddChild<UI::SplitView>();
            _sv->SlotSizePolicy(UI::ESizePolicy::kFill);
            _sv->SlotPadding(_content_root->Thickness());
            _sv->AddChild<UI::Border>();
            _right = _sv->AddChild<UI::VerticalBox>();
            _right->SlotPadding({2.0f,2.0f,0.0f,0.0f});
            _right->SlotSizePolicy(UI::ESizePolicy::kFill);
            _on_size_change += [this](Vector2f new_size)
            {
                auto t = _content_root->Thickness();
                _sv->SlotSize(new_size.x, new_size.y - kTitleBarHeight);
            };
            _current_path = g_pResourceMgr->EngineResRootPath();
            auto hb = _right->AddChild<UI::HorizontalBox>();
            hb->SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            hb->SlotAlignmentH(UI::EAlignment::kFill);
            auto back_btn = hb->AddChild<UI::Button>();
            back_btn->SlotSizePolicy(UI::ESizePolicy::kFixed);
            back_btn->SlotSize(16.0f, 16.0f);
            back_btn->OnMouseClick() += [&](UI::UIEvent& e) 
            {
                _current_path = _current_path.parent_path();
                LOG_INFO("AssetBrowser: back to {}", _current_path.string());
                _is_dirty = true;
            };
            back_btn->SetText("<");
            _path_title = hb->AddChild<UI::Text>("Current Path");
            _path_title->SlotSizePolicy(UI::ESizePolicy::kFill);
            _path_title->_horizontal_align = EAlignment::kLeft;
            _icon_area = _right->AddChild<UI::ScrollView>();
            _icon_area->SlotSizePolicy(UI::ESizePolicy::kFill);
            _icon_area->SlotAlignmentH(UI::EAlignment::kFill);
            auto slider = _right->AddChild<UI::Slider>();
            slider->_range = {50.0f, 200.0f};
            slider->SetValue(64.0f);
            _icon_content = _icon_area->AddChild<UI::Canvas>();
            _icon_content->SlotSizePolicy(UI::ESizePolicy::kAuto);
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
            slider->_on_value_change += [&](f32 value)
            {
                _icon_size = value;
                _is_icon_layout_dirty = true;
            };
            DropHandler handler;
            handler._can_drop = [](const DragPayload &payload) -> bool
            {
                return payload._type == EDragType::kFile;
            };
            handler._on_drop = [this](const DragPayload &payload, f32 x, f32 y)
            {
                LOG_INFO("{} drop", StaticEnum<EDragType>()->GetNameByEnum(payload._type));
            };
            _icon_area->SetDropHandler(handler);
            _icon_area->OnFileDrop() += [this](UI::UIEvent &e)
            {
                Vector<WString> dropped_files;
                dropped_files.reserve(e._drop_files.size());
                for (auto &f: e._drop_files)
                {
                    LOG_INFO(L"Drop file {}",(f));
                    dropped_files.push_back(f);
                }
                QueueImportFiles(dropped_files, e._mouse_position);
            };
        }

        void AssetBrowser::QueueImportFiles(const Vector<WString> &files, Vector2f popup_pos)
        {
            if (files.empty())
                return;

            const bool was_empty = _pending_import_files.empty();
            _import_popup_pos = popup_pos;
            _pending_import_files.insert(_pending_import_files.end(), files.begin(), files.end());
            if (was_empty)
                ShowNextImportPopup();
        }

        void AssetBrowser::ShowNextImportPopup()
        {
            while (!_pending_import_files.empty())
            {
                const WString &sys_path = _pending_import_files.front();
                switch (GetImportPopupType(sys_path))
                {
                case EImportPopupType::kTexture:
                case EImportPopupType::kMesh:
                    ShowImportPopupForFile(sys_path);
                    return;
                case EImportPopupType::kDirect:
                default:
                    g_pResourceMgr->ImportResource(sys_path, _current_path.wstring());
                    _pending_import_files.erase(_pending_import_files.begin());
                    _is_dirty = true;
                    break;
                }
            }
        }

        void AssetBrowser::ShowImportPopupForFile(const WString &sys_path)
        {
            const String file_name = fs::path(sys_path).filename().string();
            const auto finish_popup = [this]()
            {
                UIManager::Get()->HidePopup();
                AdvanceImportQueue();
            };

            switch (GetImportPopupType(sys_path))
            {
            case EImportPopupType::kTexture:
            {
                auto setting = std::make_shared<TextureImportSetting>(TextureImportSetting::Default());
                EditorPopup::ShowDialogAt(_import_popup_pos, "AssetBrowserTextureImportPrompt",
                                          std::format("Import Texture: {}", file_name), {320.0f, 180.0f},
                                          [setting, file_name](UI::VerticalBox *content, UI::Text *)
                                          {
                                              auto *file_text = content->AddChild<Text>(std::format("File: {}", file_name));
                                              file_text->SlotSizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
                                              file_text->_horizontal_align = EAlignment::kLeft;

                                              auto *srgb = EditorPopup::AddCheckBoxRow(content, "sRGB", setting->_is_sRGB);
                                              srgb->_on_click += [setting](bool checked)
                                              {
                                                  setting->_is_sRGB = checked;
                                              };

                                              auto *mipmap = EditorPopup::AddCheckBoxRow(content, "Generate Mipmap", setting->_generate_mipmap);
                                              mipmap->_on_click += [setting](bool checked)
                                              {
                                                  setting->_generate_mipmap = checked;
                                              };

                                              auto *readable = EditorPopup::AddCheckBoxRow(content, "Readable", setting->_is_readable);
                                              readable->_on_click += [setting](bool checked)
                                              {
                                                  setting->_is_readable = checked;
                                              };
                                          },
                                          {
                                                  {"Import", [this, setting, sys_path, finish_popup]() -> std::optional<String>
                                                   {
                                                       g_pResourceMgr->ImportResource(sys_path, _current_path.wstring(), *setting);
                                                       _is_dirty = true;
                                                       finish_popup();
                                                       return std::nullopt;
                                                   }, false, false},
                                                  {"Cancel", [finish_popup]() -> std::optional<String>
                                                   {
                                                       finish_popup();
                                                       return std::nullopt;
                                                   }, false, false}
                                          });
                return;
            }
            case EImportPopupType::kMesh:
            {
                auto setting = std::make_shared<MeshImportSetting>(MeshImportSetting::Default());
                setting->_import_flag |= MeshImportSetting::kImportFlagMesh;
                EditorPopup::ShowDialogAt(_import_popup_pos, "AssetBrowserMeshImportPrompt",
                                          std::format("Import Mesh: {}", file_name), {320.0f, 180.0f},
                                          [setting, file_name](UI::VerticalBox *content, UI::Text *)
                                          {
                                              auto *file_text = content->AddChild<Text>(std::format("File: {}", file_name));
                                              file_text->SlotSizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
                                              file_text->_horizontal_align = EAlignment::kLeft;

                                              auto *materials = EditorPopup::AddCheckBoxRow(content, "Import Materials", setting->_is_import_material);
                                              materials->_on_click += [setting](bool checked)
                                              {
                                                  setting->_is_import_material = checked;
                                              };

                                              auto *combine_mesh = EditorPopup::AddCheckBoxRow(content, "Combine Meshes", setting->_is_combine_mesh);
                                              combine_mesh->_on_click += [setting](bool checked)
                                              {
                                                  setting->_is_combine_mesh = checked;
                                              };

                                              const bool import_animation = (setting->_import_flag & MeshImportSetting::kImportFlagAnimation) != 0;
                                              auto *animation = EditorPopup::AddCheckBoxRow(content, "Import Animation", import_animation);
                                              animation->_on_click += [setting](bool checked)
                                              {
                                                  if (checked)
                                                      setting->_import_flag |= MeshImportSetting::kImportFlagAnimation;
                                                  else
                                                      setting->_import_flag &= ~MeshImportSetting::kImportFlagAnimation;
                                              };
                                          },
                                          {
                                                  {"Import", [this, setting, sys_path, finish_popup]() -> std::optional<String>
                                                   {
                                                       g_pResourceMgr->ImportResource(sys_path, _current_path.wstring(), *setting);
                                                       _is_dirty = true;
                                                       finish_popup();
                                                       return std::nullopt;
                                                   }, false, false},
                                                  {"Cancel", [finish_popup]() -> std::optional<String>
                                                   {
                                                       finish_popup();
                                                       return std::nullopt;
                                                   }, false, false}
                                          });
                return;
            }
            case EImportPopupType::kDirect:
            default:
                g_pResourceMgr->ImportResource(sys_path, _current_path.wstring());
                _is_dirty = true;
                AdvanceImportQueue();
                return;
            }
        }

        void AssetBrowser::AdvanceImportQueue()
        {
            if (!_pending_import_files.empty())
                _pending_import_files.erase(_pending_import_files.begin());
            ShowNextImportPopup();
        }

        void AssetBrowser::Update(f32 dt)
        {
            DockWindow::Update(dt);
            static auto s_folder_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"folder.alasset");
            static auto s_file_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"file.alasset");
            static auto s_mesh_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"3d.alasset");
            static auto s_shader_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"shader.alasset");
            static auto s_image_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"image.alasset");
            static auto s_scene_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"dark/scene.alasset");
            static auto s_material_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"dark/material.alasset");
            static auto s_animclip_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"dark/anim_clip.alasset");
            static auto s_skeleton_icon = g_pResourceMgr->Get<Texture2D>(EnginePath::kEngineIconPathW + L"dark/skeleton.alasset");

            Vector2f parent_size = _icon_area->GetContentRect().zw;
            if (parent_size.x <= 0.0f || parent_size.y <= 0.0f)
                return;

            if (!NearbyEqual(parent_size, _last_icon_area_size))
            {
                _last_icon_area_size = parent_size;
                _is_icon_layout_dirty = true;
            }

            if (_is_dirty)
            {
                if (fs::exists(_current_path))
                {
                    fs::directory_iterator curdir_it(_current_path);
                    _icon_content->ClearChildren();
                    //更新当前资产列表
                    SearchFilterByDirectory filter({PathUtils::ExtractAssetPath(_current_path.wstring())});
                    _cur_dir_assets.clear();
                    _cur_dir_assets = std::move(g_pResourceMgr->GetAssets(filter));
                    _path_title->SetText(_current_path.string());
                    const auto create_icon_group = []() -> std::tuple<Ref<UI::VerticalBox>,UI::Image*,UI::Text*>
                    {
                        auto vb = MakeRef<UI::VerticalBox>();
                        vb->SlotSizePolicy(UI::ESizePolicy::kFixed);
                        vb->SlotPadding(2.0f);
                        auto icon = vb->AddChild<UI::Image>();
                        icon->SlotSizePolicy(UI::ESizePolicy::kFill);
                        icon->SlotAlignmentH(UI::EAlignment::kFill);
                        auto text = vb->AddChild<UI::Text>();
                        text->SlotAlignmentH(UI::EAlignment::kFill);
                        text->_vertical_align = UI::EAlignment::kCenter;
                        text->SlotPadding(6.0f);
                        text->FontSize(14.0f);
                        return std::make_tuple(vb, icon, text);
                    };
                    for (auto &dir_it: curdir_it)
                    {
                        if (!dir_it.is_directory())
                            continue;
                        fs::path item_path = dir_it.path();
                        const auto folder_sys_path = item_path.wstring();
                        auto [vb, icon,text] = create_icon_group();
                        vb->SlotSize({_icon_size, _icon_size + 20.0f});
                        icon->Name(item_path.filename().string().c_str());
                        text->SetText(item_path.filename().string().c_str());
                        icon->SetTexture(s_folder_icon);
                        vb->OnMouseDown() += [this, folder_sys_path](UI::UIEvent &e)
                        {
                            if (e._key_code != EKey::kRBUTTON)
                                return;
                            ShowFolderContextMenu(folder_sys_path, e._mouse_position);
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
                            _current_path = item_path;
                            _is_dirty = true;
                        };
                        //icon->OnMouseDown() += [this, icon](UI::UIEvent &e)
                        //{
                        //    s_is_drag_start = true;
                        //    auto payload = DragPayload{EDragType::kFolder, nullptr};
                        //    DragDropManager::Get().BeginDrag(payload, "folder");
                        //};
                        _icon_content->AddChild(vb);
                    }
                    for (auto asset: _cur_dir_assets)
                    {
                        auto [vb, icon, text] = create_icon_group();
                        Color tint = asset->_p_obj ? Colors::kWhite : Colors::kGray;
                        vb->SlotSize({_icon_size, _icon_size + 20.0f});
                        icon->Name(asset-> Name());
                        text->SetText(asset->_p_obj ? asset->_p_obj->Name() : asset->Name());
                        icon->_tint_color = tint;
                        vb->OnMouseDown() += [this, asset](UI::UIEvent &e)
                        {
                            if (e._key_code != EKey::kRBUTTON)
                                return;
                            ShowAssetContextMenu(asset, e._mouse_position);
                            e._is_handled = true;
                        };
                        if (asset->_asset_type == StaticClass<Render::Mesh>())
                        {
                            if (asset->_p_obj)
                            {
                                auto mesh = asset->As<Render::Mesh>();
                                if (!_mesh_preview_icons.contains(mesh))
                                {
                                    Ref<RenderTexture> mesh_icon{nullptr};
                                    AssetPreviewGenerator::GeneratorMeshSnapshot(512u, 512u, mesh, mesh_icon);
                                    _mesh_preview_icons[mesh] = mesh_icon;
                                }
                                icon->SetTexture(_mesh_preview_icons[mesh].get());
                            }
                            else
                            icon->SetTexture(s_mesh_icon);
                        }
                        else if (asset->_asset_type == StaticClass<Render::Shader>())
                        {
                            icon->SetTexture(s_shader_icon);
                        }
                        else if (asset->_asset_type == StaticClass<SceneManagement::Scene>())
                        {
                            icon->SetTexture(s_scene_icon);
                        }
                        else if (asset->_asset_type == StaticClass<Render::Texture2D>())
                        {
                            if (asset->_p_obj == nullptr)
                            {
                                //并非当帧完成
                                icon->SetTexture(g_pResourceMgr->Load<Texture2D>(asset->_asset_path).get());
                            }
                            else
                            {
                                icon->SetTexture(asset->As<Texture>());
                            }
                        }
                        else if (asset->_asset_type == StaticClass<Render::Material>())
                            icon->SetTexture(s_material_icon);
                        else if (asset->_asset_type == StaticClass<AnimationClip>())
                            icon->SetTexture(s_animclip_icon);
                        else if (asset->_asset_type == StaticClass<Render::SkeletonMesh>())
                            icon->SetTexture(s_skeleton_icon);
                        else {};
                        icon->OnMouseEnter() += [this, icon](UI::UIEvent &e)
                        {
                            _hover_item = e._current_target;
                            e._current_target->As<Image>()->_tint_color = Colors::kYellow;
                        };
                        icon->OnMouseExit() += [this,tint](UI::UIEvent &e)
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
                        if (asset->_asset_type == StaticClass<Render::Mesh>())
                        {
                            icon->OnMouseDown() += [this, icon, asset](UI::UIEvent &e)
                            {
                                _is_dragging = false;
                                _drag_start_pos = e._mouse_position;
                            };
                            icon->OnMouseMove() += [this, icon, asset](UI::UIEvent &e)
                            {
                                if (Input::IsKeyDown(EKey::kLBUTTON))
                                {
                                    if (!_is_dragging)
                                    {
                                        f32 dist = Magnitude(e._mouse_position - _drag_start_pos);
                                        if (dist > kDragThreshold)
                                        {
                                            _is_dragging = true;
                                            auto payload = DragPayload{EDragType::kMesh, asset};
                                            DragDropManager::Get().BeginDrag(payload, "mesh");
                                        }
                                    }
                                }
                            };

                        }
                    }
                }
                _is_icon_layout_dirty = true;
                _is_dirty = false;
            }

            if (_is_icon_layout_dirty)
            {
                f32 x = 0.0f, y = 0.0f;
                u32 num_per_row = (u32) (parent_size.x / _icon_size);
                if (num_per_row == 0) num_per_row = 1;
                for (u32 i = 0; i < (u32) _icon_content->GetChildren().size(); i++)
                {
                    auto child = _icon_content->ChildAt(i);
                    child->SlotPosition({x, y});
                    child->SlotSize({_icon_size, _icon_size + 20.0f});
                    if ((i + 1) % num_per_row == 0)
                    {
                        x = 0.0f;
                        y += _icon_size + 20.0f;
                    }
                    else
                    {
                        x += _icon_size;
                    }
                }
                _is_icon_layout_dirty = false;
            }
        }

        void AssetBrowser::OpenAsset(Asset *asset)
        {
            if (asset == nullptr)
                return;

            if (asset->_asset_type == StaticClass<SceneManagement::Scene>())
            {
                SceneManagement::SceneMgr::Get().OpenScene(asset->_asset_path);
            }
            else if (asset->_asset_type == StaticClass<Render::Mesh>())
            {
                if (asset->_p_obj == nullptr)
                {
                    g_pResourceMgr->Load<Mesh>(asset->_asset_path);
                    _is_dirty = true;
                }
            }
        }

        void AssetBrowser::ShowBlankAreaContextMenu(Vector2f popup_pos)
        {
            Vector<PopupMenuAction> actions;
            actions.push_back({"New Folder", [this, popup_pos]()
            {
                EditorPopup::ShowTextInputAt(popup_pos, "Create Folder",
                                             MakeUniqueEntryName(_current_path.wstring(), "NewFolder", L"", true),
                                             [this](const String &input) -> std::optional<String>
                                             {
                                                 const String name = TrimNameCopy(input);
                                                 if (auto error = ValidateEntryName(name); error.has_value())
                                                     return error;
                                                 if (!CreateFolderEntry(name))
                                                     return String("Folder already exists.");
                                                 return std::nullopt;
                                             });
            }});
            actions.push_back({"New Scene", [this, popup_pos]()
            {
                EditorPopup::ShowTextInputAt(popup_pos, "Create Scene",
                                             MakeUniqueEntryName(_current_path.wstring(), "NewScene", L".almap", false),
                                             [this](const String &input) -> std::optional<String>
                                             {
                                                 const String name = TrimNameCopy(input);
                                                 if (auto error = ValidateEntryName(name); error.has_value())
                                                     return error;
                                                 if (!CreateSceneEntry(name))
                                                     return String("Scene already exists.");
                                                 return std::nullopt;
                                             });
            }});
            actions.push_back({"New Material", [this, popup_pos]()
            {
                EditorPopup::ShowTextInputAt(popup_pos, "Create Material",
                                             MakeUniqueEntryName(_current_path.wstring(), "NewMaterial", L".alasset", false),
                                             [this](const String &input) -> std::optional<String>
                                             {
                                                 const String name = TrimNameCopy(input);
                                                 if (auto error = ValidateEntryName(name); error.has_value())
                                                     return error;
                                                 if (!CreateMaterialEntry(name))
                                                     return String("Material already exists.");
                                                 return std::nullopt;
                                             });
            }});
            actions.push_back({"Refresh", [this]() { _is_dirty = true; }});
            EditorPopup::ShowActionMenuAt(popup_pos, actions);
        }

        void AssetBrowser::ShowFolderContextMenu(const WString &folder_sys_path, Vector2f popup_pos)
        {
            const String folder_name = fs::path(folder_sys_path).filename().string();
            const auto create_folder_in_target = [this, folder_sys_path](const String &name) -> bool
            {
                const fs::path previous_path = _current_path;
                _current_path = folder_sys_path;
                const bool created = CreateFolderEntry(name);
                _current_path = previous_path;
                return created;
            };
            const auto create_scene_in_target = [this, folder_sys_path](const String &name) -> bool
            {
                const fs::path previous_path = _current_path;
                _current_path = folder_sys_path;
                const bool created = CreateSceneEntry(name);
                _current_path = previous_path;
                return created;
            };
            const auto create_material_in_target = [this, folder_sys_path](const String &name) -> bool
            {
                const fs::path previous_path = _current_path;
                _current_path = folder_sys_path;
                const bool created = CreateMaterialEntry(name);
                _current_path = previous_path;
                return created;
            };
            Vector<PopupMenuAction> actions;
            actions.push_back({"Open", [this, folder_sys_path]()
            {
                _current_path = folder_sys_path;
                _is_dirty = true;
            }});
            actions.push_back({"Rename", [this, folder_name, folder_sys_path, popup_pos]()
            {
                EditorPopup::ShowTextInputAt(popup_pos, "Rename Folder", folder_name,
                                             [this, folder_sys_path](const String &input) -> std::optional<String>
                                             {
                                                 const String name = TrimNameCopy(input);
                                                 if (auto error = ValidateEntryName(name); error.has_value())
                                                     return error;
                                                 if (!RenameFolderEntry(folder_sys_path, name))
                                                     return String("Folder rename failed.");
                                                 return std::nullopt;
                                             });
            }});
            actions.push_back({"Delete", [this, folder_name, folder_sys_path, popup_pos]()
            {
                EditorPopup::ShowConfirmAt(popup_pos, std::format("Delete folder \"{}\"?", folder_name),
                                           [this, folder_sys_path]() { DeleteFolderEntry(folder_sys_path); });
            }, true});
            actions.push_back({"New Folder", [this, popup_pos, folder_sys_path, create_folder_in_target]()
            {
                EditorPopup::ShowTextInputAt(popup_pos, "Create Folder",
                                             MakeUniqueEntryName(folder_sys_path, "NewFolder", L"", true),
                                             [create_folder_in_target](const String &input) -> std::optional<String>
                                             {
                                                 const String name = TrimNameCopy(input);
                                                 if (auto error = ValidateEntryName(name); error.has_value())
                                                     return error;
                                                 if (!create_folder_in_target(name))
                                                     return String("Folder already exists.");
                                                 return std::nullopt;
                                             });
            }});
            actions.push_back({"New Scene", [this, popup_pos, folder_sys_path, create_scene_in_target]()
            {
                EditorPopup::ShowTextInputAt(popup_pos, "Create Scene",
                                             MakeUniqueEntryName(folder_sys_path, "NewScene", L".almap", false),
                                             [create_scene_in_target](const String &input) -> std::optional<String>
                                             {
                                                 const String name = TrimNameCopy(input);
                                                 if (auto error = ValidateEntryName(name); error.has_value())
                                                     return error;
                                                 if (!create_scene_in_target(name))
                                                     return String("Scene already exists.");
                                                 return std::nullopt;
                                             });
            }});
            actions.push_back({"New Material", [this, popup_pos, folder_sys_path, create_material_in_target]()
            {
                EditorPopup::ShowTextInputAt(popup_pos, "Create Material",
                                             MakeUniqueEntryName(folder_sys_path, "NewMaterial", L".alasset", false),
                                             [create_material_in_target](const String &input) -> std::optional<String>
                                             {
                                                 const String name = TrimNameCopy(input);
                                                 if (auto error = ValidateEntryName(name); error.has_value())
                                                     return error;
                                                 if (!create_material_in_target(name))
                                                     return String("Material already exists.");
                                                 return std::nullopt;
                                             });
            }});
            EditorPopup::ShowActionMenuAt(popup_pos, actions);
        }

        void AssetBrowser::ShowAssetContextMenu(Asset *asset, Vector2f popup_pos)
        {
            if (asset == nullptr)
                return;

            const String asset_name = asset->Name();
            Vector<PopupMenuAction> actions;
            if (asset->_asset_type == StaticClass<SceneManagement::Scene>() || asset->_asset_type == StaticClass<Render::Mesh>())
            {
                actions.push_back({"Open", [this, asset]() { OpenAsset(asset); }});
            }
            actions.push_back({"Rename", [this, asset_name, asset, popup_pos]()
            {
                EditorPopup::ShowTextInputAt(popup_pos, "Rename Asset", asset_name,
                                             [this, asset](const String &input) -> std::optional<String>
                                             {
                                                 const String name = TrimNameCopy(input);
                                                 if (auto error = ValidateEntryName(name); error.has_value())
                                                     return error;
                                                 if (!RenameAssetEntry(asset, name))
                                                     return String("Asset rename failed.");
                                                 return std::nullopt;
                                             });
            }});
            actions.push_back({"Delete", [this, asset_name, asset, popup_pos]()
            {
                EditorPopup::ShowConfirmAt(popup_pos, std::format("Delete asset \"{}\"?", asset_name),
                                           [this, asset]() { DeleteAssetEntry(asset); });
            }, true});
            EditorPopup::ShowActionMenuAt(popup_pos, actions);
        }

        bool AssetBrowser::RenameAssetEntry(Asset *asset, const String &new_name)
        {
            if (asset == nullptr)
                return false;

            const String name = TrimNameCopy(new_name);
            if (name.empty())
                return false;
            if (PathUtils::GetFileName(asset->_asset_path) == ToWChar(name.c_str()))
                return true;

            const WString old_asset_path = asset->_asset_path;
            const WString old_sys_path = ResourceMgr::GetResSysPath(old_asset_path);
            const WString new_asset_path = PathUtils::RenameFile(old_asset_path, ToWChar(name.c_str()));
            const WString new_sys_path = ResourceMgr::GetResSysPath(new_asset_path);
            if (g_pResourceMgr->GetAsset(new_asset_path) != nullptr || fs::exists(new_sys_path))
                return false;

            std::error_code rename_error;
            fs::rename(old_sys_path, new_sys_path, rename_error);
            if (rename_error)
            {
                LOG_WARNING("AssetBrowser: rename file failed, {}", rename_error.message());
                return false;
            }
            if (!g_pResourceMgr->RenameAsset(asset, ToWChar(name.c_str())))
            {
                std::error_code rollback_error;
                fs::rename(new_sys_path, old_sys_path, rollback_error);
                if (rollback_error)
                    LOG_WARNING("AssetBrowser: rename rollback failed, {}", rollback_error.message());
                return false;
            }

            if (asset->_p_obj)
                asset->_p_obj->Name(name);
            RewriteAssetHeaderName(new_sys_path, name);
            g_pResourceMgr->SaveAllUnsavedAssets();
            _is_dirty = true;
            return true;
        }

        bool AssetBrowser::RenameFolderEntry(const WString &folder_sys_path, const String &new_name)
        {
            const String name = TrimNameCopy(new_name);
            if (name.empty())
                return false;

            const fs::path old_path(folder_sys_path);
            if (!fs::exists(old_path) || !fs::is_directory(old_path))
                return false;
            if (old_path.filename().string() == name)
                return true;

            const fs::path new_path = old_path.parent_path() / fs::path(ToWChar(name.c_str()));
            if (fs::exists(new_path))
                return false;

            const WString old_dir_asset_path = NormalizePathWithoutTrailingSlash(PathUtils::ExtractAssetPath(old_path.wstring()));
            const WString new_dir_asset_path = NormalizePathWithoutTrailingSlash(PathUtils::ExtractAssetPath(new_path.wstring()));
            auto nested_assets = CollectAssetsUnderDirectory(old_dir_asset_path);

            std::error_code rename_error;
            fs::rename(old_path, new_path, rename_error);
            if (rename_error)
            {
                LOG_WARNING("AssetBrowser: rename folder failed, {}", rename_error.message());
                return false;
            }

            const WString old_prefix = NormalizeDirectoryPath(old_dir_asset_path);
            for (auto *asset: nested_assets)
            {
                WString asset_path = NormalizePathWithoutTrailingSlash(asset->_asset_path);
                if (asset_path.compare(0, old_prefix.size(), old_prefix) != 0)
                    continue;
                WString suffix = asset_path.substr(old_prefix.size());
                WString new_asset_path = AppendChildAssetPath(new_dir_asset_path, suffix);
                if (!g_pResourceMgr->MoveAsset(asset, new_asset_path))
                    LOG_WARNING(L"AssetBrowser: move asset {} to {} failed after folder rename.", asset->_asset_path, new_asset_path);
            }

            g_pResourceMgr->SaveAllUnsavedAssets();
            _is_dirty = true;
            return true;
        }

        void AssetBrowser::DeleteAssetEntry(Asset *asset)
        {
            if (asset == nullptr)
                return;

            const WString asset_sys_path = ResourceMgr::GetResSysPath(asset->_asset_path);
            std::error_code remove_error;
            const bool removed = fs::remove(asset_sys_path, remove_error);
            if (remove_error || !removed)
            {
                LOG_WARNING("AssetBrowser: delete asset file failed, {}", remove_error.message());
                return;
            }

            g_pResourceMgr->DeleteAsset(asset);
            g_pResourceMgr->Tick(0.0f);
            g_pResourceMgr->SaveAllUnsavedAssets();
            _is_dirty = true;
        }

        void AssetBrowser::DeleteFolderEntry(const WString &folder_sys_path)
        {
            const fs::path folder_path(folder_sys_path);
            if (!fs::exists(folder_path) || !fs::is_directory(folder_path))
                return;

            const WString dir_asset_path = NormalizePathWithoutTrailingSlash(PathUtils::ExtractAssetPath(folder_sys_path));
            auto assets_to_delete = CollectAssetsUnderDirectory(dir_asset_path);

            std::error_code remove_error;
            fs::remove_all(folder_path, remove_error);
            if (remove_error)
            {
                LOG_WARNING("AssetBrowser: delete folder failed, {}", remove_error.message());
                return;
            }

            for (auto *asset: assets_to_delete)
                g_pResourceMgr->DeleteAsset(asset);
            g_pResourceMgr->Tick(0.0f);
            g_pResourceMgr->SaveAllUnsavedAssets();
            _is_dirty = true;
        }

        bool AssetBrowser::CreateFolderEntry(const String &name)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            const fs::path new_folder_path = _current_path / fs::path(ToWChar(trimmed_name.c_str()));
            if (fs::exists(new_folder_path))
                return false;

            FileManager::CreateDirectory(new_folder_path.wstring());
            _is_dirty = true;
            return fs::exists(new_folder_path);
        }

        bool AssetBrowser::CreateSceneEntry(const String &name)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            const WString asset_path = BuildCurrentAssetPath(ToWChar(trimmed_name.c_str()) + WString(L".almap"));
            if (g_pResourceMgr->GetAsset(asset_path) != nullptr || fs::exists(ResourceMgr::GetResSysPath(asset_path)))
                return false;

            auto scene = SceneManagement::SceneMgr::Get().Create(trimmed_name);
            if (!scene)
                return false;

            g_pResourceMgr->CreateAsset(asset_path, scene);
            g_pResourceMgr->SaveAllUnsavedAssets();
            _is_dirty = true;
            return true;
        }

        bool AssetBrowser::CreateMaterialEntry(const String &name)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            const WString asset_path = BuildCurrentAssetPath(ToWChar(trimmed_name.c_str()) + WString(L".alasset"));
            if (g_pResourceMgr->GetAsset(asset_path) != nullptr || fs::exists(ResourceMgr::GetResSysPath(asset_path)))
                return false;

            auto shader = Render::Shader::s_p_defered_standart_lit.lock();
            if (!shader)
            {
                shader = g_pResourceMgr->Load<Render::Shader>(L"Shaders/defered_standard_lit.alasset");
                Render::Shader::s_p_defered_standart_lit = shader;
            }
            if (!shader)
                return false;

            auto material = MakeRef<Render::StandardMaterial>(trimmed_name);
            g_pResourceMgr->CreateAsset(asset_path, material);
            g_pResourceMgr->SaveAllUnsavedAssets();
            _is_dirty = true;
            return true;
        }

        WString AssetBrowser::CurrentAssetDirectoryPath() const
        {
            const WString current_path = NormalizePathWithoutTrailingSlash(_current_path.wstring());
            const WString root_path = NormalizePathWithoutTrailingSlash(g_pResourceMgr->EngineResRootPath());
            if (current_path == root_path)
                return L"";
            return NormalizePathWithoutTrailingSlash(PathUtils::ExtractAssetPath(current_path));
        }

        WString AssetBrowser::BuildCurrentAssetPath(const WString &file_name) const
        {
            return AppendChildAssetPath(CurrentAssetDirectoryPath(), file_name);
        }

        Vector<Asset *> AssetBrowser::CollectAssetsUnderDirectory(const WString &directory_asset_path) const
        {
            Vector<Asset *> assets;
            const WString normalized_dir = NormalizePathWithoutTrailingSlash(directory_asset_path);
            const WString normalized_prefix = NormalizeDirectoryPath(normalized_dir);
            for (auto it = g_pResourceMgr->Begin(); it != g_pResourceMgr->End(); ++it)
            {
                Asset *asset = it->second.get();
                WString asset_path = NormalizePathWithoutTrailingSlash(asset->_asset_path);
                if (asset_path == normalized_dir || asset_path.compare(0, normalized_prefix.size(), normalized_prefix) == 0)
                    assets.push_back(asset);
            }
            return assets;
        }

        String AssetBrowser::MakeUniqueEntryName(const WString &directory_sys_path, const String &base_name, const WString &extension, bool is_directory) const
        {
            const String fallback_name = is_directory ? "NewFolder" : "NewAsset";
            const String seed = TrimNameCopy(base_name).empty() ? fallback_name : TrimNameCopy(base_name);
            String candidate = seed;
            u32 suffix = 1u;
            while (true)
            {
                WString file_name = ToWChar(candidate.c_str());
                if (!is_directory)
                    file_name += extension;
                const fs::path candidate_path = fs::path(directory_sys_path) / fs::path(file_name);
                if (!fs::exists(candidate_path))
                    return candidate;
                candidate = std::format("{}_{}", seed, suffix++);
            }
        }
    }// namespace Editor
}// namespace Ailu