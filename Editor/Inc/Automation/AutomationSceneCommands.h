#pragma once
#ifndef __AUTOMATION_SCENE_COMMANDS_H__
#define __AUTOMATION_SCENE_COMMANDS_H__

#include "Automation/AutomationCommand.h"

namespace Ailu
{
    namespace Editor
    {
        // Add a component to an entity by stable type name; undo removes it.
        class AddComponentCommand final : public IEditorCommand
        {
        public:
            AddComponentCommand(Guid scene_guid, Guid entity_guid, String component_type);

            EditorCommandResult Validate(EditorCommandContext &context) override;
            EditorCommandResult Execute(EditorCommandContext &context) override;
            EditorCommandResult Undo(EditorCommandContext &context) override;
            StringView Name() const override { return "AddComponent"; }

        private:
            Guid _scene_guid;
            Guid _entity_guid;
            String _component_type;
        };

        // Remove a component from an entity by stable type name; undo re-adds it.
        class RemoveComponentCommand final : public IEditorCommand
        {
        public:
            RemoveComponentCommand(Guid scene_guid, Guid entity_guid, String component_type);

            EditorCommandResult Validate(EditorCommandContext &context) override;
            EditorCommandResult Execute(EditorCommandContext &context) override;
            EditorCommandResult Undo(EditorCommandContext &context) override;
            StringView Name() const override { return "RemoveComponent"; }

        private:
            Guid _scene_guid;
            Guid _entity_guid;
            String _component_type;
        };

        // Create an empty entity; undo removes it. Reports its guid as a temp result.
        class CreateEntityCommand final : public IEditorCommand
        {
        public:
            CreateEntityCommand(Guid scene_guid, String name);

            EditorCommandResult Validate(EditorCommandContext &context) override;
            EditorCommandResult Execute(EditorCommandContext &context) override;
            EditorCommandResult Undo(EditorCommandContext &context) override;
            StringView Name() const override { return "CreateEntity"; }
            bool GetTempResult(AutomationValue &out) const override;

        private:
            Guid _scene_guid;
            String _name;
            Guid _created_entity_guid;
        };

        // Reparent an entity; undo restores the previous parent.
        class ReparentEntityCommand final : public IEditorCommand
        {
        public:
            ReparentEntityCommand(Guid scene_guid, Guid entity_guid, Guid new_parent_guid);

            EditorCommandResult Validate(EditorCommandContext &context) override;
            EditorCommandResult Execute(EditorCommandContext &context) override;
            EditorCommandResult Undo(EditorCommandContext &context) override;
            StringView Name() const override { return "ReparentEntity"; }

        private:
            Guid _scene_guid;
            Guid _entity_guid;
            Guid _new_parent_guid;
            Guid _old_parent_guid;
            bool _has_old_parent = false;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_SCENE_COMMANDS_H__
