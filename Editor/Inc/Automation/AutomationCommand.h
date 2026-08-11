#pragma once
#ifndef __AUTOMATION_COMMAND_H__
#define __AUTOMATION_COMMAND_H__

#include "Automation/AutomationAdapter.h"
#include "Automation/AutomationTypes.h"
#include "Framework/Common/NonCopyable.h"
#include "Framework/Core/Delegate.h"
#include "Framework/Events/Event.h"
#include "Scene/Scene.h"

#include <memory>
#include <stack>

namespace Ailu
{
    class Asset;

    namespace Editor
    {
        // ---- Stable target reference (plan section 5) ----
        enum class EAutomationTargetKind
        {
            kProject,
            kScene,
            kEntity,
            kComponent,
            kObject,
            kAsset,
            kEditor,
        };

        struct AutomationTargetRef
        {
            EAutomationTargetKind _kind = EAutomationTargetKind::kEntity;
            Guid _project_guid;
            Guid _scene_guid;
            Guid _entity_guid;
            Guid _asset_guid;
            String _component_type;
            String _object_type;
        };

        // Parse an AutomationTargetRef from a JSON-like target object.
        AutomationTargetRef ParseTarget(const AutomationObject &object);

        struct EditorCommandResult
        {
            bool _success = false;
            String _error_code;
            String _message;
        };

        struct EditorCommandContext
        {
            SceneManagement::Scene *_scene = nullptr;
        };

        struct AutomationPropertyChangedEvent;

        class IEditorCommand
        {
        public:
            virtual ~IEditorCommand() = default;
            virtual EditorCommandResult Validate(EditorCommandContext &context) = 0;
            virtual EditorCommandResult Execute(EditorCommandContext &context) = 0;
            virtual EditorCommandResult Undo(EditorCommandContext &context) = 0;
            virtual StringView Name() const = 0;

            // Commands that change a single property report it so the manager can
            // publish an AutomationPropertyChangedEvent after Execute / Undo.
            virtual bool GetPropertyChange(AutomationPropertyChangedEvent &out) const { return false; }

            // Commands that produce a temporary result (e.g. a created entity guid)
            // report it for transaction temporary-id references.
            virtual bool GetTempResult(AutomationValue &out) const { return false; }

            // Destructive commands (delete, save, reimport) are not pushed to the
            // undo stack; the manager executes them immediately.
            virtual bool AllowUndo() const { return true; }
        };

        // Resolved target pointing at live scene/asset data (only valid during the call).
        struct ResolvedAutomationTarget
        {
            SceneManagement::Scene *_scene = nullptr;
            ECS::Entity _entity = ECS::kInvalidEntity;
            void *_instance = nullptr;
            String _stable_type;
            const Asset *_asset = nullptr;
        };
        ResolvedAutomationTarget ResolveTarget(EditorCommandContext &context, const AutomationTargetRef &target);

        // ---- Property changed event (plan section 20) ----
        struct AutomationPropertyChangedEvent
        {
            Guid _scene_guid;
            Guid _entity_guid;
            Guid _asset_guid;
            String _target_type;
            String _property_path;
            AutomationValue _old_value;
            AutomationValue _new_value;
        };

        class EditorCommandManager final : public NonCopyable
        {
        public:
            static EditorCommandManager &Get();
            void Clear();

            EditorCommandResult ExecuteCommand(Scope<IEditorCommand> command);
            EditorCommandResult Undo();
            EditorCommandResult Redo();
            bool CanUndo() const { return !_undo_stack.empty(); }
            bool CanRedo() const { return !_redo_stack.empty(); }
            size_t UndoCount() const { return _undo_stack.size(); }
            size_t RedoCount() const { return _redo_stack.size(); }

            Delegate<const AutomationPropertyChangedEvent &> &OnPropertyChanged() { return _on_property_changed; }

        private:
            EditorCommandContext MakeContext() const;

            std::stack<std::unique_ptr<IEditorCommand>> _undo_stack;
            std::stack<std::unique_ptr<IEditorCommand>> _redo_stack;
            Delegate<const AutomationPropertyChangedEvent &> _on_property_changed;
        };

        // ---- Generic set-property command ----
        class SetPropertyCommand final : public IEditorCommand
        {
        public:
            SetPropertyCommand(AutomationTargetRef target, String property_path, AutomationValue new_value,
                               AutomationValue old_value = AutomationValue{}, bool has_old_value = false);

            EditorCommandResult Validate(EditorCommandContext &context) override;
            EditorCommandResult Execute(EditorCommandContext &context) override;
            EditorCommandResult Undo(EditorCommandContext &context) override;
            StringView Name() const override { return "SetProperty"; }
            bool GetPropertyChange(AutomationPropertyChangedEvent &out) const override;

        private:
            EditorCommandResult Apply(EditorCommandContext &context, const AutomationValue &value);

            AutomationTargetRef _target;
            String _property_path;
            AutomationValue _old_value;
            AutomationValue _new_value;
            bool _has_old_value = false;
            bool _last_was_undo = false;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_COMMAND_H__
