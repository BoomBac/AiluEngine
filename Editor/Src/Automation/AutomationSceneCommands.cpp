#include "Automation/AutomationSceneCommands.h"

#include "Automation/AutomationReadModel.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            using namespace SceneManagement;

            // Resolve the editor descriptor for a stable component type name.
            const ComponentEditorInfo *FindComponentInfo(StringView component_type)
            {
                for (const ComponentEditorInfo &info : ComponentEditorRegistry::Get().Components())
                {
                    if (ComponentStableTypeName(info._component_type) == component_type)
                        return &info;
                }
                return nullptr;
            }

            Scene *ResolveScene(EditorCommandContext &context)
            {
                return context._scene != nullptr ? context._scene : SceneMgr::Get().ActiveScene();
            }
        }// namespace

        AddComponentCommand::AddComponentCommand(Guid scene_guid, Guid entity_guid, String component_type)
            : _scene_guid(std::move(scene_guid)),
              _entity_guid(std::move(entity_guid)),
              _component_type(std::move(component_type))
        {
        }

        EditorCommandResult AddComponentCommand::Validate(EditorCommandContext &context)
        {
            Scene *scene = ResolveScene(context);
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            if (scene->FindEntity(_entity_guid) == ECS::kInvalidEntity)
                return {false, AutomationErrors::kEntityNotFound, "entity not found"};
            if (FindComponentInfo(_component_type) == nullptr)
                return {false, AutomationErrors::kComponentNotFound, "component type is not supported"};
            return {true, "", ""};
        }

        EditorCommandResult AddComponentCommand::Execute(EditorCommandContext &context)
        {
            Scene *scene = ResolveScene(context);
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            const ECS::Entity entity = scene->FindEntity(_entity_guid);
            if (entity == ECS::kInvalidEntity)
                return {false, AutomationErrors::kEntityNotFound, "entity not found"};
            const ComponentEditorInfo *info = FindComponentInfo(_component_type);
            if (info == nullptr)
                return {false, AutomationErrors::kComponentNotFound, "component type is not supported"};
            if (info->_has_component(scene->GetRegister(), entity))
                return {false, AutomationErrors::kCommandFailed, "entity already has the component"};
            info->_add_component(scene->GetRegister(), entity);
            scene->MarkStructureChanged();
            return {true, "", ""};
        }

        EditorCommandResult AddComponentCommand::Undo(EditorCommandContext &context)
        {
            Scene *scene = ResolveScene(context);
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            const ECS::Entity entity = scene->FindEntity(_entity_guid);
            if (entity == ECS::kInvalidEntity)
                return {false, AutomationErrors::kEntityNotFound, "entity not found"};
            const ComponentEditorInfo *info = FindComponentInfo(_component_type);
            if (info == nullptr)
                return {false, AutomationErrors::kComponentNotFound, "component type is not supported"};
            info->_remove_component(scene->GetRegister(), entity);
            scene->MarkStructureChanged();
            return {true, "", ""};
        }

        RemoveComponentCommand::RemoveComponentCommand(Guid scene_guid, Guid entity_guid, String component_type)
            : _scene_guid(std::move(scene_guid)),
              _entity_guid(std::move(entity_guid)),
              _component_type(std::move(component_type))
        {
        }

        EditorCommandResult RemoveComponentCommand::Validate(EditorCommandContext &context)
        {
            Scene *scene = ResolveScene(context);
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            if (scene->FindEntity(_entity_guid) == ECS::kInvalidEntity)
                return {false, AutomationErrors::kEntityNotFound, "entity not found"};
            if (FindComponentInfo(_component_type) == nullptr)
                return {false, AutomationErrors::kComponentNotFound, "component type is not supported"};
            return {true, "", ""};
        }

        EditorCommandResult RemoveComponentCommand::Execute(EditorCommandContext &context)
        {
            Scene *scene = ResolveScene(context);
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            const ECS::Entity entity = scene->FindEntity(_entity_guid);
            if (entity == ECS::kInvalidEntity)
                return {false, AutomationErrors::kEntityNotFound, "entity not found"};
            const ComponentEditorInfo *info = FindComponentInfo(_component_type);
            if (info == nullptr)
                return {false, AutomationErrors::kComponentNotFound, "component type is not supported"};
            if (!info->_has_component(scene->GetRegister(), entity))
                return {false, AutomationErrors::kCommandFailed, "entity does not have the component"};
            info->_remove_component(scene->GetRegister(), entity);
            scene->MarkStructureChanged();
            return {true, "", ""};
        }

        EditorCommandResult RemoveComponentCommand::Undo(EditorCommandContext &context)
        {
            Scene *scene = ResolveScene(context);
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            const ECS::Entity entity = scene->FindEntity(_entity_guid);
            if (entity == ECS::kInvalidEntity)
                return {false, AutomationErrors::kEntityNotFound, "entity not found"};
            const ComponentEditorInfo *info = FindComponentInfo(_component_type);
            if (info == nullptr)
                return {false, AutomationErrors::kComponentNotFound, "component type is not supported"};
            info->_add_component(scene->GetRegister(), entity);
            scene->MarkStructureChanged();
            return {true, "", ""};
        }

        CreateEntityCommand::CreateEntityCommand(Guid scene_guid, String name)
            : _scene_guid(std::move(scene_guid)), _name(std::move(name))
        {
        }

        EditorCommandResult CreateEntityCommand::Validate(EditorCommandContext &context)
        {
            Scene *scene = ResolveScene(context);
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            return {true, "", ""};
        }

        EditorCommandResult CreateEntityCommand::Execute(EditorCommandContext &context)
        {
            Scene *scene = ResolveScene(context);
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            const ECS::Entity entity = scene->AddObject(_name);
            if (entity == ECS::kInvalidEntity)
                return {false, AutomationErrors::kCommandFailed, "failed to create entity"};
            _created_entity_guid = scene->GetEntityGuid(entity);
            return {true, "", ""};
        }

        EditorCommandResult CreateEntityCommand::Undo(EditorCommandContext &context)
        {
            Scene *scene = ResolveScene(context);
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            if (_created_entity_guid.IsValid())
            {
                const ECS::Entity entity = scene->FindEntity(_created_entity_guid);
                if (entity != ECS::kInvalidEntity)
                    scene->RemoveObject(entity);
            }
            return {true, "", ""};
        }

        bool CreateEntityCommand::GetTempResult(AutomationValue &out) const
        {
            if (!_created_entity_guid.IsValid())
                return false;
            out = AutomationValue(_created_entity_guid.ToString());
            return true;
        }

        ReparentEntityCommand::ReparentEntityCommand(Guid scene_guid, Guid entity_guid, Guid new_parent_guid)
            : _scene_guid(std::move(scene_guid)),
              _entity_guid(std::move(entity_guid)),
              _new_parent_guid(std::move(new_parent_guid))
        {
        }

        EditorCommandResult ReparentEntityCommand::Validate(EditorCommandContext &context)
        {
            Scene *scene = ResolveScene(context);
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            if (scene->FindEntity(_entity_guid) == ECS::kInvalidEntity)
                return {false, AutomationErrors::kEntityNotFound, "entity not found"};
            return {true, "", ""};
        }

        EditorCommandResult ReparentEntityCommand::Execute(EditorCommandContext &context)
        {
            Scene *scene = ResolveScene(context);
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            const ECS::Entity entity = scene->FindEntity(_entity_guid);
            if (entity == ECS::kInvalidEntity)
                return {false, AutomationErrors::kEntityNotFound, "entity not found"};

            const ECS::CHierarchy *hierarchy = scene->GetRegister().GetComponent<ECS::CHierarchy>(entity);
            const ECS::Entity old_parent = hierarchy != nullptr ? hierarchy->_parent : ECS::kInvalidEntity;
            _has_old_parent = old_parent != ECS::kInvalidEntity;
            if (_has_old_parent)
                _old_parent_guid = scene->GetEntityGuid(old_parent);

            const ECS::Entity new_parent = _new_parent_guid.IsValid() ? scene->FindEntity(_new_parent_guid) : ECS::kInvalidEntity;
            if (_new_parent_guid.IsValid() && new_parent == ECS::kInvalidEntity)
                return {false, AutomationErrors::kEntityNotFound, "new parent not found"};
            if (!scene->Reparent(entity, new_parent, true))
                return {false, AutomationErrors::kCommandFailed, "failed to reparent entity"};
            return {true, "", ""};
        }

        EditorCommandResult ReparentEntityCommand::Undo(EditorCommandContext &context)
        {
            Scene *scene = ResolveScene(context);
            if (scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            const ECS::Entity entity = scene->FindEntity(_entity_guid);
            if (entity == ECS::kInvalidEntity)
                return {false, AutomationErrors::kEntityNotFound, "entity not found"};
            const ECS::Entity old_parent = _has_old_parent ? scene->FindEntity(_old_parent_guid) : ECS::kInvalidEntity;
            scene->Reparent(entity, old_parent, true);
            return {true, "", ""};
        }
    }// namespace Editor
}// namespace Ailu
