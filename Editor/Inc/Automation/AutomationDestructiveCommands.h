#pragma once
#ifndef __AUTOMATION_DESTRUCTIVE_COMMANDS_H__
#define __AUTOMATION_DESTRUCTIVE_COMMANDS_H__

#include "Automation/AutomationCommand.h"

namespace Ailu
{
    namespace Editor
    {
        // Destructive commands are executed immediately and never pushed to the
        // undo stack (AllowUndo() == false).

        // Remove an entity (and its subtree) from the scene.
        class DeleteEntityCommand final : public IEditorCommand
        {
        public:
            DeleteEntityCommand(Guid scene_guid, Guid entity_guid);
            EditorCommandResult Validate(EditorCommandContext &context) override;
            EditorCommandResult Execute(EditorCommandContext &context) override;
            EditorCommandResult Undo(EditorCommandContext &context) override;
            StringView Name() const override { return "DeleteEntity"; }
            bool AllowUndo() const override { return false; }

        private:
            Guid _scene_guid;
            Guid _entity_guid;
        };

        // Save the active scene asset.
        class SaveSceneCommand final : public IEditorCommand
        {
        public:
            EditorCommandResult Validate(EditorCommandContext &context) override;
            EditorCommandResult Execute(EditorCommandContext &context) override;
            EditorCommandResult Undo(EditorCommandContext &context) override;
            StringView Name() const override { return "SaveScene"; }
            bool AllowUndo() const override { return false; }
        };

        // Delete an asset from the project.
        class AssetDeleteCommand final : public IEditorCommand
        {
        public:
            explicit AssetDeleteCommand(Guid asset_guid);
            EditorCommandResult Validate(EditorCommandContext &context) override;
            EditorCommandResult Execute(EditorCommandContext &context) override;
            EditorCommandResult Undo(EditorCommandContext &context) override;
            StringView Name() const override { return "AssetDelete"; }
            bool AllowUndo() const override { return false; }

        private:
            Guid _asset_guid;
        };

        // Reimport an asset from its source file.
        class AssetReimportCommand final : public IEditorCommand
        {
        public:
            explicit AssetReimportCommand(Guid asset_guid);
            EditorCommandResult Validate(EditorCommandContext &context) override;
            EditorCommandResult Execute(EditorCommandContext &context) override;
            EditorCommandResult Undo(EditorCommandContext &context) override;
            StringView Name() const override { return "AssetReimport"; }
            bool AllowUndo() const override { return false; }

        private:
            Guid _asset_guid;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_DESTRUCTIVE_COMMANDS_H__
