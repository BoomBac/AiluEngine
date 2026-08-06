#include "Automation/AutomationSceneService.h"

#include "Automation/AutomationAdapter.h"
#include "Automation/AutomationReadModel.h"
#include "Common/Selection.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Utils.h"
#include "Project/ProjectManager.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

#include <format>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            using namespace SceneManagement;

            Scene *ActiveScene()
            {
                return SceneMgr::Get().ActiveScene();
            }

            String BuildHierarchyPath(Scene *scene, ECS::Entity entity)
            {
                auto &reg = scene->GetRegister();
                Vector<String> parts;
                ECS::Entity current = entity;
                u32 guard = 0u;
                while (current != ECS::kInvalidEntity && guard++ < 1024u)
                {
                    const ECS::TagComponent *tag = reg.GetComponent<ECS::TagComponent>(current);
                    parts.emplace_back(tag ? tag->_name : String{});
                    const ECS::CHierarchy *hierarchy = reg.GetComponent<ECS::CHierarchy>(current);
                    current = hierarchy ? hierarchy->_parent : ECS::kInvalidEntity;
                }
                String path = "/";
                bool first = true;
                for (auto it = parts.rbegin(); it != parts.rend(); ++it)
                {
                    if (!first)
                        path += "/";
                    path += *it;
                    first = false;
                }
                return path;
            }

            String EntityName(Scene *scene, ECS::Entity entity)
            {
                const ECS::TagComponent *tag = scene->GetRegister().GetComponent<ECS::TagComponent>(entity);
                return tag ? tag->_name : String{};
            }

            String EntityGuidString(Scene *scene, ECS::Entity entity)
            {
                const Guid *guid = scene->FindEntityGuid(entity);
                return guid ? guid->ToString() : String{};
            }
        }// namespace

        void AutomationSceneService::Register(EditorAutomationRegistry &registry)
        {
            {
                AutomationMethodDesc desc;
                desc._name = "editor.get_state";
                desc._description = "Return editor state: mode, active scene, revisions and selection.";
                desc._permission = EAutomationPermission::kReadOnly;
                registry.Register("editor.get_state", std::move(desc), HandleGetState);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "scene.query_entities";
                desc._description = "Query entities in the active scene by name, required components and hierarchy path.";
                desc._permission = EAutomationPermission::kReadOnly;
                desc._input_schema.AddParam("name", "Entity name filter.", EAutomationValueType::kString);
                desc._input_schema.AddParam("name_match", "Name matching mode: exact, prefix or substring.", EAutomationValueType::kString);
                desc._input_schema.AddParam("required_components", "Stable component type names the entity must have.", EAutomationValueType::kString, false, true);
                desc._input_schema.AddParam("path_prefix", "Hierarchy path prefix filter.", EAutomationValueType::kString);
                desc._input_schema.AddParam("offset", "Pagination offset.", EAutomationValueType::kInteger);
                desc._input_schema.AddParam("limit", "Maximum number of items.", EAutomationValueType::kInteger);
                registry.Register("scene.query_entities", std::move(desc), HandleQueryEntities);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "scene.inspect_entity";
                desc._description = "Inspect an entity by guid: structure, component types and optional property values.";
                desc._permission = EAutomationPermission::kReadOnly;
                desc._input_schema.AddParam("entity_guid", "Persistent guid of the entity.", EAutomationValueType::kGuid, true);
                desc._input_schema.AddParam("include_components", "Include the component list.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("include_properties", "Read all readable component property values.", EAutomationValueType::kBool);
                desc._input_schema.AddParam("components", "Per-type property requests: [{type, property_paths}].", EAutomationValueType::kObject, false, true);
                registry.Register("scene.inspect_entity", std::move(desc), HandleInspectEntity);
            }
        }

        AutomationResult AutomationSceneService::HandleGetState(EditorAutomationContext &, const AutomationObject &)
        {
            AutomationObject result;

            String mode = "edit";
            if (Application::Get()._is_playing_mode)
                mode = "play";
            else if (Application::Get()._is_simulate_mode)
                mode = "simulate";
            result.emplace("editor_mode", AutomationValue(std::move(mode)));

            result.emplace("project_path",
                           AutomationValue(ProjectManager::Get().HasOpenedProject()
                                               ? ToChar(ProjectManager::Get().CurrentProject().RootDirectory())
                                               : String{}));

            Scene *scene = ActiveScene();
            if (scene != nullptr)
            {
                result.emplace("scene_guid", AutomationValue(scene->AssetGuid().ToString()));
                result.emplace("scene_name", AutomationValue(scene->Name()));
                result.emplace("scene_structure_revision", AutomationValue(scene->StructureRevision()));
                result.emplace("scene_edit_revision", AutomationValue(scene->EditRevision()));
                result.emplace("entity_count", AutomationValue(scene->EntityNum()));
            }
            else
            {
                result.emplace("scene_guid", AutomationValue(String{}));
                result.emplace("scene_name", AutomationValue(String{}));
            }

            AutomationArray selection;
            if (scene != nullptr)
            {
                for (ECS::Entity entity : Selection::SelectedEntities())
                {
                    const String guid = EntityGuidString(scene, entity);
                    if (!guid.empty())
                        selection.emplace_back(AutomationValue(guid));
                }
            }
            result.emplace("selection", AutomationValue(std::move(selection)));
            return AutomationResult::Ok(AutomationValue(std::move(result)));
        }

        AutomationResult AutomationSceneService::HandleQueryEntities(EditorAutomationContext &, const AutomationObject &arguments)
        {
            Scene *scene = ActiveScene();
            if (scene == nullptr)
                return AutomationResult::Fail(AutomationErrors::kSceneNotOpen, "no scene is open");

            const String name_filter = arguments.contains("name") ? arguments.at("name").AsString() : String{};
            const String name_match = arguments.contains("name_match") ? arguments.at("name_match").AsString() : String{"substring"};
            const String path_prefix = arguments.contains("path_prefix") ? arguments.at("path_prefix").AsString() : String{};
            const i64 offset = arguments.contains("offset") ? arguments.at("offset").AsInt() : 0;
            const i64 limit = arguments.contains("limit") ? arguments.at("limit").AsInt() : 20;

            Vector<String> required_components;
            if (const auto it = arguments.find("required_components"); it != arguments.end() && it->second.IsArray())
            {
                for (const AutomationValue &item : it->second.AsArray())
                    required_components.emplace_back(item.AsString());
            }

            auto &reg = scene->GetRegister();

            // Collect candidate entities through the TagComponent view (every scene entity has one).
            Vector<ECS::Entity> entities;
            u32 index = 0u;
            for (const auto &tag : reg.View<ECS::TagComponent>())
            {
                (void) tag;
                entities.push_back(reg.GetEntity<ECS::TagComponent>(index++));
            }

            AutomationArray items;
            bool has_more = false;
            i64 to_skip = offset;
            for (ECS::Entity entity : entities)
            {
                const String entity_name = EntityName(scene, entity);

                if (!name_filter.empty())
                {
                    if (name_match == "exact")
                    {
                        if (entity_name != name_filter)
                            continue;
                    }
                    else if (name_match == "prefix")
                    {
                        if (entity_name.rfind(name_filter, 0) != 0)
                            continue;
                    }
                    else
                    {
                        if (entity_name.find(name_filter) == String::npos)
                            continue;
                    }
                }

                if (!required_components.empty())
                {
                    bool has_all = true;
                    for (const String &required : required_components)
                    {
                        bool found = false;
                        for (ECS::ComponentTypeId cid : reg.GetEntityComponentTypes(entity))
                        {
                            if (ComponentStableTypeName(cid) == required)
                            {
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                        {
                            has_all = false;
                            break;
                        }
                    }
                    if (!has_all)
                        continue;
                }

                if (!path_prefix.empty() && BuildHierarchyPath(scene, entity).rfind(path_prefix, 0) != 0)
                    continue;

                if (to_skip > 0)
                {
                    --to_skip;
                    continue;
                }
                if (static_cast<i64>(items.size()) >= limit)
                {
                    has_more = true;
                    break;
                }

                AutomationObject item;
                item.emplace("entity_guid", AutomationValue(EntityGuidString(scene, entity)));
                item.emplace("name", AutomationValue(entity_name));
                item.emplace("hierarchy_path", AutomationValue(BuildHierarchyPath(scene, entity)));
                AutomationArray component_types;
                for (ECS::ComponentTypeId cid : reg.GetEntityComponentTypes(entity))
                    component_types.emplace_back(AutomationValue(ComponentStableTypeName(cid)));
                item.emplace("component_types", AutomationValue(std::move(component_types)));
                items.emplace_back(AutomationValue(std::move(item)));
            }

            AutomationObject result;
            result.emplace("scene_guid", AutomationValue(scene->AssetGuid().ToString()));
            result.emplace("scene_revision", AutomationValue(scene->EditRevision()));
            result.emplace("items", AutomationValue(std::move(items)));
            result.emplace("has_more", AutomationValue(has_more));
            return AutomationResult::Ok(AutomationValue(std::move(result)));
        }

        AutomationResult AutomationSceneService::HandleInspectEntity(EditorAutomationContext &, const AutomationObject &arguments)
        {
            Scene *scene = ActiveScene();
            if (scene == nullptr)
                return AutomationResult::Fail(AutomationErrors::kSceneNotOpen, "no scene is open");

            const String guid_string = arguments.contains("entity_guid") ? arguments.at("entity_guid").AsString() : String{};
            const Guid guid(guid_string);
            if (!guid.IsValid() || guid.IsEmpty())
                return AutomationResult::Fail(AutomationErrors::kInvalidGuid, "entity_guid is missing or invalid");

            const ECS::Entity entity = scene->FindEntity(guid);
            if (entity == ECS::kInvalidEntity)
                return AutomationResult::Fail(AutomationErrors::kEntityNotFound, "entity not found in the active scene");

            const bool include_properties = arguments.contains("include_properties") && arguments.at("include_properties").AsBool();

            // Optional per-type property path requests: components = [{type, property_paths}]
            HashMap<String, Vector<String>> requested_paths;
            if (const auto it = arguments.find("components"); it != arguments.end() && it->second.IsArray())
            {
                for (const AutomationValue &entry : it->second.AsArray())
                {
                    const AutomationObject &obj = entry.AsObject();
                    const String type = obj.contains("type") ? obj.at("type").AsString() : String{};
                    Vector<String> paths;
                    if (const auto pit = obj.find("property_paths"); pit != obj.end() && pit->second.IsArray())
                    {
                        for (const AutomationValue &path : pit->second.AsArray())
                            paths.emplace_back(path.AsString());
                    }
                    if (!type.empty())
                        requested_paths.emplace(type, std::move(paths));
                }
            }

            auto &reg = scene->GetRegister();
            AutomationObject result;
            result.emplace("entity_guid", AutomationValue(guid_string));
            result.emplace("scene_guid", AutomationValue(scene->AssetGuid().ToString()));
            result.emplace("name", AutomationValue(EntityName(scene, entity)));
            result.emplace("hierarchy_path", AutomationValue(BuildHierarchyPath(scene, entity)));

            const ECS::CHierarchy *hierarchy = reg.GetComponent<ECS::CHierarchy>(entity);
            if (hierarchy != nullptr && hierarchy->_parent != ECS::kInvalidEntity)
                result.emplace("parent_guid", AutomationValue(EntityGuidString(scene, hierarchy->_parent)));

            AutomationArray children;
            if (hierarchy != nullptr)
            {
                ECS::Entity child = hierarchy->_first_child;
                u32 guard = 0u;
                while (child != ECS::kInvalidEntity && guard++ < 4096u)
                {
                    children.emplace_back(AutomationValue(EntityGuidString(scene, child)));
                    const ECS::CHierarchy *child_hierarchy = reg.GetComponent<ECS::CHierarchy>(child);
                    child = child_hierarchy ? child_hierarchy->_next_sibling : ECS::kInvalidEntity;
                }
            }
            result.emplace("children_guids", AutomationValue(std::move(children)));

            AutomationArray components;
            for (ECS::ComponentTypeId cid : reg.GetEntityComponentTypes(entity))
            {
                const String stable_type = ComponentStableTypeName(cid);
                void *instance = reg.GetComponentInstance(entity, cid);
                if (instance == nullptr)
                    continue;

                AutomationObject component;
                component.emplace("type", AutomationValue(stable_type));

                const auto path_it = requested_paths.find(stable_type);
                const bool read_all = include_properties || (path_it != requested_paths.end() && !path_it->second.empty());
                const IAutomationTypeAdapter *adapter = AutomationAdapterRegistry::Get().Resolve(stable_type);
                if (read_all && adapter != nullptr)
                {
                    AutomationObject properties;
                    if (path_it != requested_paths.end() && !path_it->second.empty())
                    {
                        for (const String &path : path_it->second)
                        {
                            AutomationResult value = adapter->ReadProperty(stable_type, instance, path);
                            if (value._success)
                                properties.emplace(path, std::move(value._data));
                        }
                    }
                    else
                    {
                        for (const String &path : AutomationReadablePaths(adapter, stable_type))
                        {
                            AutomationResult value = adapter->ReadProperty(stable_type, instance, path);
                            if (value._success)
                                properties.emplace(path, std::move(value._data));
                        }
                    }
                    component.emplace("properties", AutomationValue(std::move(properties)));
                }
                components.emplace_back(AutomationValue(std::move(component)));
            }
            result.emplace("components", AutomationValue(std::move(components)));
            return AutomationResult::Ok(AutomationValue(std::move(result)));
        }
    }// namespace Editor
}// namespace Ailu
