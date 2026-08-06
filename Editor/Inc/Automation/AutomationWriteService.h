#pragma once
#ifndef __AUTOMATION_WRITE_SERVICE_H__
#define __AUTOMATION_WRITE_SERVICE_H__

#include "Automation/AutomationRegistry.h"

namespace Ailu
{
    namespace Editor
    {
        // Registers the safe-write tools. Every write is executed as an
        // IEditorCommand through the EditorCommandManager so it can be undone.
        //   scene.set_property
        //   asset.set_property
        //   scene.rename_entity
        //   scene.create_entity
        //   scene.reparent_entity
        //   scene.add_component
        //   scene.remove_component
        //   editor.undo
        //   editor.redo
        //   editor.preview_transaction
        //   editor.apply_transaction
        class AutomationWriteService final : public NonCopyable
        {
        public:
            static void Register(EditorAutomationRegistry &registry);

        private:
            static AutomationResult HandleSetProperty(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleAssetSetProperty(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleRenameEntity(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleCreateEntity(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleReparentEntity(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleAddComponent(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleRemoveComponent(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleUndo(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleRedo(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandlePreviewTransaction(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleApplyTransaction(EditorAutomationContext &context, const AutomationObject &arguments);
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_WRITE_SERVICE_H__
