#include "Widgets/AssetBrowserOperations.h"

#include "Assets/Asset.h"
#include "Widgets/AssetBrowserContent.h"

#include "Assets/PrefabAsset.h"
#include "Assets/ScriptAsset.h"
#include "Assets/WidgetAsset.h"
#include "Animation/AnimationControllerAsset.h"
#include "Animation/Clip.h"
#include "Common/EditorPopup.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "Graph/GraphAsset.h"
#include "Graph/GraphDocument.h"
#include "Input/InputActionAsset.h"
#include "Render/2D/Sprite.h"
#include "Render/2D/SpriteAtlas.h"
#include "Render/Material.h"
#include "Render/Shader.h"
#include "Scene/PrefabSystem.h"
#include "Scene/Scene.h"
#include "UI/Basic.h"
#include "UI/Container.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            String TrimNameCopyInternal(const String &value)
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

            bool RewriteAssetHeaderName(const WString &sys_path, const String &new_name)
            {
                WString content;
                if (!FileManager::ReadFile(sys_path, content))
                    return false;

                auto lines = StringUtils::Split(content, L"\n");
                const WString key = L"\"_asset_name\"";
                const WString value = ToWChar(new_name.c_str());
                bool replaced = false;
                for (auto &line: lines)
                {
                    const size_t key_pos = line.find(key);
                    if (key_pos == WString::npos)
                        continue;
                    const size_t colon_pos = line.find(L':', key_pos + key.size());
                    const size_t value_begin = colon_pos == WString::npos ? WString::npos : line.find(L'"', colon_pos + 1u);
                    const size_t value_end = value_begin == WString::npos ? WString::npos : line.find(L'"', value_begin + 1u);
                    if (value_end == WString::npos)
                        continue;
                    line.replace(value_begin + 1u, value_end - value_begin - 1u, value);
                    replaced = true;
                    break;
                }
                if (!replaced)
                    return false;

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

            bool RewriteAssetHeaderGuid(const WString &sys_path, const Guid &guid)
            {
                WString content;
                if (!FileManager::ReadFile(sys_path, content))
                    return false;

                auto lines = StringUtils::Split(content, L"\n");
                const WString key = L"\"_guid\"";
                const WString value = ToWChar(guid.ToString().c_str());
                bool replaced = false;
                for (auto &line: lines)
                {
                    const size_t key_pos = line.find(key);
                    if (key_pos == WString::npos)
                        continue;
                    const size_t colon_pos = line.find(L':', key_pos + key.size());
                    const size_t value_begin = colon_pos == WString::npos ? WString::npos : line.find(L'"', colon_pos + 1u);
                    const size_t value_end = value_begin == WString::npos ? WString::npos : line.find(L'"', value_begin + 1u);
                    if (value_end == WString::npos)
                        continue;
                    line.replace(value_begin + 1u, value_end - value_begin - 1u, value);
                    replaced = true;
                    break;
                }
                if (!replaced)
                    return false;

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

            fs::path MakeUniqueTransferPath(const fs::path &directory, const fs::path &source_path)
            {
                AssetBrowserContent content;
                const WString asset_directory = content.GetAssetDirectory(directory);
                const String stem = source_path.stem().string();
                const WString extension = source_path.extension().wstring();
                auto is_available = [&](const String &name)
                {
                    const fs::path candidate = directory / fs::path(ToWChar(name.c_str()) + extension);
                    const WString logical_path = AppendChildAssetPath(asset_directory, candidate.filename().wstring());
                    return !fs::exists(candidate) && ResourceMgr::Get().GetAsset(logical_path) == nullptr;
                };

                if (is_available(stem))
                    return directory / fs::path(ToWChar(stem.c_str()) + extension);

                for (u32 suffix = 1u;; ++suffix)
                {
                    const String candidate_name = std::format("{}({})", stem, suffix);
                    if (is_available(candidate_name))
                        return directory / fs::path(ToWChar(candidate_name.c_str()) + extension);
                }
            }

            WString GetAssetPathForSystemPath(const fs::path &path)
            {
                AssetBrowserContent content;
                return ResourceMgr::NormalizeAssetPath(path.wstring(), content.GetDomain(path));
            }

            Ref<Render::Shader> EnsureDefaultMaterialShader()
            {
                auto shader = Render::Shader::s_p_defered_standart_lit.lock();
                if (!shader)
                {
                    shader = ResourceMgr::Get().Load<Render::Shader>(L"Shaders/hlsl/defered_standard_lit.alasset");
                    Render::Shader::s_p_defered_standart_lit = shader;
                }
                return shader;
            }
        }// namespace

        String TrimNameCopy(const String &value)
        {
            return TrimNameCopyInternal(value);
        }

        std::optional<String> ValidateEntryName(const String &value)
        {
            const String trimmed = TrimNameCopyInternal(value);
            if (trimmed.empty())
                return String("Name cannot be empty.");
            if (trimmed == "." || trimmed == "..")
                return String("Name is reserved.");
            if (HasInvalidFileNameChars(trimmed))
                return String("Name contains invalid characters.");
            return std::nullopt;
        }

        Vector<Ref<Render::Shader>> CollectMaterialShaders()
        {
            Vector<Ref<Render::Shader>> shaders;
            auto default_shader = EnsureDefaultMaterialShader();
            if (default_shader)
                shaders.push_back(default_shader);

            for (auto it = ResourceMgr::Get().ResourceBegin<Render::Shader>(); it != ResourceMgr::Get().ResourceEnd<Render::Shader>(); ++it)
            {
                auto shader = ResourceMgr::IterToRefPtr<Render::Shader>(it);
                if (!shader)
                    continue;
                const bool already_added = std::any_of(shaders.begin(), shaders.end(), [shader](const Ref<Render::Shader> &item)
                {
                    return item.get() == shader.get();
                });
                if (!already_added)
                    shaders.push_back(shader);
            }
            return shaders;
        }

        void ShowCreateMaterialDialog(Vector2f popup_pos, const fs::path &target_sys_path)
        {
            AssetBrowserOperations operations;
            auto material_name = std::make_shared<String>(operations.MakeUniqueEntryName(target_sys_path, "NewMaterial", L".alasset", false));
            auto shaders = std::make_shared<Vector<Ref<Render::Shader>>>(CollectMaterialShaders());
            auto shader_names = std::make_shared<Vector<String>>();
            shader_names->reserve(shaders->size());
            for (const auto &shader: *shaders)
                shader_names->push_back(shader ? shader->Name() : String("null shader"));
            if (shader_names->empty())
                shader_names->push_back("Missing Shader");

            auto selected_shader_index = std::make_shared<i32>(shaders->empty() ? -1 : 0);
            UI::InputBlock *name_input = nullptr;
            UI::Dropdown *shader_dropdown = nullptr;

            EditorPopup::ShowDialogAt(popup_pos, "AssetBrowserMaterialCreatePrompt", "Create Material", {300.0f, 134.0f},
                                      [material_name, shader_names, selected_shader_index, &name_input, &shader_dropdown](UI::VerticalBox *content, UI::Text *)
                                      {
                                          UI::HorizontalBox *value_box = nullptr;
                                          auto *name_row = EditorPopup::AddPropertyRow(content, "name:", &value_box);
                                          name_row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({280.0f, 24.0f});
                                          name_input = value_box->AddChild<UI::InputBlock>();
                                          name_input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({0.0f, 24.0f});
                                          name_input->_on_content_changed += [material_name](String value)
                                          {
                                              *material_name = std::move(value);
                                          };
                                          name_input->SetContent(*material_name, false);

                                          auto *shader_row = EditorPopup::AddPropertyRow(content, "shader:", &value_box);
                                          shader_row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({280.0f, 24.0f});
                                          shader_dropdown = value_box->AddChild<UI::Dropdown>(*shader_names);
                                          shader_dropdown->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({0.0f, 24.0f});
                                          shader_dropdown->_on_selected_changed += [selected_shader_index](i32 index)
                                          {
                                              *selected_shader_index = index;
                                          };
                                          shader_dropdown->SetSelectedIndex(*selected_shader_index);
                                      },
                                      {
                                              {"OK", [target_sys_path, material_name, shaders, selected_shader_index, name_input]() -> std::optional<String>
                                               {
                                                   const String name = TrimNameCopy(*material_name);
                                                   if (auto error = ValidateEntryName(name); error.has_value())
                                                   {
                                                       if (name_input != nullptr)
                                                           name_input->RequestFocus();
                                                       return error;
                                                   }
                                                   if (*selected_shader_index < 0 || *selected_shader_index >= static_cast<i32>(shaders->size()) || (*shaders)[*selected_shader_index] == nullptr)
                                                       return String("Shader is required.");

                                                   if (!CreateMaterialAsset(target_sys_path, name, (*shaders)[*selected_shader_index].get()))
                                                       return String("Material already exists.");
                                                   return std::nullopt;
                                               }},
                                              {"Cancel", []() -> std::optional<String> { return std::nullopt; }}
                                      },
                                      [name_input]()
                                      {
                                          if (name_input != nullptr)
                                              name_input->RequestFocus();
                                      });
        }

        bool CreateSceneAsset(const fs::path &directory, const String &name)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            AssetBrowserContent content;
            const WString asset_path = AppendChildAssetPath(content.GetAssetDirectory(directory), ToWChar(trimmed_name.c_str()) + WString(L".almap"));
            if (ResourceMgr::Get().GetAsset(asset_path) != nullptr || fs::exists(ResourceMgr::GetResSysPath(asset_path)))
                return false;

            auto scene = SceneManagement::SceneMgr::Get().Create(trimmed_name);
            if (!scene)
                return false;

            ResourceMgr::Get().CreateAsset(asset_path, scene);
            ResourceMgr::Get().SaveAllUnsavedAssets();
            return true;
        }

        bool CreateSpriteAsset(const fs::path &directory, const String &name)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            AssetBrowserContent content;
            const WString asset_path = AppendChildAssetPath(content.GetAssetDirectory(directory), ToWChar(trimmed_name.c_str()) + WString(L".alasset"));
            if (ResourceMgr::Get().GetAsset(asset_path) != nullptr || fs::exists(ResourceMgr::GetResSysPath(asset_path)))
                return false;

            auto sprite = MakeRef<Render::Sprite>(trimmed_name);
            if (ResourceMgr::Get().CreateAsset(asset_path, sprite, false) == nullptr)
                return false;
            ResourceMgr::Get().SaveAllUnsavedAssets();
            return true;
        }

        bool CreateSpriteAtlasAsset(const fs::path &directory, const String &name)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            AssetBrowserContent content;
            const WString asset_path = AppendChildAssetPath(content.GetAssetDirectory(directory),
                                                            ToWChar(trimmed_name.c_str()) + WString(L".alasset"));
            if (ResourceMgr::Get().GetAsset(asset_path) != nullptr || fs::exists(ResourceMgr::GetResSysPath(asset_path)))
                return false;

            auto atlas = MakeRef<Render::SpriteAtlas>(trimmed_name);
            if (ResourceMgr::Get().CreateAsset(asset_path, atlas, false) == nullptr)
                return false;
            ResourceMgr::Get().SaveAllUnsavedAssets();
            return true;
        }

        bool CreateMaterialAsset(const fs::path &directory, const String &name, Render::Shader *shader)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            AssetBrowserContent content;
            const WString asset_path = AppendChildAssetPath(content.GetAssetDirectory(directory), ToWChar(trimmed_name.c_str()) + WString(L".alasset"));
            if (ResourceMgr::Get().GetAsset(asset_path) != nullptr || fs::exists(ResourceMgr::GetResSysPath(asset_path)))
                return false;

            if (!shader)
                return false;

            Ref<Render::Material> material = nullptr;
            if (shader == Render::Shader::s_p_defered_standart_lit.lock().get() || shader->Name() == "defered_standard_lit")
                material = MakeRef<Render::StandardMaterial>(trimmed_name);
            else
                material = MakeRef<Render::Material>(shader, trimmed_name);
            ResourceMgr::Get().CreateAsset(asset_path, material);
            ResourceMgr::Get().SaveAllUnsavedAssets();
            return true;
        }

        bool CreatePrefabAsset(ECS::Entity entity, const fs::path &target_directory)
        {
            auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
            if (scene == nullptr || !scene->IsValidEntity(entity) || !fs::is_directory(target_directory))
                return false;

            const auto *tag = scene->GetRegister().GetComponent<ECS::TagComponent>(entity);
            const String prefab_name = TrimNameCopy(tag != nullptr ? tag->_name : String{});
            if (prefab_name.empty())
                return false;

            const WString file_name = ToWChar(prefab_name.c_str()) + WString(L".alasset");
            AssetBrowserContent content;
            const WString asset_path = AppendChildAssetPath(content.GetAssetDirectory(target_directory), file_name);
            if (ResourceMgr::Get().GetAsset(asset_path) != nullptr || fs::exists(ResourceMgr::GetResSysPath(asset_path)))
            {
                LOG_WARNING(L"AssetBrowser: Prefab asset {} already exists.", asset_path);
                return false;
            }

            Ref<PrefabAssetDocument> prefab = SceneManagement::PrefabSystem::CreatePrefabDocument(*scene, entity);
            if (prefab == nullptr)
                return false;

            prefab->Name(prefab_name);
            if (ResourceMgr::Get().CreateAsset(asset_path, prefab, false) == nullptr)
                return false;
            ResourceMgr::Get().SaveAllUnsavedAssets();
            return true;
        }

        bool CreateInputActionAsset(const fs::path &directory, const String &name)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            AssetBrowserContent content;
            const WString asset_path = AppendChildAssetPath(content.GetAssetDirectory(directory), ToWChar(trimmed_name.c_str()) + WString(L".alasset"));
            if (ResourceMgr::Get().GetAsset(asset_path) != nullptr || fs::exists(ResourceMgr::GetResSysPath(asset_path)))
                return false;

            auto input_asset = MakeRef<InputActionAsset>(trimmed_name);
            ResourceMgr::Get().CreateAsset(asset_path, input_asset);
            ResourceMgr::Get().SaveAllUnsavedAssets();
            return true;
        }

        bool CreateAnimationClipAsset(const fs::path &directory, const String &name)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            AssetBrowserContent content;
            const WString asset_path = AppendChildAssetPath(content.GetAssetDirectory(directory),
                                                            ToWChar(trimmed_name.c_str()) + WString(L".alasset"));
            if (ResourceMgr::Get().GetAsset(asset_path) != nullptr || fs::exists(ResourceMgr::GetResSysPath(asset_path)))
                return false;

            auto clip = MakeRef<AnimationClip>();
            clip->Name(trimmed_name);
            if (ResourceMgr::Get().CreateAsset(asset_path, clip, false) == nullptr)
                return false;
            ResourceMgr::Get().SaveAllUnsavedAssets();
            return true;
        }

        bool CreateAnimationControllerAsset(const fs::path &directory, const String &name)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            AssetBrowserContent content;
            const WString asset_path = AppendChildAssetPath(content.GetAssetDirectory(directory),
                                                            ToWChar(trimmed_name.c_str()) + WString(L".alasset"));
            if (ResourceMgr::Get().GetAsset(asset_path) != nullptr || fs::exists(ResourceMgr::GetResSysPath(asset_path)))
                return false;

            auto controller = MakeRef<AnimationControllerAsset>(trimmed_name);
            if (ResourceMgr::Get().CreateAsset(asset_path, controller, false) == nullptr)
                return false;
            ResourceMgr::Get().SaveAllUnsavedAssets();
            return true;
        }

        bool CreateWidgetAsset(const fs::path &directory, const String &name)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            AssetBrowserContent content;
            const WString asset_path = AppendChildAssetPath(content.GetAssetDirectory(directory), ToWChar(trimmed_name.c_str()) + WString(L".alasset"));
            if (ResourceMgr::Get().GetAsset(asset_path) != nullptr || fs::exists(ResourceMgr::GetResSysPath(asset_path)))
                return false;

            auto widget_asset = MakeRef<WidgetAsset>(trimmed_name);
            if (ResourceMgr::Get().CreateAsset(asset_path, widget_asset, false) == nullptr)
                return false;
            ResourceMgr::Get().SaveAllUnsavedAssets();
            return true;
        }

        bool CreateFlowGraphAsset(const fs::path &directory, const String &name)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            AssetBrowserContent content;
            const WString asset_path = AppendChildAssetPath(content.GetAssetDirectory(directory), ToWChar(trimmed_name.c_str()) + WString(L".alasset"));
            if (ResourceMgr::Get().GetAsset(asset_path) != nullptr || fs::exists(ResourceMgr::GetResSysPath(asset_path)))
                return false;

            auto graph = MakeRef<GraphAsset>(trimmed_name);
            graph->SchemaType("FlowGraphSchema");
            GraphDocument document;
            if (!document.Open(graph.get()))
                return false;
            document.AddNode("Flow.Entry", {80.0f, 120.0f});
            document.Apply();
            ResourceMgr::Get().CreateAsset(asset_path, graph);
            ResourceMgr::Get().SaveAllUnsavedAssets();
            return true;
        }

        bool CreateScriptAsset(const fs::path &directory, const String &name)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            AssetBrowserContent content;
            const WString lua_file_name = ToWChar(trimmed_name.c_str()) + WString(L".lua");
            const WString asset_file_name = ToWChar(trimmed_name.c_str()) + WString(L".alasset");
            const WString lua_asset_path = AppendChildAssetPath(content.GetAssetDirectory(directory), lua_file_name);
            const WString asset_path = AppendChildAssetPath(content.GetAssetDirectory(directory), asset_file_name);
            const WString lua_sys_path = ResourceMgr::GetResSysPath(lua_asset_path);
            if (ResourceMgr::Get().GetAsset(asset_path) != nullptr || fs::exists(ResourceMgr::GetResSysPath(asset_path)) || fs::exists(lua_sys_path))
                return false;

            const String script_template = std::format(R"(---@class {} : AiluScript
local script = {{}}

function script:on_create()
end

function script:on_enable()
end

function script:on_disable()
end

function script:on_fixed_update(fixed_delta_time)
end

function script:on_update(delta_time)
end

function script:on_late_update(delta_time)
end

function script:on_destroy()
end

function script:on_reload()
end

return script
)", trimmed_name);
            if (!FileManager::WriteFile(lua_sys_path, false, script_template))
                return false;
            auto script_asset = MakeRef<ScriptAsset>(ToChar(lua_sys_path));
            script_asset->Name(trimmed_name);
            if (ResourceMgr::Get().CreateAsset(asset_path, script_asset) == nullptr)
                return false;
            ResourceMgr::Get().SaveAllUnsavedAssets();
            return true;
        }

        bool AssetBrowserOperations::RenameAsset(Asset *asset, const String &new_name)
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
            if (ResourceMgr::Get().GetAsset(new_asset_path) != nullptr || fs::exists(new_sys_path))
                return false;

            std::error_code rename_error;
            fs::rename(old_sys_path, new_sys_path, rename_error);
            if (rename_error)
            {
                LOG_WARNING("AssetBrowser: rename file failed, {}", rename_error.message());
                return false;
            }
            if (!ResourceMgr::Get().RenameAsset(asset, ToWChar(name.c_str())))
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
            ResourceMgr::Get().SaveAllUnsavedAssets();
            return true;
        }

        bool AssetBrowserOperations::RenameFolder(const fs::path &folder, const String &new_name)
        {
            const String name = TrimNameCopy(new_name);
            if (name.empty())
                return false;

            const fs::path old_path = folder;
            if (!fs::exists(old_path) || !fs::is_directory(old_path))
                return false;
            if (old_path.filename().string() == name)
                return true;

            const fs::path new_path = old_path.parent_path() / fs::path(ToWChar(name.c_str()));
            if (fs::exists(new_path))
                return false;

            AssetBrowserContent content;
            const WString old_dir_asset_path = ResourceMgr::NormalizeAssetPath(old_path.wstring(), content.GetDomain(old_path));
            const WString new_dir_asset_path = ResourceMgr::NormalizeAssetPath(new_path.wstring(), content.GetDomain(new_path));
            auto nested_assets = content.CollectAssetsUnderDirectory(old_dir_asset_path);

            std::error_code rename_error;
            fs::rename(old_path, new_path, rename_error);
            if (rename_error)
            {
                LOG_WARNING("AssetBrowser: rename folder failed, {}", rename_error.message());
                return false;
            }

            const WString old_prefix = NormalizeLogicalDirectoryPath(old_dir_asset_path);
            for (auto *asset: nested_assets)
            {
                WString asset_path = NormalizeLogicalPathWithoutTrailingSlash(asset->_asset_path);
                if (asset_path.compare(0, old_prefix.size(), old_prefix) != 0)
                    continue;
                WString suffix = asset_path.substr(old_prefix.size());
                WString new_asset_path = AppendChildAssetPath(new_dir_asset_path, suffix);
                if (!ResourceMgr::Get().MoveAsset(asset, new_asset_path))
                    LOG_WARNING(L"AssetBrowser: move asset {} to {} failed after folder rename.", asset->_asset_path, new_asset_path);
            }

            ResourceMgr::Get().SaveAllUnsavedAssets();
            return true;
        }

        bool AssetBrowserOperations::DeleteAsset(Asset *asset)
        {
            if (asset == nullptr)
                return false;

            const WString asset_sys_path = ResourceMgr::GetResSysPath(asset->_asset_path);
            std::error_code remove_error;
            const bool removed = fs::remove(asset_sys_path, remove_error);
            if (remove_error || !removed)
            {
                LOG_WARNING("AssetBrowser: delete asset file failed, {}", remove_error.message());
                return false;
            }

            ResourceMgr::Get().DeleteAsset(asset);
            ResourceMgr::Get().Tick(0.0f);
            ResourceMgr::Get().SaveAllUnsavedAssets();
            return true;
        }

        bool AssetBrowserOperations::DeleteFolder(const fs::path &folder)
        {
            const fs::path folder_path = folder;
            if (!fs::exists(folder_path) || !fs::is_directory(folder_path))
                return false;

            AssetBrowserContent content;
            const WString dir_asset_path = ResourceMgr::NormalizeAssetPath(folder_path.wstring(), content.GetDomain(folder_path));
            auto assets_to_delete = content.CollectAssetsUnderDirectory(dir_asset_path);

            std::error_code remove_error;
            fs::remove_all(folder_path, remove_error);
            if (remove_error)
            {
                LOG_WARNING("AssetBrowser: delete folder failed, {}", remove_error.message());
                return false;
            }

            for (auto *asset: assets_to_delete)
                ResourceMgr::Get().DeleteAsset(asset);
            ResourceMgr::Get().Tick(0.0f);
            ResourceMgr::Get().SaveAllUnsavedAssets();
            return true;
        }

        bool AssetBrowserOperations::CreateFolder(const fs::path &directory, const String &name)
        {
            const String trimmed_name = TrimNameCopy(name);
            if (trimmed_name.empty())
                return false;

            const fs::path new_folder_path = directory / fs::path(ToWChar(trimmed_name.c_str()));
            if (fs::exists(new_folder_path))
                return false;

            FileManager::CreateDirectory(new_folder_path.wstring());
            return fs::exists(new_folder_path);
        }

        bool AssetBrowserOperations::MoveAssets(const Vector<Asset *> &assets, const fs::path &target_directory)
        {
            if (assets.empty() || !fs::exists(target_directory) || !fs::is_directory(target_directory))
                return false;

            bool moved_any = false;
            for (auto *asset: assets)
            {
                if (asset == nullptr)
                    continue;

                const fs::path source_path(ResourceMgr::GetResSysPath(asset->_asset_path));
                if (!fs::exists(source_path) || !fs::is_regular_file(source_path))
                    continue;
                if (source_path.parent_path() == target_directory)
                {
                    moved_any = true;
                    continue;
                }

                fs::path target_path = MakeUniqueTransferPath(target_directory, source_path);

                const WString target_asset_path = GetAssetPathForSystemPath(target_path);
                std::error_code rename_error;
                fs::rename(source_path, target_path, rename_error);
                if (rename_error)
                {
                    LOG_WARNING("AssetBrowser: move asset failed, {}", rename_error.message());
                    continue;
                }

                if (!ResourceMgr::Get().MoveAsset(asset, target_asset_path))
                {
                    std::error_code rollback_error;
                    fs::rename(target_path, source_path, rollback_error);
                    if (rollback_error)
                        LOG_WARNING("AssetBrowser: move rollback failed, {}", rollback_error.message());
                    continue;
                }
                moved_any = true;
            }

            if (moved_any)
                ResourceMgr::Get().SaveAllUnsavedAssets();
            return moved_any;
        }

        bool AssetBrowserOperations::CopyAssets(const Vector<Asset *> &assets, const fs::path &target_directory)
        {
            if (assets.empty() || !fs::exists(target_directory) || !fs::is_directory(target_directory))
                return false;

            bool copied_any = false;
            for (auto *asset: assets)
            {
                if (asset == nullptr)
                    continue;

                const fs::path source_path(ResourceMgr::GetResSysPath(asset->_asset_path));
                if (!fs::exists(source_path) || !fs::is_regular_file(source_path))
                    continue;

                const fs::path target_path = MakeUniqueTransferPath(target_directory, source_path);
                std::error_code copy_error;
                fs::copy_file(source_path, target_path, fs::copy_options::none, copy_error);
                if (copy_error)
                {
                    LOG_WARNING("AssetBrowser: copy asset failed, {}", copy_error.message());
                    continue;
                }

                const String target_name = target_path.stem().string();
                if (!RewriteAssetHeaderGuid(target_path.wstring(), Guid::Generate()) ||
                    !RewriteAssetHeaderName(target_path.wstring(), target_name))
                {
                    std::error_code remove_error;
                    fs::remove(target_path, remove_error);
                    continue;
                }

                const WString target_asset_path = GetAssetPathForSystemPath(target_path);
                if (ResourceMgr::Get().Load(target_asset_path, nullptr, asset->_asset_type) == nullptr)
                {
                    std::error_code remove_error;
                    fs::remove(target_path, remove_error);
                    continue;
                }
                copied_any = true;
            }

            return copied_any;
        }

        String AssetBrowserOperations::MakeUniqueEntryName(const fs::path &directory, const String &base_name, const WString &extension, bool is_directory) const
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
                const fs::path candidate_path = directory / fs::path(file_name);
                if (!fs::exists(candidate_path))
                    return candidate;
                candidate = std::format("{}_{}", seed, suffix++);
            }
        }
    }// namespace Editor
}// namespace Ailu
