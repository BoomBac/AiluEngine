#include "Automation/AutomationCommand.h"

#include "Assets/Asset.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Component.h"

namespace Ailu
{
    namespace Editor
    {
        // -----------------------------------------------------------------------
        // Target resolution
        // -----------------------------------------------------------------------
        ResolvedAutomationTarget ResolveTarget(EditorCommandContext &context, const AutomationTargetRef &target)
        {
            ResolvedAutomationTarget resolved;
            SceneManagement::Scene *scene = context._scene != nullptr ? context._scene : SceneManagement::SceneMgr::Get().ActiveScene();
            resolved._scene = scene;
            if (scene == nullptr)
                return resolved;

            switch (target._kind)
            {
                case EAutomationTargetKind::kEntity:
                {
                    if (target._entity_guid.IsValid())
                    {
                        const ECS::Entity entity = scene->FindEntity(target._entity_guid);
                        if (entity != ECS::kInvalidEntity)
                            resolved._entity = entity;
                    }
                    break;
                }
                case EAutomationTargetKind::kComponent:
                {
                    const ECS::Entity entity = scene->FindEntity(target._entity_guid);
                    if (entity == ECS::kInvalidEntity)
                        break;
                    resolved._entity = entity;
                    auto &reg = scene->GetRegister();
                    for (ECS::ComponentTypeId cid : reg.GetEntityComponentTypes(entity))
                    {
                        if (ComponentStableTypeName(cid) == target._component_type)
                        {
                            resolved._instance = reg.GetComponentInstance(entity, cid);
                            resolved._stable_type = target._component_type;
                            break;
                        }
                    }
                    break;
                }
                case EAutomationTargetKind::kAsset:
                {
                    for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
                    {
                        const Asset *asset = it->second.get();
                        if (asset != nullptr && asset->GetGuid() == target._asset_guid)
                        {
                            resolved._asset = asset;
                            resolved._instance = asset->_p_obj.get();
                            resolved._stable_type = asset->_asset_type != nullptr ? NormalizeTypeName(asset->_asset_type->FullName()) : String{};
                            break;
                        }
                    }
                    break;
                }
                default:
                    break;
            }
            return resolved;
        }

        // -----------------------------------------------------------------------
        // EditorCommandManager
        // -----------------------------------------------------------------------
        EditorCommandManager &EditorCommandManager::Get()
        {
            static EditorCommandManager s_instance;
            return s_instance;
        }

        void EditorCommandManager::Clear()
        {
            while (!_undo_stack.empty())
                _undo_stack.pop();
            while (!_redo_stack.empty())
                _redo_stack.pop();
        }

        EditorCommandContext EditorCommandManager::MakeContext() const
        {
            EditorCommandContext context;
            context._scene = SceneManagement::SceneMgr::Get().ActiveScene();
            return context;
        }

        EditorCommandResult EditorCommandManager::ExecuteCommand(Scope<IEditorCommand> command)
        {
            if (command == nullptr)
                return {false, AutomationErrors::kInternalError, "null command"};

            EditorCommandContext context = MakeContext();
            EditorCommandResult result = command->Validate(context);
            if (!result._success)
                return result;
            result = command->Execute(context);
            if (!result._success)
                return result;

            AutomationPropertyChangedEvent event;
            if (command->GetPropertyChange(event))
                _on_property_changed.Invoke(event);

            if (command->AllowUndo())
            {
                _undo_stack.push(std::move(command));
                while (!_redo_stack.empty())
                    _redo_stack.pop();
            }
            return result;
        }

        EditorCommandResult EditorCommandManager::Undo()
        {
            if (_undo_stack.empty())
                return {false, AutomationErrors::kCommandFailed, "nothing to undo"};
            auto command = std::move(_undo_stack.top());
            _undo_stack.pop();

            EditorCommandContext context = MakeContext();
            EditorCommandResult result = command->Undo(context);
            if (!result._success)
                return result;

            AutomationPropertyChangedEvent event;
            if (command->GetPropertyChange(event))
                _on_property_changed.Invoke(event);

            _redo_stack.push(std::move(command));
            return result;
        }

        EditorCommandResult EditorCommandManager::Redo()
        {
            if (_redo_stack.empty())
                return {false, AutomationErrors::kCommandFailed, "nothing to redo"};
            auto command = std::move(_redo_stack.top());
            _redo_stack.pop();

            EditorCommandContext context = MakeContext();
            EditorCommandResult result = command->Execute(context);
            if (!result._success)
                return result;

            AutomationPropertyChangedEvent event;
            if (command->GetPropertyChange(event))
                _on_property_changed.Invoke(event);

            _undo_stack.push(std::move(command));
            return result;
        }

        // -----------------------------------------------------------------------
        // SetPropertyCommand
        // -----------------------------------------------------------------------
        SetPropertyCommand::SetPropertyCommand(AutomationTargetRef target, String property_path, AutomationValue new_value,
                                               AutomationValue old_value, bool has_old_value)
            : _target(std::move(target)),
              _property_path(std::move(property_path)),
              _new_value(std::move(new_value)),
              _old_value(std::move(old_value)),
              _has_old_value(has_old_value)
        {
        }

        EditorCommandResult SetPropertyCommand::Validate(EditorCommandContext &context)
        {
            const ResolvedAutomationTarget resolved = ResolveTarget(context, _target);
            if (resolved._scene == nullptr)
                return {false, AutomationErrors::kSceneNotOpen, "no scene is open"};
            if (resolved._instance == nullptr)
            {
                if (_target._kind == EAutomationTargetKind::kEntity)
                    return {false, AutomationErrors::kEntityNotFound, "entity not found"};
                if (_target._kind == EAutomationTargetKind::kComponent)
                    return {false, AutomationErrors::kComponentNotFound, "component not found"};
                return {false, AutomationErrors::kTargetNotFound, "target not found"};
            }
            if (AutomationAdapterRegistry::Get().Resolve(resolved._stable_type) == nullptr)
                return {false, AutomationErrors::kInvalidType, "no adapter for target type"};
            return {true, "", ""};
        }

        EditorCommandResult SetPropertyCommand::Execute(EditorCommandContext &context)
        {
            ResolvedAutomationTarget resolved = ResolveTarget(context, _target);
            if (resolved._instance == nullptr)
                return {false, AutomationErrors::kTargetNotFound, "target not found"};
            if (resolved._scene != nullptr && !_target._scene_guid.IsValid())
                _target._scene_guid = resolved._scene->AssetGuid();

            if (!_has_old_value)
            {
                if (const IAutomationTypeAdapter *adapter = AutomationAdapterRegistry::Get().Resolve(resolved._stable_type))
                {
                    const AutomationResult read = adapter->ReadProperty(resolved._stable_type, resolved._instance, _property_path);
                    if (read._success)
                    {
                        _old_value = read._data;
                        _has_old_value = true;
                    }
                }
            }
            _last_was_undo = false;
            return Apply(context, _new_value);
        }

        EditorCommandResult SetPropertyCommand::Undo(EditorCommandContext &context)
        {
            if (!_has_old_value)
                return {false, AutomationErrors::kCommandFailed, "no old value recorded for undo"};
            _last_was_undo = true;
            return Apply(context, _old_value);
        }

        EditorCommandResult SetPropertyCommand::Apply(EditorCommandContext &context, const AutomationValue &value)
        {
            const ResolvedAutomationTarget resolved = ResolveTarget(context, _target);
            if (resolved._instance == nullptr)
                return {false, AutomationErrors::kTargetNotFound, "target not found"};
            const IAutomationTypeAdapter *adapter = AutomationAdapterRegistry::Get().Resolve(resolved._stable_type);
            if (adapter == nullptr)
                return {false, AutomationErrors::kInvalidType, "no adapter for target type"};
            const AutomationResult write = adapter->WriteProperty(resolved._stable_type, resolved._instance, _property_path, value);
            if (!write._success)
                return {false, write._error._code, write._error._message};
            if (resolved._scene != nullptr)
                resolved._scene->MarkEdited();
            return {true, "", ""};
        }

        bool SetPropertyCommand::GetPropertyChange(AutomationPropertyChangedEvent &out) const
        {
            if (!_has_old_value)
                return false;
            out._scene_guid = _target._scene_guid;
            out._entity_guid = _target._entity_guid;
            out._asset_guid = _target._asset_guid;
            out._target_type = _target._component_type.empty() ? _target._object_type : _target._component_type;
            out._property_path = _property_path;
            if (_last_was_undo)
            {
                out._old_value = _new_value;
                out._new_value = _old_value;
            }
            else
            {
                out._old_value = _old_value;
                out._new_value = _new_value;
            }
            return true;
        }
    }// namespace Editor
}// namespace Ailu
