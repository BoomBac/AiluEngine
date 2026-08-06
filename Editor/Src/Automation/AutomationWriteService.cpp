#include "Automation/AutomationWriteService.h"

#include "Automation/AutomationCommand.h"
#include "Automation/AutomationSceneCommands.h"
#include "Automation/AutomationTransaction.h"
#include "Scene/Scene.h"

#include <format>

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

            Guid ActiveSceneGuid()
            {
                const Scene *scene = SceneMgr::Get().ActiveScene();
                return scene != nullptr ? scene->AssetGuid() : Guid{};
            }
        }// namespace

        void AutomationWriteService::Register(EditorAutomationRegistry &registry)
        {
            {
                AutomationMethodDesc desc;
                desc._name = "scene.set_property";
                desc._description = "Write a component/entity property through an undoable command.";
                desc._permission = EAutomationPermission::kSafeWrite;
                desc._input_schema.AddParam("target", "Target reference: {kind, scene_guid, entity_guid, component_type}.", EAutomationValueType::kObject, true);
                desc._input_schema.AddParam("property_path", "Stable property path (e.g. _local_transform._position).", EAutomationValueType::kString, true);
                desc._input_schema.AddParam("value", "New property value.", EAutomationValueType::kObject, true);
                desc._input_schema.AddParam("expected_scene_revision", "Optional scene edit revision for conflict detection.", EAutomationValueType::kInteger);
                registry.Register("scene.set_property", std::move(desc), HandleSetProperty);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "asset.set_property";
                desc._description = "Write a reflected asset property through an undoable command.";
                desc._permission = EAutomationPermission::kSafeWrite;
                desc._input_schema.AddParam("asset_guid", "Persistent guid of the asset.", EAutomationValueType::kGuid, true);
                desc._input_schema.AddParam("property_path", "Reflected property name.", EAutomationValueType::kString, true);
                desc._input_schema.AddParam("value", "New property value.", EAutomationValueType::kObject, true);
                registry.Register("asset.set_property", std::move(desc), HandleAssetSetProperty);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "scene.rename_entity";
                desc._description = "Rename an entity by guid (undoable).";
                desc._permission = EAutomationPermission::kSafeWrite;
                desc._input_schema.AddParam("entity_guid", "Persistent guid of the entity.", EAutomationValueType::kGuid, true);
                desc._input_schema.AddParam("new_name", "New entity name.", EAutomationValueType::kString, true);
                registry.Register("scene.rename_entity", std::move(desc), HandleRenameEntity);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "scene.add_component";
                desc._description = "Add a component by stable type name (undoable).";
                desc._permission = EAutomationPermission::kSafeWrite;
                desc._input_schema.AddParam("entity_guid", "Persistent guid of the entity.", EAutomationValueType::kGuid, true);
                desc._input_schema.AddParam("component_type", "Stable component type name.", EAutomationValueType::kString, true);
                registry.Register("scene.add_component", std::move(desc), HandleAddComponent);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "scene.remove_component";
                desc._description = "Remove a component by stable type name (undoable).";
                desc._permission = EAutomationPermission::kSafeWrite;
                desc._input_schema.AddParam("entity_guid", "Persistent guid of the entity.", EAutomationValueType::kGuid, true);
                desc._input_schema.AddParam("component_type", "Stable component type name.", EAutomationValueType::kString, true);
                registry.Register("scene.remove_component", std::move(desc), HandleRemoveComponent);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "scene.create_entity";
                desc._description = "Create an empty entity in the active scene (undoable). Returns its guid.";
                desc._permission = EAutomationPermission::kSafeWrite;
                desc._input_schema.AddParam("name", "Entity name.", EAutomationValueType::kString);
                registry.Register("scene.create_entity", std::move(desc), HandleCreateEntity);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "scene.reparent_entity";
                desc._description = "Reparent an entity under another (undoable).";
                desc._permission = EAutomationPermission::kSafeWrite;
                desc._input_schema.AddParam("entity_guid", "Persistent guid of the entity.", EAutomationValueType::kGuid, true);
                desc._input_schema.AddParam("new_parent_guid", "Persistent guid of the new parent; omit to detach to root.", EAutomationValueType::kGuid);
                registry.Register("scene.reparent_entity", std::move(desc), HandleReparentEntity);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "editor.undo";
                desc._description = "Undo the last automation command.";
                desc._permission = EAutomationPermission::kSafeWrite;
                registry.Register("editor.undo", std::move(desc), HandleUndo);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "editor.redo";
                desc._description = "Redo the last undone automation command.";
                desc._permission = EAutomationPermission::kSafeWrite;
                registry.Register("editor.redo", std::move(desc), HandleRedo);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "editor.preview_transaction";
                desc._description = "Validate a transaction and return an operation summary without applying it.";
                desc._permission = EAutomationPermission::kReadOnly;
                desc._input_schema.AddParam("expected_scene_revision", "Optional scene edit revision for conflict detection.", EAutomationValueType::kInteger);
                desc._input_schema.AddParam("operations", "Array of {operation, temporary_id, arguments}.", EAutomationValueType::kObject, true, true);
                registry.Register("editor.preview_transaction", std::move(desc), HandlePreviewTransaction);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "editor.apply_transaction";
                desc._description = "Apply a transaction atomically as a single undo unit.";
                desc._permission = EAutomationPermission::kSafeWrite;
                desc._input_schema.AddParam("expected_scene_revision", "Optional scene edit revision for conflict detection.", EAutomationValueType::kInteger);
                desc._input_schema.AddParam("operations", "Array of {operation, temporary_id, arguments}.", EAutomationValueType::kObject, true, true);
                registry.Register("editor.apply_transaction", std::move(desc), HandleApplyTransaction);
            }
        }

        AutomationResult AutomationWriteService::HandleSetProperty(EditorAutomationContext &, const AutomationObject &arguments)
        {
            Scene *scene = SceneMgr::Get().ActiveScene();
            if (scene == nullptr)
                return AutomationResult::Fail(AutomationErrors::kSceneNotOpen, "no scene is open");

            const auto target_it = arguments.find("target");
            if (target_it == arguments.end() || !target_it->second.IsObject())
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "target is required");
            const AutomationTargetRef target = ParseTarget(target_it->second.AsObject());
            if (target._entity_guid.IsEmpty() || target._component_type.empty())
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "target needs entity_guid and component_type");

            const auto path_it = arguments.find("property_path");
            const auto value_it = arguments.find("value");
            if (path_it == arguments.end() || value_it == arguments.end())
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "property_path and value are required");

            if (const auto revision_it = arguments.find("expected_scene_revision"); revision_it != arguments.end())
            {
                if (revision_it->second.AsUInt() != scene->EditRevision())
                    return AutomationResult::Fail(AutomationErrors::kSceneRevisionConflict,
                                                  "scene edit revision changed since the request was prepared");
            }

            return ExecuteManaged(MakeScope<SetPropertyCommand>(target, path_it->second.AsString(), value_it->second));
        }

        AutomationResult AutomationWriteService::HandleAssetSetProperty(EditorAutomationContext &, const AutomationObject &arguments)
        {
            const auto guid_it = arguments.find("asset_guid");
            const auto path_it = arguments.find("property_path");
            const auto value_it = arguments.find("value");
            if (guid_it == arguments.end() || path_it == arguments.end() || value_it == arguments.end())
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "asset_guid, property_path and value are required");

            AutomationTargetRef target;
            target._kind = EAutomationTargetKind::kAsset;
            target._asset_guid = Guid(guid_it->second.AsString());
            if (target._asset_guid.IsEmpty() || !target._asset_guid.IsValid())
                return AutomationResult::Fail(AutomationErrors::kInvalidGuid, "asset_guid is invalid");

            return ExecuteManaged(MakeScope<SetPropertyCommand>(target, path_it->second.AsString(), value_it->second));
        }

        AutomationResult AutomationWriteService::HandleRenameEntity(EditorAutomationContext &, const AutomationObject &arguments)
        {
            const auto guid_it = arguments.find("entity_guid");
            const auto name_it = arguments.find("new_name");
            if (guid_it == arguments.end() || name_it == arguments.end())
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "entity_guid and new_name are required");

            AutomationTargetRef target;
            target._kind = EAutomationTargetKind::kComponent;
            target._entity_guid = Guid(guid_it->second.AsString());
            target._component_type = "Ailu.ECS.TagComponent";
            return ExecuteManaged(MakeScope<SetPropertyCommand>(target, "_name", name_it->second));
        }

        AutomationResult AutomationWriteService::HandleAddComponent(EditorAutomationContext &, const AutomationObject &arguments)
        {
            const auto guid_it = arguments.find("entity_guid");
            const auto type_it = arguments.find("component_type");
            if (guid_it == arguments.end() || type_it == arguments.end())
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "entity_guid and component_type are required");

            const Guid scene_guid = SceneMgr::Get().ActiveScene() != nullptr ? SceneMgr::Get().ActiveScene()->AssetGuid() : Guid{};
            return ExecuteManaged(MakeScope<AddComponentCommand>(scene_guid, Guid(guid_it->second.AsString()), type_it->second.AsString()));
        }

        AutomationResult AutomationWriteService::HandleRemoveComponent(EditorAutomationContext &, const AutomationObject &arguments)
        {
            const auto guid_it = arguments.find("entity_guid");
            const auto type_it = arguments.find("component_type");
            if (guid_it == arguments.end() || type_it == arguments.end())
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "entity_guid and component_type are required");

            const Guid scene_guid = SceneMgr::Get().ActiveScene() != nullptr ? SceneMgr::Get().ActiveScene()->AssetGuid() : Guid{};
            return ExecuteManaged(MakeScope<RemoveComponentCommand>(scene_guid, Guid(guid_it->second.AsString()), type_it->second.AsString()));
        }

        AutomationResult AutomationWriteService::HandleUndo(EditorAutomationContext &, const AutomationObject &)
        {
            return CommandResultToAutomation(EditorCommandManager::Get().Undo());
        }

        AutomationResult AutomationWriteService::HandleRedo(EditorAutomationContext &, const AutomationObject &)
        {
            return CommandResultToAutomation(EditorCommandManager::Get().Redo());
        }

        AutomationResult AutomationWriteService::HandleCreateEntity(EditorAutomationContext &, const AutomationObject &arguments)
        {
            const Guid scene_guid = ActiveSceneGuid();
            const String name = arguments.contains("name") ? arguments.at("name").AsString() : String{};
            return ExecuteManaged(MakeScope<CreateEntityCommand>(scene_guid, name));
        }

        AutomationResult AutomationWriteService::HandleReparentEntity(EditorAutomationContext &, const AutomationObject &arguments)
        {
            const auto guid_it = arguments.find("entity_guid");
            if (guid_it == arguments.end())
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "entity_guid is required");
            const String parent_string = arguments.contains("new_parent_guid") ? arguments.at("new_parent_guid").AsString() : String{};
            return ExecuteManaged(MakeScope<ReparentEntityCommand>(ActiveSceneGuid(), Guid(guid_it->second.AsString()), Guid(parent_string)));
        }

        AutomationResult AutomationWriteService::HandlePreviewTransaction(EditorAutomationContext &, const AutomationObject &arguments)
        {
            AutomationTransaction transaction;
            String error;
            if (!ParseTransaction(arguments, transaction, error))
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, error);

            const HashMap<String, TransactionOperationBuilder> builders = BuildDefaultTransactionBuilders();
            Scene *scene = SceneMgr::Get().ActiveScene();
            if (scene == nullptr)
                return AutomationResult::Fail(AutomationErrors::kSceneNotOpen, "no scene is open");
            if (transaction._has_expected_scene_revision && transaction._expected_scene_revision != scene->EditRevision())
                return AutomationResult::Fail(AutomationErrors::kSceneRevisionConflict,
                                              "scene edit revision changed since the request was prepared");

            AutomationArray summary;
            HashMap<String, String> declared_temp_ids;
            for (const AutomationTransactionOperation &operation : transaction._operations)
            {
                AutomationObject entry;
                entry.emplace("operation", AutomationValue(operation._operation));
                if (!operation._temporary_id.empty())
                    entry.emplace("temporary_id", AutomationValue(operation._temporary_id));
                if (!builders.contains(operation._operation))
                    return AutomationResult::Fail(AutomationErrors::kActionNotFound,
                                                  std::format("unknown transaction operation '{}'", operation._operation));
                entry.emplace("valid", AutomationValue(true));
                if (!operation._temporary_id.empty())
                    declared_temp_ids.emplace(operation._temporary_id, operation._operation);
                summary.emplace_back(AutomationValue(std::move(entry)));
            }

            AutomationObject result;
            result.emplace("scene_guid", AutomationValue(scene->AssetGuid().ToString()));
            result.emplace("scene_edit_revision", AutomationValue(scene->EditRevision()));
            result.emplace("operations", AutomationValue(std::move(summary)));
            result.emplace("apply_required", AutomationValue(true));
            return AutomationResult::Ok(AutomationValue(std::move(result)));
        }

        AutomationResult AutomationWriteService::HandleApplyTransaction(EditorAutomationContext &, const AutomationObject &arguments)
        {
            AutomationTransaction transaction;
            String error;
            if (!ParseTransaction(arguments, transaction, error))
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, error);

            return ExecuteManaged(MakeScope<TransactionCommand>(std::move(transaction), BuildDefaultTransactionBuilders()));
        }
    }// namespace Editor
}// namespace Ailu
