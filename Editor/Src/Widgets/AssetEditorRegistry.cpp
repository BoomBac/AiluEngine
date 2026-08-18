#include "Widgets/AssetEditorRegistry.h"

#include "Assets/Asset.h"
#include "Assets/PrefabAsset.h"
#include "Assets/ScriptAsset.h"
#include "Assets/WidgetAsset.h"
#include "Animation/AnimationControllerAsset.h"
#include "Animation/Clip.h"
#include "Audio/AudioClip.h"
#include "Common/Selection.h"
#include "Dock/DockManager.h"
#include "Editors/AudioClipEditor.h"
#include "Editors/AnimationClipEditor.h"
#include "Editors/AnimationControllerEditor.h"
#include "Editors/InputActionAssetEditor.h"
#include "Editors/SpriteAssetEditor.h"
#include "Editors/Widget/WidgetEditor.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "Graph/GraphAsset.h"
#include "Graph/GraphDocument.h"
#include "Graph/GraphEditorWindow.h"
#include "Input/InputActionAsset.h"
#include "Platform/Process.h"
#include "Project/ProjectManager.h"
#include "Render/2D/Sprite.h"
#include "Render/Mesh.h"
#include "Scene/PrefabSystem.h"
#include "Scene/Scene.h"

#include <filesystem>

namespace Ailu
{
    namespace Editor
    {
        AssetEditorRegistry::AssetEditorRegistry()
        {
            RegisterEditor(StaticClass<GraphAsset>(), [](Asset *asset) -> Ref<DockWindow>
            {
                if (asset == nullptr)
                    return nullptr;
                if (asset->_p_obj == nullptr)
                    ResourceMgr::Get().Load<GraphAsset>(asset->_asset_path);
                if (auto *graph = asset->As<GraphAsset>(); graph != nullptr)
                {
                    auto editor = MakeRef<GraphEditorWindow>();
                    editor->Open(graph);
                    return editor;
                }
                return nullptr;
            });

            RegisterEditor(StaticClass<WidgetAsset>(), [](Asset *asset) -> Ref<DockWindow>
            {
                if (asset == nullptr)
                    return nullptr;
                if (asset->_p_obj == nullptr)
                    ResourceMgr::Get().Load<WidgetAsset>(asset->_asset_path);
                auto *widget_asset = asset->As<WidgetAsset>();
                if (widget_asset == nullptr)
                    return nullptr;

                auto editor = MakeRef<WidgetEditor>();
                editor->Open(widget_asset);
                return editor;
            });

            RegisterEditor(StaticClass<Render::Sprite>(), [](Asset *asset) -> Ref<DockWindow>
            {
                if (asset == nullptr)
                    return nullptr;
                if (asset->_p_obj == nullptr)
                    ResourceMgr::Get().Load<Render::Sprite>(asset->_asset_path);
                if (asset->_p_obj == nullptr)
                    return nullptr;

                auto editor = MakeRef<SpriteAssetEditor>();
                editor->Open(asset->As<Render::Sprite>());
                return editor;
            });

            RegisterEditor(StaticClass<AnimationClip>(), [](Asset *asset) -> Ref<DockWindow>
            {
                if (asset == nullptr)
                    return nullptr;
                if (asset->_p_obj == nullptr)
                    ResourceMgr::Get().Load<AnimationClip>(asset->_asset_path);
                auto *clip = asset->As<AnimationClip>();
                if (clip == nullptr)
                    return nullptr;
                auto editor = MakeRef<AnimationClipEditor>();
                editor->Open(clip);
                return editor;
            });

            RegisterEditor(StaticClass<AnimationControllerAsset>(), [](Asset *asset) -> Ref<DockWindow>
            {
                if (asset == nullptr)
                    return nullptr;
                if (asset->_p_obj == nullptr)
                    ResourceMgr::Get().Load<AnimationControllerAsset>(asset->_asset_path);
                auto *controller = asset->As<AnimationControllerAsset>();
                if (controller == nullptr)
                    return nullptr;
                auto editor = MakeRef<AnimationControllerEditor>();
                editor->Open(controller);
                return editor;
            });

            RegisterEditor(StaticClass<InputActionAsset>(), [](Asset *asset) -> Ref<DockWindow>
            {
                if (asset == nullptr)
                    return nullptr;
                if (asset->_p_obj == nullptr)
                    ResourceMgr::Get().Load<InputActionAsset>(asset->_asset_path);
                if (asset->_p_obj == nullptr)
                    return nullptr;

                auto editor = MakeRef<InputActionAssetEditor>();
                editor->Open(asset->As<InputActionAsset>());
                return editor;
            });

            RegisterEditor(StaticClass<AudioClip>(), [](Asset *asset) -> Ref<DockWindow>
            {
                if (asset == nullptr)
                    return nullptr;
                if (asset->_p_obj == nullptr)
                    ResourceMgr::Get().Load<AudioClip>(asset->_asset_path);
                if (asset->_p_obj == nullptr)
                    return nullptr;

                auto editor = MakeRef<AudioClipEditor>();
                editor->Open(asset->As<AudioClip>());
                return editor;
            });

            RegisterOpenHandler(StaticClass<SceneManagement::Scene>(), [](Asset *asset)
            {
                // 切换场景前清理 Selection，避免另一 Scene 中相同运行时句柄被误认为原对象。
                Selection::RemoveSlection();
                SceneManagement::SceneMgr::Get().OpenScene(asset->_asset_path);
            });

            RegisterOpenHandler(PrefabAssetDocument::StaticType(), [](Asset *asset)
            {
                if (asset->_p_obj == nullptr)
                    ResourceMgr::Get().Load<PrefabAssetDocument>(asset->_asset_path);
                auto *prefab = asset->As<PrefabAssetDocument>();
                if (prefab == nullptr)
                    return;

                auto temporary_scene = SceneManagement::SceneMgr::Get().Create(
                        std::format("Prefab Preview - {}", prefab->Name()));
                const auto result = SceneManagement::PrefabSystem::Instantiate(*temporary_scene, *prefab);
                if (result._root == ECS::kInvalidEntity)
                {
                    LOG_ERROR("AssetBrowser: failed to instantiate prefab {}", prefab->Name());
                    return;
                }
                Selection::RemoveSlection();
                SceneManagement::SceneMgr::Get().OpenTemporaryScene(std::move(temporary_scene), asset->GetGuid(), result._root);
                Selection::SetSelection(result._root);
            });

            RegisterOpenHandler(ScriptAsset::StaticType(), [](Asset *asset)
            {
                if (asset->_p_obj == nullptr)
                    ResourceMgr::Get().Load<ScriptAsset>(asset->_asset_path);
                auto *script = asset->As<ScriptAsset>();
                if (script == nullptr)
                    return;
                const WString script_sys_path = ResourceMgr::GetResSysPath(ToWChar(script->SourceFile()));
                if (!fs::exists(script_sys_path))
                {
                    LOG_WARNING(L"AssetBrowser: script source file does not exist, {}", script_sys_path);
                    return;
                }
                WString workspace_path = fs::path(script_sys_path).parent_path().wstring();
                if (ProjectManager::Get().HasOpenedProject())
                    workspace_path = ProjectManager::Get().CurrentProject().RootDirectory();
                ProcessStartInfo psi(L"cmd.exe /c code -r \"" + workspace_path + L"\" \"" + script_sys_path + L"\"");
                auto process = ProcessFactory::Create();
                if (process == nullptr || !process->Start(psi))
                    LOG_ERROR(L"AssetBrowser: failed to open script {} in vscode", script_sys_path);
            });

            RegisterOpenHandler(StaticClass<Render::Mesh>(), [](Asset *asset)
            {
                if (asset->_p_obj == nullptr)
                    ResourceMgr::Get().Load<Render::Mesh>(asset->_asset_path);
            });
        }

        AssetEditorRegistry &AssetEditorRegistry::Get()
        {
            static AssetEditorRegistry s_registry;
            return s_registry;
        }

        void AssetEditorRegistry::RegisterEditor(const Type *asset_type, CreateEditorFunc create_editor)
        {
            if (asset_type == nullptr || !create_editor)
                return;
            _editors[asset_type] = std::move(create_editor);
        }

        Ref<DockWindow> AssetEditorRegistry::CreateEditor(Asset *asset) const
        {
            if (asset == nullptr || asset->_asset_type == nullptr)
                return nullptr;
            auto iter = _editors.find(asset->_asset_type);
            if (iter == _editors.end())
                return nullptr;
            return iter->second(asset);
        }

        bool AssetEditorRegistry::HasEditor(const Type *asset_type) const
        {
            return asset_type != nullptr && _editors.find(asset_type) != _editors.end();
        }

        void AssetEditorRegistry::RegisterOpenHandler(const Type *asset_type, AssetOpenHandler handler)
        {
            if (asset_type == nullptr || !handler)
                return;
            _open_handlers[asset_type] = std::move(handler);
        }

        bool AssetEditorRegistry::HasOpenHandler(const Type *asset_type) const
        {
            return asset_type != nullptr && _open_handlers.find(asset_type) != _open_handlers.end();
        }

        bool AssetEditorRegistry::CanOpen(const Type *asset_type) const
        {
            return HasEditor(asset_type) || HasOpenHandler(asset_type);
        }

        bool AssetEditorRegistry::Open(Asset *asset)
        {
            if (asset == nullptr || asset->_asset_type == nullptr)
                return false;
            if (auto iter = _open_handlers.find(asset->_asset_type); iter != _open_handlers.end())
            {
                iter->second(asset);
                return true;
            }
            if (auto editor = CreateEditor(asset); editor != nullptr)
            {
                DockManager::Get().AddDock(editor);
                return true;
            }
            return false;
        }
    }// namespace Editor
}// namespace Ailu
