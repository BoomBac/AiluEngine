#include "Automation/AutomationTransaction.h"

#include "Automation/AutomationSceneCommands.h"
#include "Scene/Scene.h"

#include <format>

namespace Ailu
{
    namespace Editor
    {
        AutomationTargetRef ParseTarget(const AutomationObject &object)
        {
            AutomationTargetRef target;
            const String kind = object.contains("kind") ? object.at("kind").AsString() : String{"component"};
            if (kind == "entity")
                target._kind = EAutomationTargetKind::kEntity;
            else if (kind == "asset")
                target._kind = EAutomationTargetKind::kAsset;
            else if (kind == "object")
                target._kind = EAutomationTargetKind::kObject;
            else
                target._kind = EAutomationTargetKind::kComponent;
            target._scene_guid = Guid(object.contains("scene_guid") ? object.at("scene_guid").AsString() : String{});
            target._entity_guid = Guid(object.contains("entity_guid") ? object.at("entity_guid").AsString() : String{});
            target._asset_guid = Guid(object.contains("asset_guid") ? object.at("asset_guid").AsString() : String{});
            target._component_type = object.contains("component_type") ? object.at("component_type").AsString() : String{};
            target._object_type = object.contains("object_type") ? object.at("object_type").AsString() : String{};
            return target;
        }

        AutomationValue ResolveTransactionValue(const AutomationValue &value, const HashMap<String, AutomationValue> &temp_results)
        {
            if (value.IsString())
            {
                const String &text = value.AsString();
                if (text.size() > 1 && text[0] == '$')
                {
                    const auto it = temp_results.find(text.substr(1));
                    if (it != temp_results.end())
                        return it->second;
                }
                return value;
            }
            if (value.IsArray())
            {
                AutomationArray array;
                for (const AutomationValue &item : value.AsArray())
                    array.emplace_back(ResolveTransactionValue(item, temp_results));
                return AutomationValue(std::move(array));
            }
            if (value.IsObject())
            {
                AutomationObject object;
                for (const auto &[key, item] : value.AsObject())
                    object.emplace(key, ResolveTransactionValue(item, temp_results));
                return AutomationValue(std::move(object));
            }
            return value;
        }

        AutomationObject ResolveTransactionTempRefs(const AutomationObject &arguments, const HashMap<String, AutomationValue> &temp_results)
        {
            AutomationObject result;
            for (const auto &[key, value] : arguments)
                result.emplace(key, ResolveTransactionValue(value, temp_results));
            return result;
        }

        bool ParseTransaction(const AutomationObject &arguments, AutomationTransaction &out, String &error)
        {
            const auto operations_it = arguments.find("operations");
            if (operations_it == arguments.end() || !operations_it->second.IsArray() || operations_it->second.AsArray().empty())
            {
                error = "operations array is required";
                return false;
            }
            if (const auto revision_it = arguments.find("expected_scene_revision"); revision_it != arguments.end())
            {
                out._has_expected_scene_revision = true;
                out._expected_scene_revision = revision_it->second.AsUInt();
            }
            for (const AutomationValue &entry : operations_it->second.AsArray())
            {
                if (!entry.IsObject())
                {
                    error = "each operation must be an object";
                    return false;
                }
                const AutomationObject &operation = entry.AsObject();
                const auto name_it = operation.find("operation");
                if (name_it == operation.end())
                {
                    error = "operation name is required";
                    return false;
                }
                AutomationTransactionOperation op;
                op._operation = name_it->second.AsString();
                if (const auto tid_it = operation.find("temporary_id"); tid_it != operation.end())
                    op._temporary_id = tid_it->second.AsString();
                if (const auto args_it = operation.find("arguments"); args_it != operation.end() && args_it->second.IsObject())
                    op._arguments = args_it->second.AsObject();
                out._operations.emplace_back(std::move(op));
            }
            return true;
        }

        // -----------------------------------------------------------------------
        // Default operation builders (operation name -> IEditorCommand factory)
        // -----------------------------------------------------------------------
        HashMap<String, TransactionOperationBuilder> BuildDefaultTransactionBuilders()
        {
            using namespace SceneManagement;
            const auto active_scene_guid = []() -> Guid
            {
                const Scene *scene = SceneMgr::Get().ActiveScene();
                return scene != nullptr ? scene->AssetGuid() : Guid{};
            };

            HashMap<String, TransactionOperationBuilder> builders;
            builders.emplace("scene.create_entity", [active_scene_guid](const AutomationObject &args) -> Scope<IEditorCommand>
            {
                return MakeScope<CreateEntityCommand>(active_scene_guid(),
                                                      args.contains("name") ? args.at("name").AsString() : String{});
            });
            builders.emplace("scene.reparent_entity", [active_scene_guid](const AutomationObject &args) -> Scope<IEditorCommand>
            {
                return MakeScope<ReparentEntityCommand>(active_scene_guid(),
                                                        Guid(args.contains("entity_guid") ? args.at("entity_guid").AsString() : String{}),
                                                        Guid(args.contains("new_parent_guid") ? args.at("new_parent_guid").AsString() : String{}));
            });
            builders.emplace("scene.add_component", [active_scene_guid](const AutomationObject &args) -> Scope<IEditorCommand>
            {
                return MakeScope<AddComponentCommand>(active_scene_guid(),
                                                      Guid(args.contains("entity_guid") ? args.at("entity_guid").AsString() : String{}),
                                                      args.contains("component_type") ? args.at("component_type").AsString() : String{});
            });
            builders.emplace("scene.remove_component", [active_scene_guid](const AutomationObject &args) -> Scope<IEditorCommand>
            {
                return MakeScope<RemoveComponentCommand>(active_scene_guid(),
                                                         Guid(args.contains("entity_guid") ? args.at("entity_guid").AsString() : String{}),
                                                         args.contains("component_type") ? args.at("component_type").AsString() : String{});
            });
            builders.emplace("scene.set_property", [](const AutomationObject &args) -> Scope<IEditorCommand>
            {
                const auto target_it = args.find("target");
                const auto path_it = args.find("property_path");
                const auto value_it = args.find("value");
                if (target_it == args.end() || !target_it->second.IsObject() || path_it == args.end() || value_it == args.end())
                    return nullptr;
                return MakeScope<SetPropertyCommand>(ParseTarget(target_it->second.AsObject()),
                                                     path_it->second.AsString(),
                                                     value_it->second);
            });
            builders.emplace("scene.rename_entity", [](const AutomationObject &args) -> Scope<IEditorCommand>
            {
                const auto guid_it = args.find("entity_guid");
                const auto name_it = args.find("new_name");
                if (guid_it == args.end() || name_it == args.end())
                    return nullptr;
                AutomationTargetRef target;
                target._kind = EAutomationTargetKind::kComponent;
                target._entity_guid = Guid(guid_it->second.AsString());
                target._component_type = "Ailu.ECS.TagComponent";
                return MakeScope<SetPropertyCommand>(target, "_name", name_it->second);
            });
            builders.emplace("asset.set_property", [](const AutomationObject &args) -> Scope<IEditorCommand>
            {
                const auto guid_it = args.find("asset_guid");
                const auto path_it = args.find("property_path");
                const auto value_it = args.find("value");
                if (guid_it == args.end() || path_it == args.end() || value_it == args.end())
                    return nullptr;
                AutomationTargetRef target;
                target._kind = EAutomationTargetKind::kAsset;
                target._asset_guid = Guid(guid_it->second.AsString());
                return MakeScope<SetPropertyCommand>(target, path_it->second.AsString(), value_it->second);
            });
            return builders;
        }

        // -----------------------------------------------------------------------
        // TransactionCommand
        // -----------------------------------------------------------------------
        TransactionCommand::TransactionCommand(AutomationTransaction transaction, HashMap<String, TransactionOperationBuilder> builders)
            : _transaction(std::move(transaction)), _builders(std::move(builders))
        {
        }

        Scope<IEditorCommand> TransactionCommand::Build(const AutomationTransactionOperation &operation, const AutomationObject &resolved_arguments)
        {
            const auto it = _builders.find(operation._operation);
            return it != _builders.end() ? it->second(resolved_arguments) : nullptr;
        }

        bool TransactionCommand::IsStructural(StringView operation_name)
        {
            return operation_name == "scene.create_entity" || operation_name == "scene.reparent_entity" ||
                   operation_name == "scene.add_component" || operation_name == "scene.remove_component";
        }

        EditorCommandResult TransactionCommand::Validate(EditorCommandContext &context)
        {
            SceneManagement::Scene *scene = context._scene != nullptr ? context._scene : SceneManagement::SceneMgr::Get().ActiveScene();
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            if (_transaction._has_expected_scene_revision && _transaction._expected_scene_revision != scene->EditRevision())
                return {false, AutomationErrors::kSceneRevisionConflict, "scene edit revision changed since the request was prepared"};
            for (const AutomationTransactionOperation &operation : _transaction._operations)
            {
                if (!_builders.contains(operation._operation))
                    return {false, AutomationErrors::kActionNotFound,
                            std::format("unknown transaction operation '{}'", operation._operation)};
            }
            return {true, "", ""};
        }

        EditorCommandResult TransactionCommand::Execute(EditorCommandContext &context)
        {
            SceneManagement::Scene *scene = context._scene != nullptr ? context._scene : SceneManagement::SceneMgr::Get().ActiveScene();
            const u64 start_edit = scene != nullptr ? scene->EditRevision() : 0u;
            const auto rollback = [&]()
            {
                for (auto it = _executed.rbegin(); it != _executed.rend(); ++it)
                    (*it)->Undo(context);
                _executed.clear();
                if (scene != nullptr)
                    scene->SetEditRevision(start_edit);
            };

            _temp_results.clear();
            _executed.clear();
            for (const AutomationTransactionOperation &operation : _transaction._operations)
            {
                const AutomationObject resolved = ResolveTransactionTempRefs(operation._arguments, _temp_results);
                Scope<IEditorCommand> command = Build(operation, resolved);
                if (command == nullptr)
                {
                    rollback();
                    return {false, AutomationErrors::kActionNotFound,
                            std::format("unknown transaction operation '{}'", operation._operation)};
                }
                EditorCommandResult result = command->Validate(context);
                if (result._success)
                    result = command->Execute(context);
                if (!result._success)
                {
                    rollback();
                    return result;
                }
                if (!operation._temporary_id.empty())
                {
                    AutomationValue temporary;
                    if (command->GetTempResult(temporary))
                        _temp_results.emplace(operation._temporary_id, std::move(temporary));
                }
                _executed.emplace_back(std::move(command));
            }

            // The whole transaction counts as a single edit revision.
            if (scene != nullptr)
                scene->SetEditRevision(start_edit + 1u);
            return {true, "", ""};
        }

        EditorCommandResult TransactionCommand::Undo(EditorCommandContext &context)
        {
            SceneManagement::Scene *scene = context._scene != nullptr ? context._scene : SceneManagement::SceneMgr::Get().ActiveScene();
            const u64 start_edit = scene != nullptr ? scene->EditRevision() : 0u;
            for (auto it = _executed.rbegin(); it != _executed.rend(); ++it)
            {
                EditorCommandResult result = (*it)->Undo(context);
                if (!result._success)
                    return result;
            }
            if (scene != nullptr)
                scene->SetEditRevision(start_edit + 1u);
            return {true, "", ""};
        }
    }// namespace Editor
}// namespace Ailu
