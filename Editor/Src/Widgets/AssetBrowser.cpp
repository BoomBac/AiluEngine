#include "Widgets/AssetBrowser.h"
#include "Framework/Common/ResourceMgr.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIRenderer.h"
#include "UI/DragDrop.h"
#include "Render/AssetPreviewGenerator.h"

namespace Ailu
{
    namespace Editor
    {
        using namespace UI;
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
            slider->_on_value_change += [&](f32 value)
            {
                _icon_size = value;
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
                for (auto &f: e._drop_files)
                {
                    LOG_INFO(L"Drop file {}",(f));
                    MeshImportSetting settings;
                    settings._is_combine_mesh = true;
                    settings._is_import_material = true;
                    g_pResourceMgr->ImportResource(f, _current_path.wstring(), settings);
                    //fs::path dest_path = _current_path / fs::path(f).filename();
                    //fs::copy_file(fs::path(f), dest_path, fs::copy_options::overwrite_existing);
                    //LOG_INFO("Copy file {} to {}", f, dest_path.string());
                }
                _is_dirty = true;
            };
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

            static Vector2f last_size = Vector2f::kZero;
            Vector2f parent_size = _icon_area->GetArrangeRect().zw;
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
                        text->_font_size = 9u;
                        return std::make_tuple(vb, icon, text);
                    };
                    for (auto &dir_it: curdir_it)
                    {
                        if (!dir_it.is_directory())
                            continue;
                        fs::path item_path = dir_it.path();
                        auto [vb, icon,text] = create_icon_group();
                        vb->SlotSize({_icon_size, _icon_size + 20.0f});
                        icon->Name(item_path.filename().string().c_str());
                        text->SetText(item_path.filename().string().c_str());
                        icon->SetTexture(s_folder_icon);
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
                        icon->OnMouseDown() += [this, icon](UI::UIEvent &e)
                        {
                            auto payload = DragPayload{EDragType::kFolder, nullptr};
                            DragDropManager::Get().BeginDrag(payload, "folder");
                        };
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
                        if (asset->_asset_type == EAssetType::kMesh)
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
                        else if (asset->_asset_type == EAssetType::kShader)
                        {
                            icon->SetTexture(s_shader_icon);
                        }
                        else if (asset->_asset_type == EAssetType::kScene)
                        {
                            icon->SetTexture(s_scene_icon);
                        }
                        else if (asset->_asset_type == EAssetType::kTexture2D)
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
                        else if (asset->_asset_type == EAssetType::kMaterial)
                            icon->SetTexture(s_material_icon);
                        else if (asset->_asset_type == EAssetType::kAnimClip)
                            icon->SetTexture(s_animclip_icon);
                        else if (asset->_asset_type == EAssetType::kSkeletonMesh)
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
                            if (asset->_asset_type == EAssetType::kScene)
                            {
                                g_pSceneMgr->OpenScene(asset->_asset_path);
                            }
                            else if (asset->_asset_type == EAssetType::kMesh)
                            {
                                if (asset->_p_obj == nullptr)
                                {
                                    g_pResourceMgr->Load<Mesh>(asset->_asset_path);
                                    _is_dirty = true;
                                }
                            }
                        };
                        _icon_content->AddChild(vb);
                        if (asset->_asset_type == EAssetType::kMesh)
                        {
                            icon->OnMouseDown() += [this, icon, asset](UI::UIEvent &e)
                            {
                                auto payload = DragPayload{EDragType::kMesh, asset};
                                DragDropManager::Get().BeginDrag(payload, "folder");
                            };
                        }
                    }
                }
                _is_dirty = false;
            }
            f32 x = 0.0f, y = 0.0f;
            u32 num_per_row = (u32) (parent_size.x / _icon_size);
            if (num_per_row == 0) num_per_row = 1;
            for (u32 i = 0; i < (u32)_icon_content->GetChildren().size(); i++)
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
            auto r = UI::UIRenderer::Get();
            //if (_hover_item)
            //{
            //    r->PushScissor(_icon_area->GetArrangeRect());
            //    r->DrawQuad(_hover_item->GetArrangeRect(), Colors::kOrange);
            //    r->PopScissor();
            //}
        }
    }// namespace Editor
}// namespace Ailu