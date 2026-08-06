#include "Automation/AutomationDestructiveCommands.h"

#include "Assets/Asset.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Scene.h"

#include <filesystem>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            using namespace SceneManagement;

            Scene *ResolveScene(EditorCommandContext &context)
            {
                return context._scene != nullptr ? context._scene : SceneMgr::Get().ActiveScene();
            }

            const Asset *FindAssetByGuid(const Guid &guid)
            {
                for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
                {
                    const Asset *asset = it->second.get();
                    if (asset != nullptr && asset->GetGuid() == guid)
                        return asset;
                }
                return nullptr;
            }
        }// namespace

        DeleteEntityCommand::DeleteEntityCommand(Guid scene_guid, Guid entity_guid)
            : _scene_guid(std::move(scene_guid)), _entity_guid(std::move(entity_guid))
        {
        }

        EditorCommandResult DeleteEntityCommand::Validate(EditorCommandContext &context)
        {
            Scene *scene = ResolveScene(context);
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            if (scene->FindEntity(_entity_guid) == ECS::kInvalidEntity)
                return {false, AutomationErrors::kEntityNotFound, "entity not found"};
            return {true, "", ""};
        }

        EditorCommandResult DeleteEntityCommand::Execute(EditorCommandContext &context)
        {
            Scene *scene = ResolveScene(context);
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            const ECS::Entity entity = scene->FindEntity(_entity_guid);
            if (entity == ECS::kInvalidEntity)
                return {false, AutomationErrors::kEntityNotFound, "entity not found"};
            scene->RemoveObject(entity);
            scene->MarkStructureChanged();
            return {true, "", ""};
        }

        EditorCommandResult DeleteEntityCommand::Undo(EditorCommandContext &)
        {
            return {false, AutomationErrors::kCommandFailed, "delete entity is not undoable"};
        }

        EditorCommandResult SaveSceneCommand::Validate(EditorCommandContext &context)
        {
            Scene *scene = ResolveScene(context);
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            return {true, "", ""};
        }

        EditorCommandResult SaveSceneCommand::Execute(EditorCommandContext &context)
        {
            Scene *scene = ResolveScene(context);
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
            {
                Asset *asset = it->second.get();
                if (asset != nullptr && asset->_p_obj.get() == static_cast<Object *>(scene))
                {
                    ResourceMgr::Get().SaveAsset(asset);
                    return {true, "", ""};
                }
            }
            return {false, AutomationErrors::kAssetNotFound, "active scene is not registered as an asset"};
        }

        EditorCommandResult SaveSceneCommand::Undo(EditorCommandContext &)
        {
            return {false, AutomationErrors::kCommandFailed, "save scene is not undoable"};
        }

        AssetDeleteCommand::AssetDeleteCommand(Guid asset_guid) : _asset_guid(std::move(asset_guid))
        {
        }

        EditorCommandResult AssetDeleteCommand::Validate(EditorCommandContext &)
        {
            if (!_asset_guid.IsValid() || _asset_guid.IsEmpty())
                return {false, AutomationErrors::kInvalidGuid, "asset_guid is invalid"};
            if (FindAssetByGuid(_asset_guid) == nullptr)
                return {false, AutomationErrors::kAssetNotFound, "asset not found"};
            return {true, "", ""};
        }

        EditorCommandResult AssetDeleteCommand::Execute(EditorCommandContext &)
        {
            const Asset *asset = FindAssetByGuid(_asset_guid);
            if (asset == nullptr)
                return {false, AutomationErrors::kAssetNotFound, "asset not found"};
            ResourceMgr::Get().DeleteAsset(const_cast<Asset *>(asset));
            return {true, "", ""};
        }

        EditorCommandResult AssetDeleteCommand::Undo(EditorCommandContext &)
        {
            return {false, AutomationErrors::kCommandFailed, "asset delete is not undoable"};
        }

        AssetReimportCommand::AssetReimportCommand(Guid asset_guid) : _asset_guid(std::move(asset_guid))
        {
        }

        EditorCommandResult AssetReimportCommand::Validate(EditorCommandContext &)
        {
            if (!_asset_guid.IsValid() || _asset_guid.IsEmpty())
                return {false, AutomationErrors::kInvalidGuid, "asset_guid is invalid"};
            if (FindAssetByGuid(_asset_guid) == nullptr)
                return {false, AutomationErrors::kAssetNotFound, "asset not found"};
            return {true, "", ""};
        }

        EditorCommandResult AssetReimportCommand::Execute(EditorCommandContext &)
        {
            const Asset *asset = FindAssetByGuid(_asset_guid);
            if (asset == nullptr)
                return {false, AutomationErrors::kAssetNotFound, "asset not found"};

            const WString source = asset->_external_asset_path.empty() ? asset->_asset_path : asset->_external_asset_path;
            const WString target_dir = std::filesystem::path(asset->_asset_path).parent_path().wstring();
            ResourceMgr::Get().DeleteAsset(const_cast<Asset *>(asset));
            ResourceMgr::Get().ImportResource(source, target_dir);
            return {true, "", ""};
        }

        EditorCommandResult AssetReimportCommand::Undo(EditorCommandContext &)
        {
            return {false, AutomationErrors::kCommandFailed, "asset reimport is not undoable"};
        }
    }// namespace Editor
}// namespace Ailu
