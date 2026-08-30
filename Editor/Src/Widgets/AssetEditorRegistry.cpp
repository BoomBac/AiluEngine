#include "Widgets/AssetEditorRegistry.h"

#include "Assets/Asset.h"
#include "Assets/PrefabAsset.h"
#include "Assets/ScriptAsset.h"
#include "Assets/WidgetAsset.h"
#include "Animation/AnimationControllerAsset.h"
#include "Animation/Clip.h"
#include "Animation/SkeletonAsset.h"
#include "Audio/AudioClip.h"
#include "Common/Selection.h"
#include "Dock/DockManager.h"
#include "Editors/AudioClipEditor.h"
#include "Editors/AnimationClipEditor.h"
#include "Editors/AnimationControllerEditor.h"
#include "Editors/InputActionAssetEditor.h"
#include "Editors/SpriteAssetEditor.h"
#include "Editors/SpriteAtlasEditor.h"
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
#include "Render/2D/SpriteAtlas.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/Texture.h"
#include "Editors/MeshAssetEditor.h"
#include "Editors/SkeletonMeshAssetEditor.h"
#include "Editors/SkeletonAssetEditor.h"
#include "Editors/MaterialAssetEditor.h"
#include "Editors/TextureAssetEditor.h"
#include "Scene/PrefabSystem.h"
#include "Scene/Scene.h"

#include <filesystem>

namespace Ailu
{
    namespace Editor
    {
        AssetEditorRegistry::AssetEditorRegistry()
        {
            RegisterEditor<GraphAsset, GraphEditorWindow>();
            RegisterEditor<WidgetAsset, WidgetEditor>();
            RegisterEditor<Render::Sprite, SpriteAssetEditor>();
            RegisterEditor<Render::SpriteAtlas, SpriteAtlasEditor>();
            RegisterEditor<Render::Mesh, MeshAssetEditor>();
            RegisterEditor<Render::Texture2D, TextureAssetEditor>();
            RegisterEditor<Render::Material, MaterialAssetEditor>();

            RegisterEditor<AnimationClip, AnimationClipEditor>();
            RegisterEditor<AnimationControllerAsset, AnimationControllerEditor>();
            RegisterEditor<InputActionAsset, InputActionAssetEditor>();
            RegisterEditor<AudioClip, AudioClipEditor>();
            RegisterEditor<SkeletonAsset, SkeletonAssetEditor>();

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

            RegisterEditor<Render::SkeletonMesh, SkeletonMeshAssetEditor>();
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
