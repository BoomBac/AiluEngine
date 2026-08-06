#include "Automation/AutomationDestructiveService.h"

#include "Automation/AutomationCommand.h"
#include "Automation/AutomationDestructiveCommands.h"
#include "Scene/Scene.h"

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            using namespace SceneManagement;

            AutomationResult CommandResultToAutomation(const EditorCommandResult &result)
            {
                if (result._success)
                    return AutomationResult::Ok(AutomationValue{});
                const String code = result._error_code.empty() ? AutomationErrors::kCommandFailed : result._error_code;
                return AutomationResult::Fail(code, result._message);
            }

            AutomationResult ExecuteManaged(Scope<IEditorCommand> command)
            {
                return CommandResultToAutomation(EditorCommandManager::Get().ExecuteCommand(std::move(command)));
            }
        }// namespace

        void AutomationDestructiveService::Register(EditorAutomationRegistry &registry)
        {
            {
                AutomationMethodDesc desc;
                desc._name = "scene.delete_entity";
                desc._description = "Permanently delete an entity and its subtree (destructive, not undoable).";
                desc._permission = EAutomationPermission::kDestructiveWrite;
                desc._input_schema.AddParam("entity_guid", "Persistent guid of the entity.", EAutomationValueType::kGuid, true);
                registry.Register("scene.delete_entity", std::move(desc), HandleDeleteEntity);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "scene.save";
                desc._description = "Save the active scene asset to disk.";
                desc._permission = EAutomationPermission::kDestructiveWrite;
                registry.Register("scene.save", std::move(desc), HandleSaveScene);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "asset.delete";
                desc._description = "Delete an asset from the project (destructive, not undoable).";
                desc._permission = EAutomationPermission::kDestructiveWrite;
                desc._input_schema.AddParam("asset_guid", "Persistent guid of the asset.", EAutomationValueType::kGuid, true);
                registry.Register("asset.delete", std::move(desc), HandleDeleteAsset);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "asset.reimport";
                desc._description = "Reimport an asset from its source file (destructive).";
                desc._permission = EAutomationPermission::kDestructiveWrite;
                desc._input_schema.AddParam("asset_guid", "Persistent guid of the asset.", EAutomationValueType::kGuid, true);
                registry.Register("asset.reimport", std::move(desc), HandleReimportAsset);
            }
        }

        AutomationResult AutomationDestructiveService::HandleDeleteEntity(EditorAutomationContext &, const AutomationObject &arguments)
        {
            const auto guid_it = arguments.find("entity_guid");
            if (guid_it == arguments.end())
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "entity_guid is required");
            const Guid scene_guid = SceneMgr::Get().ActiveScene() != nullptr ? SceneMgr::Get().ActiveScene()->AssetGuid() : Guid{};
            return ExecuteManaged(MakeScope<DeleteEntityCommand>(scene_guid, Guid(guid_it->second.AsString())));
        }

        AutomationResult AutomationDestructiveService::HandleSaveScene(EditorAutomationContext &, const AutomationObject &)
        {
            return ExecuteManaged(MakeScope<SaveSceneCommand>());
        }

        AutomationResult AutomationDestructiveService::HandleDeleteAsset(EditorAutomationContext &, const AutomationObject &arguments)
        {
            const auto guid_it = arguments.find("asset_guid");
            if (guid_it == arguments.end())
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "asset_guid is required");
            return ExecuteManaged(MakeScope<AssetDeleteCommand>(Guid(guid_it->second.AsString())));
        }

        AutomationResult AutomationDestructiveService::HandleReimportAsset(EditorAutomationContext &, const AutomationObject &arguments)
        {
            const auto guid_it = arguments.find("asset_guid");
            if (guid_it == arguments.end())
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "asset_guid is required");
            return ExecuteManaged(MakeScope<AssetReimportCommand>(Guid(guid_it->second.AsString())));
        }
    }// namespace Editor
}// namespace Ailu
